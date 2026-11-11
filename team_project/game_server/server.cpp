#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <vector>
#include <conio.h>

#include "../network_defaults.h"

namespace {
// 종료 시 소켓을 먼저 닫고 Winsock을 정리합니다.
struct ServerResources {
    bool winsockStarted = false;
    SOCKET listenSocket = INVALID_SOCKET;
    std::vector<SOCKET> clients;

    ~ServerResources() {
        for (SOCKET client : clients) closesocket(client);
        if (listenSocket != INVALID_SOCKET) {
            closesocket(listenSocket);
        }
        if (winsockStarted) {
            WSACleanup();
        }
    }
};
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    ServerResources server;
    WSADATA wsaData{};
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        std::cerr << u8"[서버] Winsock 초기화 실패. 오류 코드: " << startupResult << '\n';
        return 1;
    }
    server.winsockStarted = true;
    if (wsaData.wVersion != MAKEWORD(2, 2)) {
        std::cerr << u8"[서버] Winsock 2.2가 필요합니다.\n";
        return 1;
    }

    server.listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server.listenSocket == INVALID_SOCKET) {
        std::cerr << u8"[서버] 소켓 생성 실패. 오류 코드: " << WSAGetLastError() << '\n';
        return 1;
    }

    // 다른 프로세스가 서버 주소와 포트를 함께 사용하지 못하게 합니다.
    const BOOL exclusiveAddress = TRUE;
    if (setsockopt(server.listenSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusiveAddress), sizeof(exclusiveAddress)) == SOCKET_ERROR) {
        std::cerr << u8"[서버] 소켓 옵션 설정 실패. 오류 코드: " << WSAGetLastError() << '\n';
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(kDefaultHostPort);

    if (bind(server.listenSocket, reinterpret_cast<const sockaddr*>(&serverAddress),
        sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << u8"[서버] 주소와 포트 바인딩 실패. 오류 코드: " << WSAGetLastError() << '\n';
        return 1;
    }

    // 접속 요청을 대기열에 넣고 아래 반복문에서 수락합니다.
    if (listen(server.listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << u8"[서버] 접속 대기 시작 실패. 오류 코드: " << WSAGetLastError() << '\n';
        return 1;
    }

    std::cout << u8"[서버] TCP 접속 대기 중: 0.0.0.0:" << kDefaultHostPort << '\n'
        << u8"Enter 키를 누르면 서버가 종료됩니다." << std::endl;
    while (true) {
        if (_kbhit() && _getch() == '\r') break;
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(server.listenSocket, &readable);
        for (SOCKET client : server.clients) FD_SET(client, &readable);
        timeval timeout{0, 100000};
        if (select(0, &readable, nullptr, nullptr, &timeout) == SOCKET_ERROR) {
            std::cerr << u8"[서버] 소켓 상태 확인 실패. 오류 코드: " << WSAGetLastError() << std::endl;
            return 1;
        }
        for (size_t i = 0; i < server.clients.size();) {
            SOCKET client = server.clients[i];
            if (FD_ISSET(client, &readable)) {
                char buffer[256];
                const int received = recv(client, buffer, sizeof(buffer), 0);
                if (received <= 0) {
                    closesocket(client);
                    server.clients.erase(server.clients.begin() + i);
                    std::cout << u8"[서버] 클라이언트 연결이 종료됐습니다. 현재 연결 수: "
                        << server.clients.size() << std::endl;
                    continue;
                }
            }
            ++i;
        }
        if (FD_ISSET(server.listenSocket, &readable)) {
            sockaddr_in peer{};
            int size = sizeof(peer);
            SOCKET client = accept(server.listenSocket, reinterpret_cast<sockaddr*>(&peer), &size);
            if (client == INVALID_SOCKET) {
                std::cerr << u8"[서버] 연결 수락 실패. 오류 코드: " << WSAGetLastError() << std::endl;
                continue;
            }
            if (server.clients.size() >= FD_SETSIZE - 1) {
                closesocket(client);
                std::cerr << u8"[서버] 최대 연결 수에 도달했습니다." << std::endl;
                continue;
            }
            server.clients.push_back(client);
            char ip[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
            std::cout << u8"[서버] 클라이언트가 연결됐습니다: " << ip << ':' << ntohs(peer.sin_port)
                << u8" | 현재 연결 수: " << server.clients.size() << std::endl;
        }
    }
    return 0;
}
