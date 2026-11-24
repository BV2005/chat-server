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

    if (argc < 2)
    {
        printf("Usage: server <port>\n");
        WSACleanup();
        return 1;
    }

    SOCKET sockfd, newsockfd;

    struct sockaddr_in servaddr, cliaddr;

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sockfd == INVALID_SOCKET)
        error("Socket creation failed");

    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(atoi(argv[1]));

    if (bind(sockfd,
             (struct sockaddr *)&servaddr,
             sizeof(servaddr)) == SOCKET_ERROR)
    {
        closesocket(sockfd);
        error("Bind failed");
    }

    if (listen(sockfd, 5) == SOCKET_ERROR)
    {
        closesocket(sockfd);
        error("Listen failed");
    }

    printf("Waiting for client...\n");

    int client_length = sizeof(cliaddr);

    newsockfd = accept(sockfd,
                       (struct sockaddr *)&cliaddr,
                       &client_length);

    if (newsockfd == INVALID_SOCKET)
    {
        closesocket(sockfd);
        error("Accept failed");
    }

    printf("Client connected!\n");

    char msg[256];

    int n = recv(newsockfd, msg, sizeof(msg) - 1, 0);

    if (n == SOCKET_ERROR)
    {
        closesocket(newsockfd);
        closesocket(sockfd);
        error("Receive failed");
    }

    msg[n] = '\0';

    printf("Client: %s\n", msg);

    send(newsockfd, "Message received!", 17, 0);

    closesocket(newsockfd);
    closesocket(sockfd);

    WSACleanup();

    return 0;
}
