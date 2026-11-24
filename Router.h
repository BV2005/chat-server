#pragma once
#include "Client_create_and_manage.h"
#include "Room_manager.h"
#include "TCP_SERVER.h"
#include "packet.h"
class RoomManager;

class Router {
public:
    Router(Client_Manager *clientManager, RoomManager *roomManager)
        : clientManager(clientManager), roomManager(roomManager) {
    }

    void Dispatch(const std::shared_ptr<Client> &client, const Packet &packet);

private:
    Client_Manager *clientManager;
    RoomManager *roomManager;

    void HandleLogin(const std::shared_ptr<Client> &client, const Packet &packet);

    void HandleLoginReply(const std::shared_ptr<Client> &client, const Packet &packet);

    void HandlePrivateMessage(const std::shared_ptr<Client> &client, const Packet &packet);

    void HandleGroupMessage(const std::shared_ptr<Client> &client, const Packet &packet);

    void HandleJoinRoom(const std::shared_ptr<Client> &client, const Packet &packet);

    void HandleLeaveRoom(const std::shared_ptr<Client> &client, const Packet &packet);

    void HandleLogout(const std::shared_ptr<Client> &client);

    void HandleHeartbeat(const std::shared_ptr<Client> &client);
};
