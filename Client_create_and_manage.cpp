#include "Client_create_and_manage.h"
#include "packet.h"

#include <chrono>
#include <stdexcept>

namespace {
    int64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

Client::Client(SOCKET s, uint64_t id) : sock(s), connectionId(id) { Touch(); }

Client::~Client() {
    std::lock_guard<std::mutex> lock(sendMutex);
    while (!sendQueue.empty()) {
        delete sendQueue.front();
        sendQueue.pop();
    }
    if (sock != INVALID_SOCKET && !closed.exchange(true)) closesocket(sock);
}

void Client::EnqueueSend(const Packet &packet) {
    auto *context = new Send_Context();
    context->client = sock;
    context->owner = weak_from_this();
    context->op = OP_SEND;
    try { context->buffer = packet.Serialize(); } catch (const std::length_error &) {
        delete context;
        return;
    }
    context->wsabuf.buf = context->buffer.data();
    context->wsabuf.len = static_cast<ULONG>(context->buffer.size());
    Send_Context *next = nullptr;
    {
        std::lock_guard<std::mutex> lock(sendMutex);
        sendQueue.push(context);
        if (sending) return;
        sending = true;
        next = sendQueue.front();
        sendQueue.pop();
    }
    PostNextSend(next);
}

void Client::OnSendCompleted(Send_Context *context, DWORD bytesTransferred) {
    Send_Context *next = nullptr;
    if (bytesTransferred < context->wsabuf.len) {
        context->wsabuf.buf += bytesTransferred;
        context->wsabuf.len -= bytesTransferred;
        context->ov = {};
        next = context;
    } else {
        delete context;
        std::lock_guard<std::mutex> lock(sendMutex);
        if (sendQueue.empty()) sending = false;
        else {
            next = sendQueue.front();
            sendQueue.pop();
        }
    }
    if (next) PostNextSend(next);
}

void Client::OnSendFailed(Send_Context *context) {
    delete context;
    Send_Context *next = nullptr;
    {
        std::lock_guard<std::mutex> lock(sendMutex);
        if (sendQueue.empty()) {
            sending = false;
            return;
        }
        next = sendQueue.front();
        sendQueue.pop();
    }
    PostNextSend(next);
}

void Client::PostNextSend(Send_Context *context) {
    while (context) {
        if (postsend(context)) return;
        delete context;
        std::lock_guard<std::mutex> lock(sendMutex);
        if (sendQueue.empty()) {
            sending = false;
            return;
        }
        context = sendQueue.front();
        sendQueue.pop();
    }
}

bool Client::isConnected() const { return !closed.load(std::memory_order_relaxed); }
bool Client::loggedIn() const { return logged.load(std::memory_order_relaxed); }
void Client::Touch() { lastActivityMs.store(NowMs(), std::memory_order_relaxed); }

bool Client::AllowMessage() {
    constexpr uint32_t messagesPerMinute = 120;
    const auto now = NowMs();
    if (now - messageWindowStartMs.load(std::memory_order_relaxed) >= 60'000) {
        messageWindowStartMs.store(now, std::memory_order_relaxed);
        messagesThisWindow.store(0, std::memory_order_relaxed);
    }
    return messagesThisWindow.fetch_add(1, std::memory_order_relaxed) < messagesPerMinute;
}

Client_Manager::~Client_Manager() {
    decltype(clients) released;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex);
        usernames.clear();
        released.swap(clients);
    }
}

void Client_Manager::AddClient(std::shared_ptr<Client> client) {
    if (client) {
        std::unique_lock<std::shared_mutex> lock(clientsMutex);
        clients[client->sock] = std::move(client);
    }
}

void Client_Manager::RemoveClient(const std::shared_ptr<Client> &client) {
    if (!client) return;
    std::shared_ptr<Client> removed;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex);
        const auto it = clients.find(client->sock);
        if (it == clients.end() || it->second.get() != client.get() || it->second->connectionId != client->connectionId)
            return;
        removed = std::move(it->second);
        clients.erase(it);
        usernames.erase(removed->username);
    }
}

std::shared_ptr<Client> Client_Manager::FindByUsername(const std::string &username) const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex);
    const auto it = usernames.find(username);
    return it == usernames.end() ? nullptr : it->second.lock();
}

bool Client_Manager::TryLogin(const std::shared_ptr<Client> &client, const std::string &username) {
    if (!client) return false;
    std::unique_lock<std::shared_mutex> lock(clientsMutex);
    const auto current = clients.find(client->sock);
    if (current == clients.end() || current->second.get() != client.get() || client->logged.load()) return false;
    const auto existing = usernames.find(username);
    if (existing != usernames.end() && !existing->second.expired()) return false;
    client->username = username;
    client->logged.store(true);
    usernames[username] = client;
    return true;
}

bool Client_Manager::Contains(const std::shared_ptr<Client> &client) const {
    if (!client) return false;
    std::shared_lock<std::shared_mutex> lock(clientsMutex);
    const auto it = clients.find(client->sock);
    return it != clients.end() && it->second.get() == client.get() && it->second->connectionId == client->connectionId;
}

std::vector<std::shared_ptr<Client> > Client_Manager::Snapshot() const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex);
    std::vector<std::shared_ptr<Client> > snapshot;
    snapshot.reserve(clients.size());
    for (const auto &[_, client]: clients) snapshot.push_back(client);
    return snapshot;
}
