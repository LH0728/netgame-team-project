#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <utility>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <conio.h>
#include "../team_project/netframe.h"
#include "../team_project/host_server.h"

namespace {
constexpr unsigned short kServerPort = Protocol::PORT;
constexpr std::size_t kMaxClients = 1;

constexpr std::uint32_t kHostPlayerId = Protocol::HOST_PLAYER_ID;
constexpr std::chrono::seconds kJoinRequestTimeout{5};

struct ClientSession {
    SOCKET socket;
    std::uint32_t playerId;
    std::uint32_t sessionId;
    std::vector<char> receiveBuffer;
    SendState sendState;
    std::uint32_t nextSendSequence = 1;
    bool joinValidated = false;
    std::chrono::steady_clock::time_point connectedAt = std::chrono::steady_clock::now();
};

struct ServerResources {
    bool winsockStarted = false;
    SOCKET listenSocket = INVALID_SOCKET;
    std::vector<ClientSession> clients;
    std::uint32_t nextSessionId = 1;
    std::uint32_t nextPlayerId = Protocol::HOST_PLAYER_ID + 1;

    ~ServerResources() {
        for (const ClientSession& client : clients) closesocket(client.socket);
        if (listenSocket != INVALID_SOCKET) {
            closesocket(listenSocket);
        }
        if (winsockStarted) {
            WSACleanup();
        }
    }
};

void writeNetworkU16(unsigned char* bytes, std::uint16_t value) {
    bytes[0] = static_cast<unsigned char>(value >> 8);
    bytes[1] = static_cast<unsigned char>(value);
}

void writeNetworkU32(unsigned char* bytes, std::uint32_t value) {
    writeNetworkU16(bytes, static_cast<std::uint16_t>(value >> 16));
    writeNetworkU16(bytes + 2, static_cast<std::uint16_t>(value));
}

bool enqueueFrame(ClientSession& client, Protocol::MessageType type, const std::vector<char>& body) {
    std::vector<char> frame;
    if (!encodeFrame(type, client.nextSendSequence, body, frame)) return false;
    client.sendState.sendQueue.push_back(std::move(frame));
    if (++client.nextSendSequence == 0) client.nextSendSequence = 1;
    return true;
}

bool consumeJoinRequest(ClientSession& client, const char* bytes, std::size_t count) {
    // This handler owns admission only. Gameplay dispatch is the next host step.
    if (client.joinValidated) {
        std::cerr << "[Server] Post-JOIN message handling is not implemented yet." << std::endl;
        return false;
    }
    // A JOIN request has no body. Keep the original admission buffer limit.
    if (count > Protocol::HEADER_SIZE - client.receiveBuffer.size()) {
        std::cerr << "[Server] Join rejected: request must contain one empty-body frame." << std::endl;
        return false;
    }
    client.receiveBuffer.insert(client.receiveBuffer.end(), bytes, bytes + count);
    std::vector<DecodedFrame> frames;
    if (!decodeFrames(client.receiveBuffer, frames)) {
        std::cerr << "[Server] Join rejected: invalid protocol header." << std::endl;
        return false;
    }
    if (frames.empty()) {
        // decodeFrames also returns true for a partial header/body.
        if (client.receiveBuffer.size() == Protocol::HEADER_SIZE) {
            std::cerr << "[Server] Join rejected: request body must be empty." << std::endl;
            return false;
        }
        return true;
    }
    if (frames.size() != 1 || !client.receiveBuffer.empty() ||
        frames.front().header.type != Protocol::MessageType::SESSION_JOIN_REQUEST ||
        frames.front().header.sequence != 1 ||
        frames.front().body.size() != Protocol::SESSION_JOIN_REQUEST_BODY_SIZE) {
        std::cerr << "[Server] Join rejected: expected SESSION_JOIN_REQUEST, empty body, sequence 1." << std::endl;
        return false;
    }
    std::vector<char> body(Protocol::SESSION_JOIN_ACCEPT_BODY_SIZE);
    writeNetworkU32(reinterpret_cast<unsigned char*>(body.data()), client.playerId);
    writeNetworkU32(reinterpret_cast<unsigned char*>(body.data() + 4), client.sessionId);
    if (!enqueueFrame(client, Protocol::MessageType::SESSION_JOIN_ACCEPT, body)) return false;
    client.joinValidated = true;

    std::cout << "[Server] Join request validated. Player ID: " << client.playerId
        << " | Session ID: " << client.sessionId << std::endl;
    return true;
}

void acceptClient(ServerResources& server) {
    sockaddr_in peer{};
    int size = sizeof(peer);
    SOCKET client = accept(server.listenSocket, reinterpret_cast<sockaddr*>(&peer), &size);
    if (client == INVALID_SOCKET) {
        std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
        return;
    }
    if (server.clients.size() >= kMaxClients) {
        closesocket(client);
        std::cerr << "[Server] Room full. Only one remote client can join." << std::endl;
        return;
    }
  
    if (server.nextSessionId == 0 || server.nextPlayerId <= Protocol::HOST_PLAYER_ID) {
        closesocket(client);
        std::cerr << "[Server] Player/session ID limit reached." << std::endl;
        return;
    }
    u_long nonblocking = 1;
    if (ioctlsocket(client, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        std::cerr << "[Server] Nonblocking socket failed: " << WSAGetLastError() << std::endl;
        closesocket(client);
        return;
    }
    const ClientSession newClient{client, server.nextPlayerId++, server.nextSessionId++};
    server.clients.push_back(newClient);
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    std::cout << "[Server] Client connected: " << ip << ':' << ntohs(peer.sin_port)
        << " | Player ID: " << newClient.playerId
        << " | Session ID: " << newClient.sessionId
        << " | Connected: " << server.clients.size() << std::endl;
}

bool startServer(ServerResources& server) {
    WSADATA wsaData{};
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        std::cerr << "WSAStartup failed: " << startupResult << '\n';
        return false;
    }
    server.winsockStarted = true;
    if (wsaData.wVersion != MAKEWORD(2, 2)) {
        std::cerr << "Winsock 2.2 is required.\n";
        return false;
    }

    server.listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server.listenSocket == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << '\n';
        return false;
    }

