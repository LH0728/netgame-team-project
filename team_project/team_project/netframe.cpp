#include <winsock2.h>
#include <cstring>
#include <cstdint>
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