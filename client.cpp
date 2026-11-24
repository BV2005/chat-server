#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

void error(const char *msg)
{
    printf("%s\n", msg);
    printf("Error Code: %d\n", WSAGetLastError());
    WSACleanup();
    exit(1);
}

int main(int argc, char *argv[])
{
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup Failed\n");
        return 1;
    }

    if (argc < 3)
    {
        printf("Usage: client <hostname> <port>\n");
        WSACleanup();
        return 1;
    }

    SOCKET sock;

    struct sockaddr_in servaddr;
    struct hostent *server;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock == INVALID_SOCKET)
        error("Socket creation failed");

    server = gethostbyname(argv[1]);

    if (server == NULL)
    {
        closesocket(sock);
        error("Host not found");
    }

    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    memcpy(&servaddr.sin_addr.s_addr,
           server->h_addr,
           server->h_length);

    servaddr.sin_port = htons(atoi(argv[2]));

    if (connect(sock,
                (struct sockaddr *)&servaddr,
                sizeof(servaddr)) == SOCKET_ERROR)
    {
        closesocket(sock);
        error("Connection failed");
    }

    printf("Connected to server.\n");

    char msg[256];

    printf("Enter message: ");
    fgets(msg, sizeof(msg), stdin);

    int n = send(sock, msg, strlen(msg), 0);

    if (n == SOCKET_ERROR)
    {
        closesocket(sock);
        error("Send failed");
    }

    char buffer[256];

    n = recv(sock, buffer, sizeof(buffer) - 1, 0);

    if (n == SOCKET_ERROR)
    {
        closesocket(sock);
        error("Receive failed");
    }

    buffer[n] = '\0';

    printf("Server: %s\n", buffer);

    closesocket(sock);
    WSACleanup();

    return 0;
}
