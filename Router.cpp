#include "Router.h"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>

namespace {
    const char *PacketTypeName(Type type) {
        switch (type) {
            case LOGIN: return "LOGIN";
            case LOGIN_REPLY: return "LOGIN_REPLY";
            case PRIVATE_MESSAGE: return "PRIVATE_MESSAGE";
            case GROUP_MESSAGE: return "GROUP_MESSAGE";
            case JOIN_ROOM: return "JOIN_ROOM";
            case LEAVE_ROOM: return "LEAVE_ROOM";
            case LOGOUT: return "LOGOUT";
            case HEARTBEAT: return "HEARTBEAT";
            default: return "UNKNOWN";
        }
    }
} // namespace

namespace {
    bool IsValidUsername(const std::string &username) {
        if (username.empty() || username.size() > 32) return false;
        return std::all_of(username.begin(), username.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        });
    }

    bool ConstantTimeEquals(const std::string &left, const std::string &right) {
        const auto maxLength = std::max(left.size(), right.size());
        unsigned char diff = static_cast<unsigned char>(left.size() ^ right.size());
        for (size_t i = 0; i < maxLength; ++i) {
            const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
            const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
            diff |= a ^ b;
        }
        return diff == 0;
    }

    constexpr const char *CHAT_USERS_FILE = "chat_users.txt";
    std::mutex credentialsMutex;

    enum class LoginResult { Authenticated, Registered, Rejected };

    LoginResult AuthenticateOrRegister(const std::string &username, const std::string &password) {
        if (password.empty() || password.size() > 128
            || password.find_first_of("\r\n\t") != std::string::npos) {
            return LoginResult::Rejected;
        }

        std::lock_guard<std::mutex> lock(credentialsMutex);
        std::ifstream input(CHAT_USERS_FILE);
        std::string line;
        while (std::getline(input, line)) {
            const auto separator = line.find('\t');
            if (separator == std::string::npos || line.substr(0, separator) != username) continue;
            return ConstantTimeEquals(line.substr(separator + 1), password)
                       ? LoginResult::Authenticated
                       : LoginResult::Rejected;
        }

        std::ofstream output(CHAT_USERS_FILE, std::ios::app);
        if (!output) return LoginResult::Rejected;
        output << username << '\t' << password << '\n';
        return output ? LoginResult::Registered : LoginResult::Rejected;
    }

    void SendError(const std::shared_ptr<Client> &client, const std::string &message) {
        if (!client) return;
        Packet error{};
        error.header.type = ERROR_MESSAGE;
        error.sender = "server";
        error.payload = message;
        client->EnqueueSend(error);
    }
}

void Router::Dispatch(const std::shared_ptr<Client> &client, const Packet &packet) {
    const std::string username = client != nullptr && client->loggedIn()
                                     ? client->username
                                     : "<not logged in>";
    std::cout << "Received " << PacketTypeName(packet.header.type)
            << " from " << username;
    if (!packet.recipient.empty()) {
        std::cout << " to " << packet.recipient;
    }
    if (!packet.payload.empty()) {
        std::cout << ": " << packet.payload;
    }
    std::cout << std::endl;

    switch (packet.header.type) {
        case LOGIN:
            HandleLogin(client, packet);
            break;

        case LOGIN_REPLY:
            HandleLoginReply(client, packet);
            break;

        case PRIVATE_MESSAGE:
            HandlePrivateMessage(client, packet);
            break;

        case GROUP_MESSAGE:
            HandleGroupMessage(client, packet);
            break;

        case JOIN_ROOM:
            HandleJoinRoom(client, packet);
            break;

        case LEAVE_ROOM:
            HandleLeaveRoom(client, packet);
            break;

        case LOGOUT:
            HandleLogout(client);
            break;

        case HEARTBEAT:
            HandleHeartbeat(client);
            break;

        default:
            // Invalid packet type
            break;
    }
}

void Router::HandleLogin(const std::shared_ptr<Client> &client, const Packet &packet) {
    if (client == nullptr || !IsValidUsername(packet.payload)) {
        SendError(client, "Invalid username");
        return;
    }

    Packet reply;
    reply.sender = "server";
    reply.recipient = packet.payload;
    const auto loginResult = AuthenticateOrRegister(packet.payload, packet.sender);
    if (loginResult == LoginResult::Rejected) {
        reply.header.type = LOGIN_REPLY;
        reply.header.flags = 0;
        reply.payload = "Invalid credentials or unable to save account";
    } else if (clientManager->TryLogin(client, packet.payload)) {
        reply.header.type = LOGIN_REPLY;
        reply.header.flags = LOGIN_SUCCEEDED;
        reply.payload = loginResult == LoginResult::Registered
                            ? "Account created and login successful"
                            : "Login successful";
    } else {
        reply.header.type = LOGIN_REPLY;
        reply.header.flags = 0;
        reply.payload = "Username already exists";
    }
    client->EnqueueSend(reply);
}

void Router::HandleLoginReply(const std::shared_ptr<Client> &client, const Packet &packet) {
    // Client-side only
}

void Router::HandlePrivateMessage(const std::shared_ptr<Client> &client, const Packet &packet) {
    if (client == nullptr || !client->loggedIn() || packet.recipient.empty()) {
        SendError(client, "Login and recipient are required");
        return;
    }
    if (!client->AllowMessage()) {
        SendError(client, "Message rate limit exceeded");
        return;
    }

    const auto recipient = clientManager->FindByUsername(packet.recipient);
    if (recipient == nullptr || !recipient->loggedIn()) {
        SendError(client, "Recipient is unavailable");
        return;
    }

    Packet message = packet;
    message.sender = client->username; // Never trust a sender supplied by the client.
    message.recipient = recipient->username;
    recipient->EnqueueSend(message);
}

void Router::HandleGroupMessage(const std::shared_ptr<Client> &client, const Packet &packet) {
    if (client == nullptr || !client->loggedIn() || packet.recipient.empty()) {
        SendError(client, "Login and room are required");
        return;
    }
    if (!client->AllowMessage()) {
        SendError(client, "Message rate limit exceeded");
        return;
    }
    if (!roomManager->IsMember(client, packet.recipient)) {
        SendError(client, "Join the room before posting");
        return;
    }

    Packet message = packet;
    message.sender = client->username;
    roomManager->Broadcast(message.recipient, message);
}

void Router::HandleJoinRoom(const std::shared_ptr<Client> &client, const Packet &packet) {
    if (client == nullptr || !client->loggedIn() || packet.recipient.empty()) {
        SendError(client, "Login and room are required");
        return;
    }

    roomManager->JoinRoom(client, packet.recipient);
}

void Router::HandleLeaveRoom(const std::shared_ptr<Client> &client, const Packet &packet) {
    if (client == nullptr || !client->loggedIn() || packet.recipient.empty()) {
        SendError(client, "Login and room are required");
        return;
    }

    roomManager->LeaveRoom(client, packet.recipient);
}

void Router::HandleLogout(const std::shared_ptr<Client> &client) {
    if (client == nullptr) {
        return;
    }

    client->logged.store(false);
    DisconnectClient(client);
}

void Router::HandleHeartbeat(const std::shared_ptr<Client> &client) {
    if (client) client->Touch();
}
