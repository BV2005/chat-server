#pragma once

#include "packet.h"

#include <winsock2.h>

#include <mutex>
#include <string>

class ChatClient {
public:
    ChatClient();

    ~ChatClient();

    ChatClient(const ChatClient &) = delete;

    ChatClient &operator=(const ChatClient &) = delete;

    bool Connect(const std::string &host, unsigned short port);

    void Disconnect();

    bool Login(const std::string &username, const std::string &password);

    bool SendPrivateMessage(const std::string &recipient, const std::string &message);

    bool SendGroupMessage(const std::string &room, const std::string &message);

    bool JoinRoom(const std::string &room);

    bool LeaveRoom(const std::string &room);

    bool Logout();

    bool Receive(Packet &packet);

    bool IsConnected() const;

private:
    bool SendPacket(const Packet &packet);

    bool SendAll(const char *data, std::size_t length);

    bool ReceiveAll(char *data, std::size_t length);

    SOCKET socket_ = INVALID_SOCKET;
    std::string username_;
    std::mutex sendMutex_;
};
