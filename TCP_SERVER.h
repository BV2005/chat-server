#pragma once

#include <winsock2.h>

#include <memory>
#include <vector>

class Client;

enum OpType {
    OP_ACCEPT,
    OP_RECV,
    OP_SEND
};

struct Send_Context {
    OVERLAPPED ov{};
    SOCKET client = INVALID_SOCKET;
    OpType op = OP_SEND;
    std::weak_ptr<Client> owner;
    std::vector<char> buffer;
    WSABUF wsabuf{};
};

bool postsend(Send_Context *context);

void DisconnectClient(const std::shared_ptr<Client> &client);
