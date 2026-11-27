#pragma once

#include "Protocol.h"
#include <vector>

// GLUT 게임 스레드에서 접속 관리, 전투 이벤트 등록, 소켓 송신을 순서대로 처리한다.
bool startHostServer();
bool pollHostServer(std::uint32_t nextWorldTick);
// 현재 승인된 원격 참가자의 ID. 빈 슬롯이나 승인 대기 중이면 0이다.
std::uint32_t joinedHostPlayerId();
// 가장 최근의 유효 입력을 복사하고 해당 세션의 플레이어 ID를 반환한다. 입력이 없으면 0이다.
std::uint32_t readHostPlayerInput(Protocol::PlayerInput& input);
bool hostNeedsWorldInit(std::uint32_t playerId);
// 빈 본문은 초기화 본문 생성 실패로 처리한다.
void queueHostWorldInit(std::uint32_t playerId, const std::vector<char>& body);
// eventId는 수신 연결별로 부여한다. 호출자는 판정 결과와 발생 틱을 넘긴다.
void publishEvent(const Protocol::CombatEvent& event);
