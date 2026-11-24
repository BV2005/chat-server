#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

constexpr uint32_t MAX_PACKET_SIZE = 64 * 1024;

enum Type {
    LOGIN,
    LOGIN_REPLY,
    PRIVATE_MESSAGE,
    GROUP_MESSAGE,
    JOIN_ROOM,
    LEAVE_ROOM,
    LOGOUT,
    HEARTBEAT,
    ERROR_MESSAGE
};

enum PacketFlags : uint32_t {
    LOGIN_SUCCEEDED = 1u << 0
};

class Client;

class PacketHeader {
public:
    uint32_t payload_length;
    Type type;
    uint32_t flags;
};

class Packet {
public:
    PacketHeader header;
    std::string sender;
    std::string recipient;
    std::string payload;

    std::vector<char> Serialize() const;
};

class PacketParser {
public:
    enum class Result { NeedMore, Parsed, Invalid };

    Result Parse(std::vector<char> &recvAccumulator, Packet &packet) const;

    static bool DecodePacket(const PacketHeader &header, const char *body,
                             std::size_t bodySize, Packet &packet);

    static bool IsValidType(Type type);

private:
    void ErasePacket(std::vector<char> &recvAccumulator,
                     const PacketHeader &header) const;
};
