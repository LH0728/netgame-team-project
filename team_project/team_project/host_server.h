#pragma once

#include "Protocol.h"

// GLUT 게임 스레드에서 접속 관리, 전투 이벤트 등록, 소켓 송신을 순서대로 처리한다.
bool startHostServer();
bool pollHostServer();
