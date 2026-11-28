#include <winsock2.h>
#include <cstring>
#include <cstdint>
#include <utility>
#include "netframe.h"

#pragma comment(lib, "Ws2_32.lib")

bool encodeFrame(
    Protocol::MessageType type, // 메시지 종류
    std::uint32_t sequence,     // 메시지 순서 번호
    const std::vector<char>& body, // 메시지 본문
    std::vector<char>& outFrame)   // 인코딩된 프레임을 저장할 벡터
{
    // 1. 이전 출력 내용 지우기
    outFrame.clear();

    // 2. 본문이 MAX_BODY_SIZE보다 크면 false 반환
    if (body.size() > Protocol::MAX_BODY_SIZE) {
        return false;
    }

    // 3. 헤더 + 본문 크기만큼 outFrame 크기 설정
    outFrame.resize(Protocol::HEADER_SIZE + body.size());


    // 4. 헤더 값 4개를 네트워크 바이트 순서로 변환 
    std::uint16_t netVersion = htons(Protocol::VERSION);   // 2바이트
    std::uint16_t netType = htons(static_cast<std::uint16_t>(type)); // 2바이트
    std::uint32_t netBodyLength = htonl(static_cast<std::uint32_t>(body.size())); // 4바이트
    std::uint32_t netSequence = htonl(sequence);           // 4바이트

    // 5. 변환한 값을 정해진 헤더 위치에 복사
    std::memcpy(&outFrame[Protocol::HeaderOffset::VERSION], &netVersion, sizeof(netVersion)); 
    std::memcpy(&outFrame[Protocol::HeaderOffset::TYPE], &netType, sizeof(netType));
    std::memcpy(&outFrame[Protocol::HeaderOffset::BODY_LENGTH], &netBodyLength, sizeof(netBodyLength));
    std::memcpy(&outFrame[Protocol::HeaderOffset::SEQUENCE], &netSequence, sizeof(netSequence));

    // 6. 본문이 있으면 헤더 뒤에 복사
    if (!body.empty()) {
        std::memcpy(&outFrame[Protocol::HEADER_SIZE], body.data(), body.size());
    }

    // 7. true 반환
    return true;
}


bool decodeFrames(
    std::vector<char>& receiveBuffer,
    std::vector<DecodedFrame>& outFrames)
{
    // 1. 이전 호출의 출력 지우기
    outFrames.clear();

    // 이번 호출에서 처리한 바이트 수
    std::size_t offset = 0;

    while (true) {
        const std::size_t remaining =
            receiveBuffer.size() - offset;

        // 2. 헤더 12바이트가 없으면 다음 수신을 기다림
        if (remaining < Protocol::HEADER_SIZE) {
            break;
        }

        const char* header = receiveBuffer.data() + offset;

        // 3. 헤더의 각 필드를 복사
        std::uint16_t netVersion;
        std::uint16_t netType;
        std::uint32_t netBodyLength;
        std::uint32_t netSequence;

        std::memcpy(
            &netVersion,
            header + Protocol::HeaderOffset::VERSION,
            sizeof(netVersion));

        std::memcpy(
            &netType,
            header + Protocol::HeaderOffset::TYPE,
            sizeof(netType));

        std::memcpy(
            &netBodyLength,
            header + Protocol::HeaderOffset::BODY_LENGTH,
            sizeof(netBodyLength));

        std::memcpy(
            &netSequence,
            header + Protocol::HeaderOffset::SEQUENCE,
            sizeof(netSequence));

        // 4. 네트워크 바이트 순서 -> 호스트 바이트 순서
        const std::uint16_t version = ntohs(netVersion);
        const std::uint16_t type = ntohs(netType);
        const std::uint32_t bodyLength = ntohl(netBodyLength);
        const std::uint32_t sequence = ntohl(netSequence);

        // 현재 MessageType은 1~12가 빠짐없이 정의되어 있음
        const bool validType =
            type >= static_cast<std::uint16_t>(
                Protocol::MessageType::SESSION_JOIN_REQUEST) &&
            type <= static_cast<std::uint16_t>(
                Protocol::MessageType::SESSION_CLOSE);

        // 5. 공통 헤더 검증
        if (version != Protocol::VERSION ||
            !validType ||
            bodyLength > Protocol::MAX_BODY_SIZE ||
            sequence == 0)
        {
            // 오류가 있으면 이번 호출의 결과는 사용하지 않음
            outFrames.clear();
            return false;
        }

        const std::size_t frameSize =
            Protocol::HEADER_SIZE +
            static_cast<std::size_t>(bodyLength);

        // 6. 본문이 아직 다 도착하지 않았으면 기다림
        if (remaining < frameSize) {
            break;
        }

        // 7. 완성된 프레임의 헤더 저장
        DecodedFrame frame;

        frame.header.version = version;
        frame.header.type = static_cast<Protocol::MessageType>(type);
        frame.header.bodyLength = bodyLength;
        frame.header.sequence = sequence;

        // 본문은 아직 해석하지 않고 바이트 그대로 저장
        const char* body = header + Protocol::HEADER_SIZE;
        frame.body.assign(body, body + bodyLength);

        outFrames.push_back(std::move(frame));

        // 8. 다음 프레임 위치로 이동
        offset += frameSize;
    }

    // 9. 처리한 데이터만 삭제
    // 미완성 헤더/본문은 버퍼에 남아 다음 수신과 합쳐짐
    using Difference = std::vector<char>::difference_type;

    receiveBuffer.erase(
        receiveBuffer.begin(),
        receiveBuffer.begin() + static_cast<Difference>(offset));

    return true;
}

bool flushSendQueue(SOCKET socket, SendState& state)
{
    while (!state.sendQueue.empty()) {
        const std::vector<char>& frame = state.sendQueue.front();

        const char* start = frame.data() + state.sendOffset;
        const int remaining = static_cast<int>(frame.size() - state.sendOffset);

        const int sent = send(socket, start, remaining, 0);

        if (sent == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                return true;  // 현재 위치를 보존하고 다음 쓰기 기회에 재시도
            }
            return false;
        }

        if (sent == 0) {
            return false;  // 남은 데이터가 있는데 송신이 진행되지 않음
        }

        state.sendOffset += sent;

        if (state.sendOffset == frame.size()) {
            state.sendQueue.pop_front();
            state.sendOffset = 0;  // 다음 프레임의 시작 위치
        }
    }

    return true;
}

bool networkLoop(
    SOCKET socket,
    bool readable,
    bool writable,
    std::vector<char>& receiveBuffer,
    SendState& sendState,
    std::vector<DecodedFrame>& outFrames)
{
    // 이번 호출에서 완성된 프레임만 전달한다.
    outFrames.clear();

    if (readable) {
        char buffer[256];
        const int received = recv(socket, buffer, sizeof(buffer), 0);

        if (received > 0) {
            receiveBuffer.insert(
                receiveBuffer.end(), buffer, buffer + received);

            if (!decodeFrames(receiveBuffer, outFrames))
                return false;
        }
        else if (received == 0) {
            return false;  // 상대가 연결을 종료함
        }
        else if (WSAGetLastError() != WSAEWOULDBLOCK) {
            return false;  // 복구할 수 없는 수신 오류
        }
    }

    if (writable) {
        if (!flushSendQueue(socket, sendState))
            return false;
    }

    return true;
}

bool checkTimeout(
    std::chrono::steady_clock::time_point startedAt,
    std::chrono::milliseconds timeout)
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - startedAt;

    return elapsed >= timeout;
}