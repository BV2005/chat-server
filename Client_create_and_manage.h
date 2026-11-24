#pragma once
#include "TCP_SERVER.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Packet;

class Client : public std::enable_shared_from_this<Client> {
public:
    SOCKET sock;
    const uint64_t connectionId;
    std::string username;
    std::atomic_bool logged{false};
    std::atomic_bool closed{false};
    bool sending = false;
    std::queue<Send_Context *> sendQueue;
    // Exactly one receive is outstanding per Client; only its completion worker mutates this buffer.
    std::vector<char> recvAccumulator;
    std::mutex sendMutex;
    std::atomic<int64_t> lastActivityMs{0};

    Client(SOCKET s, uint64_t id);

    ~Client();

    void EnqueueSend(const Packet &packet);

    void OnSendCompleted(Send_Context *context, DWORD bytesTransferred);

    void OnSendFailed(Send_Context *context);

    bool isConnected() const;

    bool loggedIn() const;

    void Touch();

    bool AllowMessage();

private:
    std::atomic_uint32_t messagesThisWindow{0};
    std::atomic<int64_t> messageWindowStartMs{0};

    void PostNextSend(Send_Context *context);
};

class Client_Manager {
public:
    ~Client_Manager();

    void AddClient(std::shared_ptr<Client> client);

    void RemoveClient(const std::shared_ptr<Client> &client);

    std::shared_ptr<Client> FindByUsername(const std::string &username) const;

    bool TryLogin(const std::shared_ptr<Client> &client, const std::string &username);

    bool Contains(const std::shared_ptr<Client> &client) const;

    std::vector<std::shared_ptr<Client> > Snapshot() const;

private:
    mutable std::shared_mutex clientsMutex;
    std::unordered_map<SOCKET, std::shared_ptr<Client> > clients;
    std::unordered_map<std::string, std::weak_ptr<Client> > usernames;
};