    // Prevent another process from sharing this server's address and port.
    const BOOL exclusiveAddress = TRUE;
    if (setsockopt(server.listenSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusiveAddress), sizeof(exclusiveAddress)) == SOCKET_ERROR) {
        std::cerr << "setsockopt failed: " << WSAGetLastError() << '\n';
        return false;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(kServerPort);

    if (bind(server.listenSocket, reinterpret_cast<const sockaddr*>(&serverAddress),
        sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << '\n';
        return false;
    }

    u_long nonblocking = 1;
    if (ioctlsocket(server.listenSocket, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        std::cerr << "Listening socket setup failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Queue incoming requests, then accept them in the loop below.
    if (listen(server.listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << '\n';
        return false;
    }

    std::cout << "TCP server listening on 0.0.0.0:" << kServerPort << '\n'
        << "Host player ID: " << kHostPlayerId << std::endl;
    return true;
}

bool pollServer(ServerResources& server, long waitMicros) {
    fd_set readable, writable;
    FD_ZERO(&readable);
    FD_ZERO(&writable);
    FD_SET(server.listenSocket, &readable);
    for (const ClientSession& client : server.clients) {
        FD_SET(client.socket, &readable);
        if (!client.sendState.sendQueue.empty()) {
            FD_SET(client.socket, &writable);
        }
    }
    timeval timeout{0, waitMicros};
    if (select(0, &readable, &writable, nullptr, &timeout) == SOCKET_ERROR) {
        std::cerr << "select failed: " << WSAGetLastError() << std::endl;
        return false;
    }
    for (size_t i = 0; i < server.clients.size();) {
        ClientSession& client = server.clients[i];
        bool keepConnection = true;
        if (!client.joinValidated &&
            std::chrono::steady_clock::now() - client.connectedAt >= kJoinRequestTimeout) {
            std::cerr << "[Server] Join request timed out. Player ID: " << client.playerId
                << " | Session ID: " << client.sessionId << std::endl;
            keepConnection = false;
        }
        if (keepConnection && FD_ISSET(client.socket, &readable)) {
            char buffer[256];
            const int received = recv(client.socket, buffer, sizeof(buffer), 0);
            if (received > 0)
                keepConnection = consumeJoinRequest(client, buffer, static_cast<std::size_t>(received));
            else if (received == 0)
                keepConnection = false;
            else if (WSAGetLastError() != WSAEWOULDBLOCK)
                keepConnection = false;
        }
        if (keepConnection && FD_ISSET(client.socket, &writable))
            keepConnection = flushSendQueue(client.socket, client.sendState);
        if (!keepConnection) {
            const std::uint32_t playerId = client.playerId;
            const std::uint32_t sessionId = client.sessionId;
            closesocket(client.socket);
            server.clients.erase(server.clients.begin() + i);
            std::cout << "[Server] Client disconnected. Player ID: " << playerId
                << " | Session ID: " << sessionId << " | Connected: "
                << server.clients.size() << std::endl;
            continue;
        }
        ++i;
    }
    if (FD_ISSET(server.listenSocket, &readable)) {
        acceptClient(server);
    }
    return true;
}

#ifdef NETGAME_EMBEDDED_SERVER
ServerResources hostServer;

#endif
}

#ifdef NETGAME_EMBEDDED_SERVER
bool startHostServer() {
    return startServer(hostServer);
}

bool pollHostServer() {
    return pollServer(hostServer, 0);
}

#else
int main() {
    ServerResources server;
    if (!startServer(server)) return 1;
    std::cout << "Press Enter to stop the server." << std::endl;
    while (!_kbhit() || _getch() != '\r') {
        if (!pollServer(server, 100000)) return 1;
    }
    return 0;
}
#endif
