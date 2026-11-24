#include "Chat_client.h"
#include <ws2tcpip.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {
    void PrintPacket(const Packet &packet) {
        switch (packet.header.type) {
            case LOGIN_REPLY:
                std::cout << packet.payload << std::endl;
                break;
            case PRIVATE_MESSAGE:
                std::cout << "[private] " << packet.sender << ": " << packet.payload << std::endl;
                break;
            case GROUP_MESSAGE:
                std::cout << '[' << packet.recipient << "] " << packet.sender << ": " << packet.payload << std::endl;
                break;
            case ERROR_MESSAGE:
                std::cerr << "Server error: " << packet.payload << std::endl;
                break;
            default:
                std::cout << packet.payload << std::endl;
                break;
        }
    }
} // namespace

ChatClient::ChatClient() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
    }
}

ChatClient::~ChatClient() {
    Disconnect();
    WSACleanup();
}

bool ChatClient::Connect(const std::string &host, unsigned short port) {
    Disconnect();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *results = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &results) != 0) {
        return false;
    }

    for (auto *address = results; address != nullptr; address = address->ai_next) {
        const SOCKET candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == INVALID_SOCKET) {
            continue;
        }

        if (connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
            socket_ = candidate;
            freeaddrinfo(results);
            return true;
        }
        closesocket(candidate);
    }

    freeaddrinfo(results);
    return false;
}

void ChatClient::Disconnect() {
    if (socket_ != INVALID_SOCKET) {
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    username_.clear();
}

bool ChatClient::Login(const std::string &username, const std::string &password) {
    if (username.empty()) {
        return false;
    }

    Packet packet{};
    packet.header.type = LOGIN;
    packet.payload = username;
    packet.sender = password;
    if (!SendPacket(packet)) return false;

    Packet reply{};
    if (!Receive(reply) || reply.header.type != LOGIN_REPLY) return false;
    std::cout << reply.payload << std::endl;
    if ((reply.header.flags & LOGIN_SUCCEEDED) == 0) return false;
    username_ = username;
    return true;
}

bool ChatClient::SendPrivateMessage(const std::string &recipient, const std::string &message) {
    Packet packet{};
    packet.header.type = PRIVATE_MESSAGE;
    packet.recipient = recipient;
    packet.payload = message;
    return !recipient.empty() && SendPacket(packet);
}

bool ChatClient::SendGroupMessage(const std::string &room, const std::string &message) {
    Packet packet{};
    packet.header.type = GROUP_MESSAGE;
    packet.recipient = room;
    packet.payload = message;
    return !room.empty() && SendPacket(packet);
}

bool ChatClient::JoinRoom(const std::string &room) {
    Packet packet{};
    packet.header.type = JOIN_ROOM;
    packet.recipient = room;
    return !room.empty() && SendPacket(packet);
}

bool ChatClient::LeaveRoom(const std::string &room) {
    Packet packet{};
    packet.header.type = LEAVE_ROOM;
    packet.recipient = room;
    return !room.empty() && SendPacket(packet);
}

bool ChatClient::Logout() {
    Packet packet{};
    packet.header.type = LOGOUT;
    return SendPacket(packet);
}

bool ChatClient::Receive(Packet &packet) {
    PacketHeader header{};
    if (!ReceiveAll(reinterpret_cast<char *>(&header), sizeof(header))
        || header.payload_length < sizeof(uint32_t) * 2
        || header.payload_length > MAX_PACKET_SIZE
        || !PacketParser::IsValidType(header.type)) {
        return false;
    }

    std::string body(header.payload_length, '\0');
    if (!ReceiveAll(body.data(), body.size())) {
        return false;
    }

    return PacketParser::DecodePacket(header, body.data(), body.size(), packet);
}

bool ChatClient::IsConnected() const {
    return socket_ != INVALID_SOCKET;
}

bool ChatClient::SendPacket(const Packet &packet) {
    if (!IsConnected()) {
        return false;
    }

    const auto buffer = packet.Serialize();
    std::lock_guard<std::mutex> lock(sendMutex_);
    return SendAll(buffer.data(), buffer.size());
}

bool ChatClient::SendAll(const char *data, std::size_t length) {
    while (length != 0) {
        const int sent = send(socket_, data, static_cast<int>(length), 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        length -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool ChatClient::ReceiveAll(char *data, std::size_t length) {
    while (length != 0) {
        const int received = recv(socket_, data, static_cast<int>(length), 0);
        if (received <= 0) {
            return false;
        }
        data += received;
        length -= static_cast<std::size_t>(received);
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc != 1 && argc != 3) {
        std::cerr << "Usage: tcp_client [host port]\n";
        return 1;
    }

    const std::string host = argc == 3 ? argv[1] : "127.0.0.1";
    const std::string portText = argc == 3 ? argv[2] : "5000";
    unsigned short port = 0;
    try {
        const auto parsedPort = std::stoul(portText);
        if (parsedPort == 0 || parsedPort > 65535) throw std::out_of_range("port");
        port = static_cast<unsigned short>(parsedPort);
    } catch (const std::exception &) {
        std::cerr << "Port must be a number from 1 to 65535\n";
        return 1;
    }

    std::string username;
    std::string password;
    std::cout << "Username: ";
    std::getline(std::cin, username);
    std::cout << "Password: ";
    std::getline(std::cin, password);

    ChatClient client;
    if (!client.Connect(host, port) || !client.Login(username, password)) {
        std::cerr << "Unable to connect or log in\n";
        return 1;
    }

    std::atomic<bool> receiving = true;
    std::thread receiver([&] {
        Packet packet;
        while (client.Receive(packet)) {
            PrintPacket(packet);
        }
        std::cerr << "Receive loop ended: the server disconnected or sent an invalid packet.\n";
        receiving = false;
    });

    std::cout << "Commands: /msg <user> <text>, /join <room>, /leave <room>, /group <room> <text>, /quit\n";
    for (std::string line; receiving && std::getline(std::cin, line);) {
        const auto firstSpace = line.find(' ');
        const std::string command = line.substr(0, firstSpace);
        const std::string arguments = firstSpace == std::string::npos ? "" : line.substr(firstSpace + 1);

        if (command == "/quit") {
            client.Logout();
            break;
        }
        if (command == "/join") {
            client.JoinRoom(arguments);
            continue;
        }
        if (command == "/leave") {
            client.LeaveRoom(arguments);
            continue;
        }

        const auto secondSpace = arguments.find(' ');
        if ((command == "/msg" || command == "/group") && secondSpace != std::string::npos) {
            const auto target = arguments.substr(0, secondSpace);
            const auto message = arguments.substr(secondSpace + 1);
            if (command == "/msg") {
                client.SendPrivateMessage(target, message);
            } else {
                client.SendGroupMessage(target, message);
            }
        } else {
            std::cerr << "Invalid command\n";
        }
    }

    client.Disconnect();
    receiver.join();
    return 0;
}
