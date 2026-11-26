#pragma once

#include "Protocol.h"

// GLUT 게임 스레드에서 접속 관리, 전투 이벤트 등록, 소켓 송신을 순서대로 처리한다.
bool startHostServer();
bool pollHostServer();
// eventId는 수신 연결별로 부여한다. 호출자는 판정 결과와 발생 틱을 넘긴다.
void publishEvent(const Protocol::CombatEvent& event);
