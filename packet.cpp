#include "packet.h"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

std::vector<char> Packet::Serialize() const {
    PacketHeader serializedHeader = header;
    if (sender.size() > UINT32_MAX || recipient.size() > UINT32_MAX || payload.size() > UINT32_MAX) {
        throw std::length_error("packet field exceeds protocol limit");
    }
    const auto senderLength = static_cast<uint32_t>(sender.size());
    const auto recipientLength = static_cast<uint32_t>(recipient.size());
    const auto payloadLength = static_cast<uint32_t>(payload.size());
    const uint64_t bodyLength = sizeof(senderLength) + sizeof(recipientLength)
                                + static_cast<uint64_t>(senderLength) + recipientLength + payloadLength;
    if (bodyLength > MAX_PACKET_SIZE) {
        throw std::length_error("packet exceeds MAX_PACKET_SIZE");
    }
    serializedHeader.payload_length = static_cast<uint32_t>(bodyLength);

    std::vector<char> buffer(sizeof(PacketHeader) + serializedHeader.payload_length);
    std::memcpy(buffer.data(), &serializedHeader, sizeof(PacketHeader));
    char *body = buffer.data() + sizeof(PacketHeader);
    std::memcpy(body, &senderLength, sizeof(senderLength));
    body += sizeof(senderLength);
    std::memcpy(body, &recipientLength, sizeof(recipientLength));
    body += sizeof(recipientLength);
    std::memcpy(body, sender.data(), senderLength);
    body += senderLength;
    std::memcpy(body, recipient.data(), recipientLength);
    body += recipientLength;
    std::memcpy(body, payload.data(), payloadLength);

    return buffer;
}

bool PacketParser::IsValidType(Type type) {
    return type >= LOGIN && type <= ERROR_MESSAGE;
}

PacketParser::Result PacketParser::Parse(std::vector<char> &recvAccumulator, Packet &packet) const {
    if (recvAccumulator.size() < sizeof(PacketHeader)) {
        return Result::NeedMore;
    }

    PacketHeader header{};
    std::memcpy(&header, recvAccumulator.data(), sizeof(PacketHeader));
    if (header.payload_length < sizeof(uint32_t) * 2
        || header.payload_length > MAX_PACKET_SIZE
        || !IsValidType(header.type)) {
        return Result::Invalid;
    }
    if (recvAccumulator.size() < sizeof(PacketHeader) + header.payload_length) {
        return Result::NeedMore;
    }

    if (!DecodePacket(header, recvAccumulator.data() + sizeof(PacketHeader), header.payload_length, packet)) {
        return Result::Invalid;
    }
    ErasePacket(recvAccumulator, header);
    return Result::Parsed;
}

bool PacketParser::DecodePacket(const PacketHeader &header, const char *body,
                                std::size_t bodySize, Packet &packet) {
    if (!IsValidType(header.type) || header.payload_length < sizeof(uint32_t) * 2
        || header.payload_length != bodySize) {
        return false;
    }
    uint32_t senderLength{};
    uint32_t recipientLength{};
    std::memcpy(&senderLength, body, sizeof(senderLength));
    std::memcpy(&recipientLength, body + sizeof(senderLength), sizeof(recipientLength));

    const uint32_t metadataLength = sizeof(senderLength) + sizeof(recipientLength);
    if (senderLength > header.payload_length - metadataLength
        || recipientLength > header.payload_length - metadataLength - senderLength) {
        return false;
    }

    body += metadataLength;
    packet.header = header;
    packet.sender.assign(body, senderLength);
    body += senderLength;
    packet.recipient.assign(body, recipientLength);
    body += recipientLength;
    packet.payload.assign(body, header.payload_length - metadataLength - senderLength - recipientLength);
    return true;
}

void PacketParser::ErasePacket(std::vector<char> &recvAccumulator,
                               const PacketHeader &header) const {
    recvAccumulator.erase(recvAccumulator.begin(),
                          recvAccumulator.begin() + sizeof(PacketHeader) + header.payload_length);
    if (recvAccumulator.capacity() > MAX_PACKET_SIZE && recvAccumulator.size() < MAX_PACKET_SIZE / 4) {
        std::vector<char>(recvAccumulator).swap(recvAccumulator);
    }
}
