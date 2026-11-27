#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <utility>
#include <cmath>
#include <limits>
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
constexpr std::size_t kMaxQueuedFrames = 256;

constexpr std::uint32_t kHostPlayerId = Protocol::HOST_PLAYER_ID;
constexpr std::chrono::seconds kJoinRequestTimeout{5};

struct ClientSession {
    SOCKET socket;
    std::uint32_t playerId;
    std::uint32_t sessionId;
    std::vector<char> receiveBuffer;
    SendState sendState;
    std::uint32_t nextSendSequence = 1;
    std::uint32_t nextEventId = 1;
    std::uint32_t nextInputSequence = 3; // JOIN 요청은 1, CLIENT_READY는 2를 사용한다.
    Protocol::PlayerInput latestInput{};
    bool hasInput = false;
    bool joinValidated = false;
    bool initialWorldQueued = false;
    bool matchStartQueued = false;
    bool sendFailed = false;
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

std::uint32_t readNetworkU32(const char* bytes) {
    std::uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return ntohl(value);
}

float readNetworkFloat(const char* bytes) {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559,
        "Player input requires IEEE 754 binary32");
    const auto bits = readNetworkU32(bytes);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool hasQueuedFrame(const ClientSession& client, Protocol::MessageType messageType) {
    const auto matches = [messageType](const std::vector<char>& frame) {
        std::uint16_t type;
        std::memcpy(&type, frame.data() + Protocol::HeaderOffset::TYPE, sizeof(type));
        return ntohs(type) == static_cast<std::uint16_t>(messageType);
    };
    return std::any_of(client.sendState.sendQueue.begin(), client.sendState.sendQueue.end(), matches);
}

bool enqueueFrame(ClientSession& client, Protocol::MessageType type, const std::vector<char>& body) {
    if (client.sendState.sendQueue.size() >= kMaxQueuedFrames) return false;
    std::vector<char> frame;
    if (!encodeFrame(type, client.nextSendSequence, body, frame)) return false;
    client.sendState.sendQueue.push_back(std::move(frame));
    if (++client.nextSendSequence == 0) client.nextSendSequence = 1;
    return true;
}

bool consumeJoinRequest(ClientSession& client, const char* bytes, std::size_t count) {
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

bool consumeClientReady(ClientSession& client, const char* bytes, std::size_t count, std::uint32_t nextWorldTick) {
    if (!client.initialWorldQueued || client.matchStartQueued) return false;
    // 초기 월드의 마지막 바이트까지 송신한 뒤에만 준비 완료를 받는다.
    if (hasQueuedFrame(client, Protocol::MessageType::WORLD_STATE_INIT)) return false;

    constexpr auto readyFrameSize = Protocol::HEADER_SIZE + Protocol::CLIENT_READY_BODY_SIZE;
    if (count > readyFrameSize - client.receiveBuffer.size()) return false;
    client.receiveBuffer.insert(client.receiveBuffer.end(), bytes, bytes + count);
    std::vector<DecodedFrame> frames;
    if (!decodeFrames(client.receiveBuffer, frames)) return false;
    if (frames.empty()) return client.receiveBuffer.size() < readyFrameSize;
    if (frames.size() != 1 || !client.receiveBuffer.empty()) return false;

    const auto& frame = frames.front();
    if (frame.header.type != Protocol::MessageType::CLIENT_READY ||
        frame.header.sequence != 2 || frame.body.size() != Protocol::CLIENT_READY_BODY_SIZE) return false;

    if (readNetworkU32(frame.body.data()) != client.sessionId) return false;

    std::vector<char> startBody(Protocol::MATCH_START_BODY_SIZE);
    auto* startBytes = reinterpret_cast<unsigned char*>(startBody.data());
    writeNetworkU32(startBytes, client.sessionId);
    writeNetworkU32(startBytes + 4, nextWorldTick);
    if (!enqueueFrame(client, Protocol::MessageType::MATCH_START, startBody)) return false;
    client.matchStartQueued = true;
    return true;
}

bool consumePlayerInput(ClientSession& client, const char* bytes, std::size_t count) {
    if (hasQueuedFrame(client, Protocol::MessageType::MATCH_START)) return false;

    client.receiveBuffer.insert(client.receiveBuffer.end(), bytes, bytes + count);
    std::vector<DecodedFrame> frames;
    constexpr auto inputFrameSize = Protocol::HEADER_SIZE + Protocol::PLAYER_INPUT_BODY_SIZE;
    // 완성된 프레임을 처리하고 나면 올바른 미완성 입력은 26바이트 미만이다.
    if (!decodeFrames(client.receiveBuffer, frames) || client.receiveBuffer.size() >= inputFrameSize) return false;

    for (const auto& frame : frames) {
        if (frame.header.type != Protocol::MessageType::PLAYER_INPUT ||
            frame.header.sequence != client.nextInputSequence ||
            frame.body.size() != Protocol::PLAYER_INPUT_BODY_SIZE) return false;

        const char* body = frame.body.data();
        std::uint16_t buttons;
        std::memcpy(&buttons, body + 4, sizeof(buttons));
        const Protocol::PlayerInput input{ readNetworkU32(body), ntohs(buttons),
            readNetworkFloat(body + 6), readNetworkFloat(body + 10) };
        if (!Protocol::isValidPlayerInput(input)) return false;

        // 헤더 순번은 모든 프레임에서 전진하고, 기체에는 가장 최근 입력만 반영한다.
        if (++client.nextInputSequence == 0) client.nextInputSequence = 1;
        const auto tickDifference = input.clientTick - client.latestInput.clientTick;
        if (!client.hasInput || (tickDifference != 0 && tickDifference < 0x80000000u)) {
            client.latestInput = input;
            client.hasInput = true;
        }
    }
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

bool pollServer(ServerResources& server, long waitMicros,
    const std::uint32_t* nextWorldTick = nullptr) {
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
        bool keepConnection = !client.sendFailed;
        if (!client.joinValidated &&
            std::chrono::steady_clock::now() - client.connectedAt >= kJoinRequestTimeout) {
            std::cerr << "[Server] Join request timed out. Player ID: " << client.playerId
                << " | Session ID: " << client.sessionId << std::endl;
            keepConnection = false;
        }
        if (keepConnection && FD_ISSET(client.socket, &readable)) {
            char buffer[256];
            const int received = recv(client.socket, buffer, sizeof(buffer), 0);
            if (received > 0) {
                const auto count = static_cast<std::size_t>(received);
                if (!client.joinValidated) {
                    keepConnection = consumeJoinRequest(client, buffer, count);
                } else {
                    keepConnection = nextWorldTick != nullptr && (client.matchStartQueued ?
                        consumePlayerInput(client, buffer, count) :
                        consumeClientReady(client, buffer, count, *nextWorldTick));
                    if (!keepConnection) {
                        std::cerr << "[Server] Unexpected message after JOIN. Closing session " << client.sessionId << std::endl;
                    }
                }
            } else if (received == 0)
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

void writeNetworkFloat(unsigned char* bytes, float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559,
        "Combat events require IEEE 754 binary32");
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    writeNetworkU32(bytes, bits);
}

std::vector<char> encodeCombatEventBody(const Protocol::CombatEvent& event, std::uint32_t eventId) {
    std::vector<char> body(Protocol::COMBAT_EVENT_BODY_SIZE);
    auto* bytes = reinterpret_cast<unsigned char*>(body.data());
    writeNetworkU32(bytes, event.serverTick);
    writeNetworkU32(bytes + 4, eventId);
    writeNetworkU16(bytes + 8, static_cast<std::uint16_t>(event.eventType));
    writeNetworkU32(bytes + 10, event.sourcePlayerId);
    writeNetworkU32(bytes + 14, event.projectileId);
    bytes[18] = static_cast<unsigned char>(event.targetKind);
    writeNetworkU32(bytes + 19, event.targetId);
    writeNetworkFloat(bytes + 23, event.position.x);
    writeNetworkFloat(bytes + 27, event.position.y);
    writeNetworkFloat(bytes + 31, event.position.z);
    writeNetworkU32(bytes + 35, static_cast<std::uint32_t>(event.damage));
    return body;
}
#endif
}

#ifdef NETGAME_EMBEDDED_SERVER
bool startHostServer() {
    return startServer(hostServer);
}

bool pollHostServer(std::uint32_t nextWorldTick) {
    return pollServer(hostServer, 0, &nextWorldTick);
}

std::uint32_t joinedHostPlayerId() {
    for (const auto& client : hostServer.clients) {
        if (client.joinValidated && !client.sendFailed) return client.playerId;
    }
    return 0;
}

std::uint32_t readHostPlayerInput(Protocol::PlayerInput& input) {
    for (const auto& client : hostServer.clients) {
        if (client.joinValidated && client.matchStartQueued && client.hasInput && !client.sendFailed) {
            input = client.latestInput;
            return client.playerId;
        }
    }
    return 0;
}

bool hostNeedsWorldInit(std::uint32_t playerId) {
    for (const auto& client : hostServer.clients) {
        if (client.playerId == playerId) {
            return client.joinValidated && !client.initialWorldQueued && !client.sendFailed;
        }
    }
    return false;
}

void queueHostWorldInit(std::uint32_t playerId, const std::vector<char>& body) {
    for (auto& client : hostServer.clients) {
        if (client.playerId != playerId || !client.joinValidated ||
            client.initialWorldQueued || client.sendFailed) continue;
        if (body.size() < Protocol::WORLD_INIT_HEADER_SIZE || body.size() > UINT32_MAX) {
            std::cerr << "[Server] Cannot build initial world. Closing session " << client.sessionId << std::endl;
            client.sendFailed = true;
            return;
        }

        for (std::size_t offset = 0; offset < body.size();) {
            const std::size_t count = (std::min)(body.size() - offset, Protocol::MAX_INIT_CHUNK_DATA_SIZE);
            std::vector<char> chunk(Protocol::WORLD_INIT_CHUNK_HEADER_SIZE + count);
            auto* header = reinterpret_cast<unsigned char*>(chunk.data());
            writeNetworkU32(header, client.sessionId); // 한 세션마다 초기화는 한 번이다.
            writeNetworkU32(header + 4, static_cast<std::uint32_t>(body.size()));
            writeNetworkU32(header + 8, static_cast<std::uint32_t>(offset));
            std::memcpy(chunk.data() + Protocol::WORLD_INIT_CHUNK_HEADER_SIZE, body.data() + offset, count);
            if (!enqueueFrame(client, Protocol::MessageType::WORLD_STATE_INIT, chunk)) {
                std::cerr << "[Server] Cannot queue initial world. Closing session " << client.sessionId << std::endl;
                client.sendFailed = true;
                return;
            }
            offset += count;
        }
        client.initialWorldQueued = true;
        return;
    }
}

void publishEvent(const Protocol::CombatEvent& event) {
    if (!std::isfinite(event.position.x) || !std::isfinite(event.position.y) ||
        !std::isfinite(event.position.z) || event.damage < 0) return;

    for (auto& client : hostServer.clients) {
        if (!client.joinValidated || client.sendFailed) continue;
        if (client.nextEventId == 0 || !enqueueFrame(client, Protocol::MessageType::COMBAT_EVENT,
            encodeCombatEventBody(event, client.nextEventId))) {
            // 이벤트를 덮어쓰거나 건너뛰면 참가자마다 연출 순서가 달라진다.
            std::cerr << "[Server] Cannot queue combat event. Closing session " << client.sessionId << std::endl;
            client.sendFailed = true;
            continue;
        }
        ++client.nextEventId;
    }
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
