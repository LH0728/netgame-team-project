#pragma once

#include <cmath>
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
    // 5. 메시지별 본문 규칙
    // ==========================================

    // 아래 구조체는 프로그램 내부 표현이다. sizeof/구조체 메모리를 전송하지 않는다.
    // 전송 시 선언 순서대로 필드를 패딩 없이 기록한다. SIZE 상수가 전송 크기다.
    // 모든 정수는 big endian. float는 유한한 IEEE 754 binary32 값만 허용하며
    // 비트를 uint32_t로 복사한 후 big endian으로 기록한다. bool/포인터/GLM/GLuint는 전송하지 않는다.
    // Vec3는 x, y, z 순서. 거리 단위는 게임 월드 단위, 속도는 월드 단위/초,
    // 각도는 도(degree), 시간은 초. 기존 로컬 프레임별 속도는 전송 전 이 단위로 변환한다.
    // ID 0은 미지정. 서버가 ID를 부여하며 세션 동안 재사용하지 않는다.
    // 행성 ID는 수성=1, 금성=2, 지구=3, 화성=4, 목성=5, 토성=6, 천왕성=7, 해왕성=8.
    // 달/고리/텍스처/설명문/파티클은 클라이언트가 공통 리소스로 렌더링한다.
    constexpr std::uint32_t HOST_PLAYER_ID = 1;
    // 참가자 ID는 서버가 접속마다 2부터 순서대로 부여한다.
    constexpr std::uint32_t MAX_PLANETS = 8;
    constexpr std::uint32_t MAX_ASTEROIDS = 1500;
    constexpr std::uint32_t MAX_PROJECTILES = 4096;

    struct Vec3 { float x, y, z; };
    constexpr std::size_t VEC3_SIZE = 12;

    enum class WorldPhase : std::uint8_t {
        Playing = 0, SunExploding = 1, Cleared = 2
    };

    struct WorldState {
        WorldPhase phase;
        std::uint8_t rotating; // 0/1 자전
        std::uint8_t orbiting; // 0/1 공전
        float globalTime;
        float rotateTime;
        float orbitTime;
        float sunExplosionTime;
    };
    constexpr std::size_t WORLD_STATE_SIZE = 19;

    struct PlayerState {
        std::uint32_t playerId;
        Vec3 position;
        float yaw;   // [0, 360), 0도는 +Z, +90도는 +X
        float pitch; // [-89, 89], 양수는 위쪽
    };
    constexpr std::size_t PLAYER_STATE_SIZE = 24;

    struct PlanetState {
        std::uint32_t planetId;
        float distance;
        float size;
        float initialSize;
        float orbitSpeed;      // 도/초
        float selfRotateSpeed; // 도/초
        float axisTilt;
        std::int32_t health;   // 0 이상
        std::uint8_t active;   // 0/1, 파괴되면 0
    };
    constexpr std::size_t PLANET_STATE_SIZE = 33;

    struct AsteroidState {
        std::uint32_t asteroidId;
        Vec3 position; // 이 메시지의 serverTick 시점 실제 위치
        Vec3 velocity;
        float size;
        float initialSize;
        float rotationAngle;
        float rotationSpeed;
        Vec3 rotationAxis; // 단위 벡터
        std::int32_t health;
    };
    constexpr std::size_t ASTEROID_STATE_SIZE = 60;

    struct ProjectileState {
        std::uint32_t projectileId;
        std::uint32_t ownerPlayerId;
        Vec3 position;
        Vec3 velocity;
        float lifeTime;
    };
    constexpr std::size_t PROJECTILE_STATE_SIZE = 36;

    // 헤더 sequence는 각 송신 방향에서 1부터 프레임마다 증가하며 0을 쓰지 않는다.
    // 최댓값 다음은 1. serverTick도 u32 순환 값이며 틱 간 비교는 순환을 고려한다.
    // 본문 길이, 개수, enum, 플래그, ID, 수치 범위를 검증하고 남는 바이트도 거절한다.
    // 아래에 허용한 비트 외의 입력 비트와 정의되지 않은 enum 값은 거절한다.

    // ==========================================
    // 메시지별 본문과 방향
    // ==========================================

    // SESSION_JOIN_REQUEST: 클라이언트 -> 서버, 본문 없음, 최초 sequence=1.
    // 현재 동작 중인 12바이트 참가 요청 및 8바이트 승인 본문과 호환된다.
    constexpr std::size_t SESSION_JOIN_REQUEST_BODY_SIZE = 0;

    struct SessionJoinAccept { // 서버 -> 클라이언트
        std::uint32_t playerId;
        std::uint32_t sessionId;
    };
    constexpr std::size_t SESSION_JOIN_ACCEPT_BODY_SIZE = 8;

    // WORLD_STATE_INIT: 서버 -> 클라이언트.
    // 논리 데이터 = WorldInitHeader, PlayerState[playerCount],
    // PlanetState[planetCount], AsteroidState[asteroidCount], ProjectileState[projectileCount].
    struct WorldInitHeader {
        std::uint32_t serverTick;
        std::uint32_t tickRate; // 초당 서버 틱, 1..1000
        WorldState world;
        std::uint32_t playerCount;
        std::uint32_t planetCount;
        std::uint32_t asteroidCount;
        std::uint32_t projectileCount;
    };
    constexpr std::size_t WORLD_INIT_HEADER_SIZE = 43;
    // 참가 인원에 고정 상한을 두지 않는다. 개수로 계산한 전체 크기는 u32 범위여야 한다.

    // WORLD_STATE_INIT의 각 프레임 본문은 아래 헤더 + 논리 데이터의 연속 바이트 조각.
    // 조각마다 별도 sequence를 쓴다. 레코드 경계를 가로질러 분할할 수 있다.
    struct WorldInitChunk {
        std::uint32_t initializationId; // 세션 내 0이 아닌 초기화 식별자
        std::uint32_t totalBytes;       // WORLD_INIT_HEADER_SIZE 이상, u32 범위
        std::uint32_t offset;           // 논리 데이터 내 시작 바이트 위치
        // 뒤에 1..MAX_INIT_CHUNK_DATA_SIZE 바이트
    };
    constexpr std::size_t WORLD_INIT_CHUNK_HEADER_SIZE = 12;
    constexpr std::size_t MAX_INIT_CHUNK_DATA_SIZE = MAX_BODY_SIZE - WORLD_INIT_CHUNK_HEADER_SIZE;
    // offset=0부터 연속 전송. 같은 initializationId/totalBytes를 유지하고
    // offset은 누적 수신 길이와 정확히 같아야 한다. 겹침/누락/초과는 거절한다.
    // 수신 조각만큼 점진적으로 저장한다. totalBytes만 보고 선할당하지 않는다.
    // 동시에 한 초기화만 조립하며 자원 부족 시 InitializationFailed로 종료한다.
    // 첫 조각부터 RECEIVE_TIMEOUT_MS 내 전부 조립되지 않으면 연결을 종료한다.
    // 전부 조립하고 개수로 계산한 길이와 totalBytes가 일치할 때만 월드에 적용한다.

    struct ClientReady { // 클라이언트 -> 서버, 초기 월드 적용 완료 후
        std::uint32_t initializationId;
    };
    constexpr std::size_t CLIENT_READY_BODY_SIZE = 4;

    struct MatchStart { // 서버 -> 클라이언트, 해당 초기화의 준비 완료 확인 후
        std::uint32_t initializationId;
        std::uint32_t startTick; // 서버가 플레이를 시작할 틱
    };
    constexpr std::size_t MATCH_START_BODY_SIZE = 8;
    // 틱은 서버 기준이다. 클라이언트는 자기 시계와 직접 비교하지 않는다.
    // 같은 initializationId만 수락한다. MATCH_START 이전 PLAYER_INPUT은 거절한다.

    enum class InputBits : std::uint16_t {
        Forward = 1u << 0, Backward = 1u << 1,
        Left = 1u << 2, Right = 1u << 3,
        Boost = 1u << 4, Fire = 1u << 5
    };
    constexpr std::uint16_t INPUT_MASK = 0x003f;
    struct PlayerInput { // 클라이언트 -> 서버
        std::uint32_t clientTick;
        std::uint16_t buttons; // InputBits OR 조합, 누르고 있는 상태
        float yaw;
        float pitch;
    };
    constexpr std::size_t PLAYER_INPUT_BODY_SIZE = 14;

    inline bool isValidPlayerInput(const PlayerInput& input) {
        return (input.buttons & ~INPUT_MASK) == 0 &&
            std::isfinite(input.yaw) && std::isfinite(input.pitch) &&
            input.yaw >= 0.0f && input.yaw < 360.0f &&
            input.pitch >= -89.0f && input.pitch <= 89.0f;
    }
    // 플레이어는 소켓의 세션으로 식별한다. 입력은 다음 서버 틱부터 적용한다.
    // 이동/가속은 PlayerState와 같은 방향 기준. Fire는 서버 발사 간격을 적용한다.
    // 카메라 모드와 무관하게 yaw/pitch는 실제 기체/발사 방향이다.

    // WORLD_SNAPSHOT: 서버 -> 클라이언트. 아래 헤더 뒤에
    // PlayerState[playerCount], ProjectileState[projectileCount], PlanetState[planetCount].
    struct WorldSnapshotHeader {
        std::uint32_t serverTick;
        WorldState world;
        std::uint32_t playerCount;
        std::uint32_t projectileCount;
        std::uint32_t planetCount;
    };
    constexpr std::size_t WORLD_SNAPSHOT_HEADER_SIZE = 35;
    // 하나의 논리 스냅샷이 완전한 기체/행성/활성 투사체 목록이다.
    // WORLD_STATE_INIT과 같은 12바이트 조각 헤더 형식을 매 프레임 앞에 쓴다.
    // 첫 필드는 initializationId 대신 snapshotId이며 세션 내 0이 아닌 증가 번호다.
    // totalBytes는 WORLD_SNAPSHOT_HEADER_SIZE 이상. 조립/길이 검증 규칙은 초기 월드와 같다.
    // 전부 조립한 뒤 한 번에 적용한다. 목록에 없는 투사체는 제거한다.
    // 플레이어 수에 따른 고정 상한은 없으며 MAX_BODY_SIZE는 각 프레임에 적용한다.
    // 월드의 클리어/태양 폭발 상태도 이 메시지로 동기화한다.

    // ASTEROID_STATE_DELTA: 서버 -> 클라이언트, 아래 헤더 + AsteroidState[count].
    struct AsteroidStateDeltaHeader {
        std::uint32_t serverTick;
        std::uint32_t count;
    };
    constexpr std::size_t ASTEROID_DELTA_HEADER_SIZE = 8;
    // 변경된 소행성의 전체 레코드만 포함. health=0, size=0이면 제거.
    // count는 MAX_ASTEROIDS 이하, 본문 크기는 MAX_BODY_SIZE 이하.
    // 넘으면 레코드 경계에서 여러 프레임으로 나눈다. 각 프레임을 독립 적용한다.

    enum class TargetKind : std::uint8_t {
        None = 0, Player = 1, Planet = 2, Asteroid = 3, Sun = 4
    };
    enum class CombatEventType : std::uint16_t {
        Hit = 1, Destroyed = 2, SunExplosion = 3
    };
    struct CombatEvent { // 서버 -> 클라이언트, 효과 재생용. 상태는 snapshot/delta가 확정.
        std::uint32_t serverTick;
        std::uint32_t eventId; // 세션 내 1부터 증가, 중복 ID는 효과를 재생하지 않는다.
        CombatEventType eventType;
        std::uint32_t sourcePlayerId; // 환경 이벤트면 0
        std::uint32_t projectileId;   // 환경 이벤트면 0
        TargetKind targetKind;
        std::uint32_t targetId;       // None/Sun이면 0
        Vec3 position;
        std::int32_t damage;          // 0 이상, 해당 없으면 0
    };
    constexpr std::size_t COMBAT_EVENT_BODY_SIZE = 39;

    struct Heartbeat { // 요청/응답 모두 같은 본문, 양방향
        std::uint32_t requestId;
    };
    constexpr std::size_t HEARTBEAT_BODY_SIZE = 4;
    // 요청마다 0이 아닌 requestId를 부여하고 응답은 그 값을 그대로 돌려준다.
    // 응답 프레임에는 응답 송신 방향의 새 sequence를 사용한다.
    // 한 방향당 요청 하나만 대기. RECEIVE_TIMEOUT_MS 내 일치 응답이 없으면 연결 종료.
    // HEARTBEAT_RESPONSE에 다시 응답하지 않는다.

    enum class CloseReason : std::uint16_t {
        Normal = 0, ServerShutdown = 1, RoomFull = 2, VersionMismatch = 3,
        InvalidMessage = 4, Timeout = 5, InitializationFailed = 6
    };
    struct SessionClose { // 양방향
        CloseReason reason;
    };
    constexpr std::size_t SESSION_CLOSE_BODY_SIZE = 2;
    // 가능한 경우 SESSION_CLOSE를 전송한 뒤 소켓을 닫는다.
    // 예고 없는 EOF/소켓 오류도 종료로 처리하며 서버는 세션 자원을 해제한다.
}
