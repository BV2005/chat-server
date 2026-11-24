#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include "TCP_SERVER.h"
#include "Client_create_and_manage.h"
#include "Router.h"
#include "Room_manager.h"
#include "packet.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#pragma comment(lib, "Mswsock.lib")
#pragma comment(lib, "ws2_32.lib")

constexpr std::size_t ADDR_BUF_SIZE = sizeof(sockaddr_in) + 16;
constexpr int WORKER_COUNT = 4;
constexpr int ACCEPT_DEPTH = 16;
constexpr int64_t UNAUTHENTICATED_TIMEOUT_MS = 30'000;
constexpr int64_t IDLE_TIMEOUT_MS = 120'000;

LPFN_ACCEPTEX AcceptExPtr = nullptr;
std::atomic_bool stopping{false};
std::atomic_uint64_t nextConnectionId{1};
Client_Manager clientManager;
RoomManager roomManager;
Router router(&clientManager, &roomManager);
PacketParser packetParser;

struct Io_Context {
    OVERLAPPED ov{};
    SOCKET client = INVALID_SOCKET;
    OpType op{};
    char addrbuf[ADDR_BUF_SIZE * 2]{};
};

struct Receive_Context {
    OVERLAPPED ov{};
    SOCKET client = INVALID_SOCKET;
    OpType op = OP_RECV;
    std::weak_ptr<Client> owner;
    char recvbuf[4096]{};
    WSABUF wsabuf{};
};

struct ServerState {
    HANDLE iocp;
    SOCKET listenSocket;
};

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void DisconnectClient(const std::shared_ptr<Client> &client) {
    if (!client) return;
    client->logged.store(false);
    roomManager.RemoveClient(client);
    clientManager.RemoveClient(client); // identity check prevents a recycled SOCKET from removing a new client.
    if (client->sock != INVALID_SOCKET && !client->closed.exchange(true)) {
        shutdown(client->sock, SD_BOTH);
        closesocket(client->sock);
    }
}

SOCKET createSocket() {
    return WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
}

bool postsend(Send_Context *context) {
    DWORD sent = 0;
    const int result = WSASend(context->client, &context->wsabuf, 1, &sent, 0, &context->ov, nullptr);
    return result == 0 || WSAGetLastError() == WSA_IO_PENDING;
}

void postreceive(const std::shared_ptr<Client> &client) {
    if (!clientManager.Contains(client)) return;
    auto *context = new(std::nothrow) Receive_Context{};
    if (!context) {
        DisconnectClient(client);
        return;
    }
    context->client = client->sock;
    context->owner = client;
    context->wsabuf.buf = context->recvbuf;
    context->wsabuf.len = sizeof(context->recvbuf);
    DWORD flags = 0, received = 0;
    const int result = WSARecv(context->client, &context->wsabuf, 1, &received, &flags, &context->ov, nullptr);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        delete context;
        DisconnectClient(client);
    }
}

void postaccept(SOCKET listenSocket) {
    SOCKET client = createSocket();
    if (client == INVALID_SOCKET) return;
    auto *context = new(std::nothrow) Io_Context{};
    if (!context) {
        closesocket(client);
        return;
    }
    context->client = client;
    context->op = OP_ACCEPT;
    DWORD bytes = 0;
    if (!AcceptExPtr(listenSocket, client, context->addrbuf, 0, ADDR_BUF_SIZE, ADDR_BUF_SIZE, &bytes, &context->ov)
        && WSAGetLastError() != ERROR_IO_PENDING) {
        closesocket(client);
        delete context;
    }
}

void HandleReceive(Receive_Context *context, DWORD bytes) {
    std::unique_ptr<Receive_Context> cleanup(context);
    const auto client = context->owner.lock();
    if (!client || !clientManager.Contains(client) || bytes == 0) {
        DisconnectClient(client);
        return;
    }
    client->Touch();
    client->recvAccumulator.insert(client->recvAccumulator.end(), context->recvbuf, context->recvbuf + bytes);
    Packet packet;
    for (;;) {
        const auto result = packetParser.Parse(client->recvAccumulator, packet);
        if (result == PacketParser::Result::NeedMore) break;
        if (result == PacketParser::Result::Invalid) {
            DisconnectClient(client);
            return;
        }
        router.Dispatch(client, packet);
        // LOGOUT (or an error handler added later) can remove this connection mid-buffer.
        if (!clientManager.Contains(client)) return;
    }
    postreceive(client);
}

