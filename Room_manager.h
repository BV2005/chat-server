#pragma once
#include "Client_create_and_manage.h"
#include <memory>
#include <unordered_map>

class RoomManager {
private:
    using Members = std::unordered_map<Client *, std::weak_ptr<Client> >;
    std::unordered_map<std::string, Members> rooms;
    mutable std::shared_mutex roomsMutex;

public:
    void JoinRoom(const std::shared_ptr<Client> &, const std::string &);

    void LeaveRoom(const std::shared_ptr<Client> &, const std::string &);

    bool IsMember(const std::shared_ptr<Client> &, const std::string &) const;

    void Broadcast(const std::string &, const Packet &);

    void RemoveClient(const std::shared_ptr<Client> &);
};
