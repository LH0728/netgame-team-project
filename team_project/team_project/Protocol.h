#pragma once

#include <cstddef>
#include <cstdint>

namespace Protocol
{
    // ==========================================
    // 1. 기본 통신 규칙
    // ==========================================

    constexpr std::uint16_t PORT = 50000; // 포트 번호

    // 제안값: 서버와 클라이언트가 같은 값을 사용
    constexpr std::uint16_t VERSION = 1;

    constexpr std::size_t HEADER_SIZE = 12; // 헤더 크기
	constexpr std::size_t MAX_BODY_SIZE = 64 * 1024; // 64KB 본문 최대 크기 
    constexpr std::size_t MAX_FRAME_SIZE =
		HEADER_SIZE + MAX_BODY_SIZE; // 헤더 + 본문 최대 크기

    constexpr std::uint32_t RECEIVE_TIMEOUT_MS = 5000;


    // ==========================================
    // 2. 메시지 종류
    // 숫자 코드는 팀원들과 합의 후 공통 사용
    // ==========================================

    enum class MessageType : std::uint16_t
    {
        SESSION_JOIN_REQUEST = 1,  // 참가 요청
        SESSION_JOIN_ACCEPT = 2,  // 참가 승인

        WORLD_STATE_INIT = 3,  // 초기 월드 정보
        CLIENT_READY = 4,  // 참가자 준비 완료
        MATCH_START = 5,  // 게임 시작

        PLAYER_INPUT = 6,  // 이동·가속·발사·조준 입력
        WORLD_SNAPSHOT = 7,  // 현재 월드 상태

        ASTEROID_STATE_DELTA = 8,  // 소행성 변경 정보
        COMBAT_EVENT = 9,  // 피격·폭발 연출 이벤트

        HEARTBEAT_REQUEST = 10, // 연결 확인 요청
        HEARTBEAT_RESPONSE = 11, // 연결 확인 응답

        SESSION_CLOSE = 12  // 세션 종료
    };


    // ==========================================
    // 3. 전송 헤더에서 각 필드가 시작하는 위치
    // ==========================================

    namespace HeaderOffset
    {
        constexpr std::size_t VERSION = 0;  // 2바이트  
        constexpr std::size_t TYPE = 2;  // 2바이트
        constexpr std::size_t BODY_LENGTH = 4;  // 4바이트
        constexpr std::size_t SEQUENCE = 8;  // 4바이트
    }


    // ==========================================
    // 4. 프로그램 내부에서 사용할 헤더 정보
    // ==========================================

    struct FrameHeader
    {
        std::uint16_t version;
        MessageType type;
        std::uint32_t bodyLength;
        std::uint32_t sequence;
    };

    // 주의:
    // FrameHeader 구조체 자체를 그대로 send()하지 않는다.
    //
    // encodeFrame():
    //   각 필드를 네트워크 바이트 순서로 변환해서
    //   정해진 위치에 기록한다.
    //
    // decodeFrames():
    //   각 필드를 읽고 호스트 바이트 순서로 변환한다.
    //
    // 전송 헤더 크기는 sizeof(FrameHeader)가 아니라
    // HEADER_SIZE를 사용한다.


    // ==========================================
    // 5. 메시지별 본문 규칙 — 이후 팀원들과 정의
    // ==========================================

    // 만들어야 할 구조체

    // SESSION_JOIN_REQUEST
    // TODO: 참가 요청에 필요한 필드

    // SESSION_JOIN_ACCEPT
    // TODO: 참가자 ID, 세션 ID의 자료형과 기록 순서

    // WORLD_STATE_INIT
    // TODO: 시작 틱, 기체·행성·소행성 초기값
    // TODO: 한도 초과 시 분할·조립 규칙

    // CLIENT_READY / MATCH_START
    // TODO: 본문 유무와 필요한 필드

    // PLAYER_INPUT
    // TODO: 입력 비트 배치, yaw·pitch 표현과 단위

    // WORLD_SNAPSHOT
    // TODO: 서버 틱, 기체, 투사체, 행성, 클리어 정보

    // ASTEROID_STATE_DELTA / COMBAT_EVENT
    // TODO: 대상 ID, 이벤트 ID, 변경 내용

    // HEARTBEAT_REQUEST / HEARTBEAT_RESPONSE
    // TODO: 본문 유무, 요청·응답 대응 방식

    // SESSION_CLOSE
    // TODO: 종료 사유 코드
}