void HandleFailedOperation(Io_Context *context) {
    switch (context->op) {
        case OP_ACCEPT: closesocket(context->client);
            delete context;
            break;
        case OP_RECV: {
            auto *receive = reinterpret_cast<Receive_Context *>(context);
            auto client = receive->owner.lock();
            delete receive;
            DisconnectClient(client);
            break;
        }
        case OP_SEND: {
            auto *send = reinterpret_cast<Send_Context *>(context);
            if (auto owner = send->owner.lock()) owner->OnSendFailed(send);
            else delete send;
            break;
        }
    }
}

void WorkerThread(ServerState *state) {
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;
        const BOOL ok = GetQueuedCompletionStatus(state->iocp, &bytes, &key, &overlapped, 1000);
        if (!overlapped) {
            if (stopping.load()) break;
            continue;
        }
        auto *context = reinterpret_cast<Io_Context *>(overlapped);
        if (!ok) {
            HandleFailedOperation(context);
            continue;
        }
        switch (context->op) {
            case OP_ACCEPT: {
                std::unique_ptr<Io_Context> cleanup(context);
                if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(context->client), state->iocp, 0, 0)
                    || setsockopt(context->client, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                                  reinterpret_cast<const char *>(&state->listenSocket),
                                  sizeof(state->listenSocket)) == SOCKET_ERROR) {
                    closesocket(context->client);
                } else {
                    auto client = std::make_shared<Client>(context->client, nextConnectionId.fetch_add(1));
                    clientManager.AddClient(client);
                    postreceive(client);
                }
                if (!stopping.load()) postaccept(state->listenSocket);
                break;
            }
            case OP_RECV: HandleReceive(reinterpret_cast<Receive_Context *>(context), bytes);
                break;
            case OP_SEND: {
                auto *send = reinterpret_cast<Send_Context *>(context);
                if (auto owner = send->owner.lock()) owner->OnSendCompleted(send, bytes);
                else delete send;
                break;
            }
        }
    }
}

void TimeoutThread() {
    while (!stopping.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto now = NowMs();
        for (const auto &client: clientManager.Snapshot()) {
            const auto timeout = client->loggedIn() ? IDLE_TIMEOUT_MS : UNAUTHENTICATED_TIMEOUT_MS;
            if (now - client->lastActivityMs.load(std::memory_order_relaxed) > timeout) DisconnectClient(client);
        }
    }
}

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        stopping.store(true);
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <port>\n";
        return 1;
    }
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    SOCKET listenSocket = createSocket();
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(std::atoi(argv[1])));
    address.sin_addr.s_addr = INADDR_ANY;
    if (listenSocket == INVALID_SOCKET || bind(listenSocket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ==
        SOCKET_ERROR || listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        if (listenSocket != INVALID_SOCKET) closesocket(listenSocket);
        WSACleanup();
        return 1;
    }
    GUID guid = WSAID_ACCEPTEX;
    DWORD bytes = 0;
    if (WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &AcceptExPtr,
                 sizeof(AcceptExPtr), &bytes, nullptr, nullptr) == SOCKET_ERROR) {
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }
    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!iocp || !CreateIoCompletionPort(reinterpret_cast<HANDLE>(listenSocket), iocp, 0, 0)) {
        closesocket(listenSocket);
        if (iocp) CloseHandle(iocp);
        WSACleanup();
        return 1;
    }
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    ServerState state{iocp, listenSocket};
    std::vector<std::thread> workers;
    for (int i = 0; i < WORKER_COUNT; ++i) workers.emplace_back(WorkerThread, &state);
    std::thread timeout(TimeoutThread);
    for (int i = 0; i < ACCEPT_DEPTH; ++i) postaccept(listenSocket);
    std::cout << "Server listening on port " << argv[1]
            << " (new accounts are saved to chat_users.txt)\n";
    while (!stopping.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    closesocket(listenSocket);
    for (const auto &client: clientManager.Snapshot()) DisconnectClient(client);
    for (int i = 0; i < WORKER_COUNT; ++i) PostQueuedCompletionStatus(iocp, 0, 0, nullptr);
    for (auto &worker: workers) worker.join();
    timeout.join();
    CloseHandle(iocp);
    WSACleanup();
}
