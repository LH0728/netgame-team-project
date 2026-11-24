#pragma once

#include "Protocol.h"
#include <vector>
#include <deque>

// encodeFrame(): 메시지 타입, 순번, 본문을 받아 헤더를 붙인 전송 바이트 생성
bool encodeFrame(
    Protocol::MessageType type,
    std::uint32_t sequence,
    const std::vector<char>& body,
    std::vector<char>& outFrame
);

// decodeFrames(): 수신 버퍼에서 완성된 프레임들을 꺼내 헤더와 본문으로 분리
struct DecodedFrame {
    Protocol::FrameHeader header;
    std::vector<char> body;
};

bool decodeFrames(
    std::vector<char>& receiveBuffer,
    std::vector<DecodedFrame>& outFrames);

struct SendState {
    std::deque<std::vector<char>> sendQueue; // 보내려고 대기 중인 프레임들
    std::size_t sendOffset = 0;              // 맨 앞 프레임에서 이미 보낸 바이트 수
};

bool flushSendQueue(SOCKET socket, SendState& state);

