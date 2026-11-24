#include "Room_manager.h"
#include <vector>

void RoomManager::JoinRoom(const std::shared_ptr<Client> &client, const std::string &name) {
    if (!client || name.empty()) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(roomsMutex);
    constexpr std::size_t maxRoomsPerClient = 20;
    std::size_t joined = 0;
    for (const auto &[_, members]: rooms) {
        if (members.contains(client.get())) ++joined;
    }
    const auto existing = rooms.find(name);
    if ((existing == rooms.end() || !existing->second.contains(client.get())) && joined >= maxRoomsPerClient) return;
    rooms[name][client.get()] = client;
}

void RoomManager::LeaveRoom(const std::shared_ptr<Client> &client, const std::string &name) {
    if (!client || name.empty()) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(roomsMutex);
    const auto room = rooms.find(name);
    if (room == rooms.end()) {
        return;
    }

    room->second.erase(client.get());
    if (room->second.empty()) {
        rooms.erase(room);
    }
}

bool RoomManager::IsMember(const std::shared_ptr<Client> &client, const std::string &name) const {
    if (!client || name.empty()) return false;
    std::shared_lock<std::shared_mutex> lock(roomsMutex);
    const auto room = rooms.find(name);
    return room != rooms.end() && room->second.contains(client.get());
}

void RoomManager::Broadcast(const std::string &name, const Packet &packet) {
    std::vector<std::shared_ptr<Client> > members;
    {
        std::unique_lock<std::shared_mutex> lock(roomsMutex);
        const auto room = rooms.find(name);
        if (room == rooms.end()) {
            return;
        }

        for (auto member = room->second.begin(); member != room->second.end();) {
            if (auto client = member->second.lock()) {
                members.push_back(std::move(client));
                ++member;
            } else {
                member = room->second.erase(member);
            }
        }

        if (room->second.empty()) {
            rooms.erase(room);
        }
    }

    for (const auto &client: members) {
        client->EnqueueSend(packet);
    }
}

void RoomManager::RemoveClient(const std::shared_ptr<Client> &client) {
    if (!client) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(roomsMutex);
    for (auto it = rooms.begin(); it != rooms.end();) {
        it->second.erase(client.get());
        if (it->second.empty()) {
            it = rooms.erase(it);
        } else {
            ++it;
        }
    }
}
