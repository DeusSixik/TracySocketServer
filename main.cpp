#include "tracy/Tracy.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>

#include "RemoteZoneProfiler.h"
#include "tracy/TracyC.h"

int main() {
    ZoneScopedN("RemoteZoneServer");

    WSADATA wsaData;  WSAStartup(MAKEWORD(2,2), &wsaData);
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(9001);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 1);

    std::cout << "[C++] Server listening on port 9001...\n";
    SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
    std::cout << "[C++] Java connected!\n";

    RemoteZoneProfiler profiler;

    while (true) {
        unsigned char sizeBuf[4];
        int r = recv(clientSocket, (char*)sizeBuf, 4, MSG_WAITALL);
        if (r <= 0) break;

        const int length = (sizeBuf[0]<<24) | (sizeBuf[1]<<16) | (sizeBuf[2]<<8) | sizeBuf[3];
        std::vector<unsigned char> data(length);

        {
            int got = recv(clientSocket, (char*)data.data(), length, MSG_WAITALL);
            if (got != length) break;
        }

        profiler.handlePacket(data);
    }

    profiler.shutdown();

    closesocket(clientSocket);
    closesocket(serverSocket);
    WSACleanup();
}
