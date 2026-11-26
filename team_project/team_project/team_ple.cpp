#define _CRT_SECURE_NO_WARNINGS
#include "connection_check.h"
#include "host_server.h"
#include "netframe.h"
#include "snapshot_sender.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" // 프로젝트 폴더에 stb_image.h 파일이 있어야 합니다.

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <unordered_set>

#include <gl/glew.h>
#include <gl/freeglut.h>
#include <gl/freeglut_ext.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

struct PlanetData {
    std::string name;      // [추가] 행성 이름 (예: "earth")
    float distance;
    float size;
    float orbitSpeed;
    float selfRotateSpeed; // 자전 속도
    float axisTilt;        // [추가] 자전축 기울기 (도 단위)
    GLuint textureID;

    // [추가] 행성 체력 (폭발 로직을 위해 추가)
    int health;
    float initialSize; // [추가] 크기 변화를 위해 초기 크기 저장
    bool active;

    std::string realDiameter;   // 실제 지름
    std::string realDay;        // 자전 주기 (1일 길이)
    std::string realYear;       // 공전 주기 (1년 길이)
    std::string planetType;     // 행성 타입 (암석/가스형)
};

struct AsteroidData {
    std::uint32_t asteroidId;
    glm::vec3 position;
    glm::vec3 velocity;
    float size;
    float initialSize;
    float rotationSpeed;
    glm::vec3 rotationAxis;
    int health;
};

struct MissileData {
    std::uint32_t projectileId;
    std::uint32_t ownerPlayerId;
    glm::vec3 position;
    glm::vec3 velocity;
    float yaw;   // [변경] 좌우 회전 각도 (기존 angle 대체)
    float pitch; // [추가] 상하 회전 각도
    float lifeTime;
    bool active;
};

struct MissileLauncher {
    std::uint32_t ownerPlayerId;
    std::uint64_t lastFireTick = 0;
    bool hasFired = false;
};

struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float life;
    float size;
    float fadeSpeed;
};

// --- 전역 변수 ---
GLint width = 1200, height = 800;
GLuint shaderProgramID;
GLuint vertexShader, fragmentShader;
GLuint modelLoc, viewLoc, projLoc, colorLoc, lightPosLoc, viewPosLoc, isSunLoc;
GLuint whiteTex; // [추가] 빈 텍스처 (하얀색)

// VAO/VBO
GLuint sphereVAO, sphereVBO, sphereEBO;
GLuint asteroidVAO, asteroidVBO;
GLuint starVAO, starVBO;
GLuint ringVAO, ringVBO; // 링 모델 데이터
GLuint saturnRingTex;    // 링 텍스처 ID

size_t sphereIndexCount = 0;

// 텍스처 ID들
GLuint sunTex, mercuryTex, venusTex, earthTex, moonTex, marsTex, jupiterTex, saturnTex, uranusTex, neptuneTex;
GLuint backgroundTex;

std::vector<PlanetData> planets;
std::vector<AsteroidData> asteroids;
std::vector<MissileData> missiles;
std::uint32_t nextProjectileId = 1; // 0은 미지정이며 이미 사용한 번호는 재사용하지 않는다.
std::vector<Particle> particles;

const int asteroidCount = 1500;
const int starCount = 4000;
const float missileSpeed = 2.5f;
const float missileRange = 5000.0f;
const float PI = 3.1415926535f;

// 우주선(플레이어)
glm::vec3 shipPos(0.0f, 500.0f, 1500.0f);
float shipAngle = 180.0f;
float shipPitch = 0.0f;
bool keyState[256] = { false };

// 마우스 제어
float mouseSensitivity = 0.1f;
float lastMouseX = -1.0f, lastMouseY = -1.0f;
bool firstMouse = true;
bool isRightButtonDown = false;
int cameraMode = 1;
int targetPlanetIndex = -1; // 현재 추적 중인 행성의 인덱스
float mouse_x = 0.0f, mouse_y = 0.0f;


constexpr unsigned int worldTickMs = 16;
constexpr float worldTickSeconds = worldTickMs / 1000.0f;
constexpr float planetTimeStep = 0.5f; // 한 틱 동안 공전·자전 시계가 전진하는 양
constexpr float planetSpinMultiplier = 5.0f;
constexpr float sunExplosionTimeStep = 0.1f; // 한 틱에 진행하는 폭발 연출 시간
std::uint64_t worldTick = 0; // 초기 월드는 0틱이며 월드를 갱신할 때마다 증가한다.
float globalTime = 0.0f;
// [추가] 자전/공전 제어 변수
bool isRotating = true;  // 자전 스위치 (기본 켜짐)
bool isOrbiting = true;  // 공전 스위치 (기본 켜짐)
float rotateTime = 0.0f; // 자전용 누적 시간
float orbitTime = 0.0f;  // 공전용 누적 시간


// [추가] 인게임 검색창 관련 변수
bool isTyping = false;       // 현재 검색창이 켜져있는가?
std::string inputString = ""; // 현재 입력된 검색어
bool isGameCleared = false;

constexpr std::uint64_t missileFireDelayTicks = 10;
MissileLauncher hostMissileLauncher{Protocol::HOST_PLAYER_ID};


// --- 전역 변수 ---
// ... (기존 변수들 아래에 추가)

// [추가] 태양 폭발 연출용 변수
bool isSunExploding = false;    // 태양이 폭발 중인가?
bool isSunDestroyed = false;    // 태양이 완전히 사라졌는가?
float sunExplosionTimer = 0.0f; // 폭발 진행 시간

// --- 함수 선언 ---
char* filetobuf(const char* file);
void make_vertexShaders();
void make_fragmentShaders();
GLuint make_shaderProgram();
GLuint loadTexture(char const* path);

void initSphereData();
void initAsteroidModel();
void initStars();
void initPlanetSettings();
void initAsteroids();

void spawnExplosion(glm::vec3 pos, glm::vec3 color, int count);
void spawnTrail(glm::vec3 pos, glm::vec3 color);

void updateParticles();
void updateGameLogic();
void updateMovement(glm::vec3& position, float yaw, float pitch,
    int forwardInput, int strafeInput, bool boosting);
void spawnMissile(MissileLauncher& launcher, const glm::vec3& origin, const glm::vec3& direction);
void fireMissile();

void drawScene();
void Reshape(int w, int h);
void Mouse(int button, int state, int x, int y);
void Motion(int x, int y);
void Keyboard(unsigned char key, int x, int y);
void KeyboardUp(unsigned char key, int x, int y);
void Timer(int value);


// --- 구현 ---

char* filetobuf(const char* file) {
    FILE* fptr;
    long length;
    char* buf;
    fptr = fopen(file, "rb");
    if (!fptr) return NULL;
    fseek(fptr, 0, SEEK_END);
    length = ftell(fptr);
    buf = (char*)malloc(length + 1);
    fseek(fptr, 0, SEEK_SET);
    fread(buf, length, 1, fptr);
    fclose(fptr);
    buf[length] = 0;
    return buf;
}
// [추가] 하얀색 1x1 텍스처 생성 함수 (색상이 있는 물체용)
GLuint createWhiteTexture() {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // 하얀색 (R:255, G:255, B:255, A:255) 픽셀 데이터
    unsigned char whiteData[] = { 255, 255, 255, 255 };

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whiteData);

    // 텍스처 설정
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    return textureID;
}
// [추가] 화면에 글자를 그리는 함수
void renderBitmapString(float x, float y, void* font, const char* string) {
    const char* c;
    glRasterPos2f(x, y);
    for (c = string; *c != '\0'; c++) {
        glutBitmapCharacter(font, *c);
    }
}

// [추가] 검색창 UI를 그리는 함수
void drawInputBox() {
    if (!isTyping) return; // 입력 모드가 아니면 그리지 않음

    // 1. 3D 설정을 잠시 끄고 2D 모드로 전환
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, width, 0, height); // 화면 크기에 맞게 2D 좌표 설정

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST); // UI는 맨 앞에 보여야 하므로 깊이 검사 끔
    glDisable(GL_LIGHTING);   // UI는 조명 영향 안 받음
    glUseProgram(0);          // 쉐이더 끄기 (고정 파이프라인 사용)

    // 2. 반투명 검은색 박스 그리기 (화면 중앙)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.0f, 0.0f, 0.0f, 0.7f); // 검은색, 투명도 0.7
    float cx = width / 2.0f;
    float cy = height / 2.0f;
    float boxW = 300.0f; // 박스 너비 절반
    float boxH = 50.0f;  // 박스 높이 절반

    glBegin(GL_QUADS);
    glVertex2f(cx - boxW, cy - boxH);
    glVertex2f(cx + boxW, cy - boxH);
    glVertex2f(cx + boxW, cy + boxH);
    glVertex2f(cx - boxW, cy + boxH);
    glEnd();
    glDisable(GL_BLEND);

    // 3. 텍스트 그리기
    glColor3f(1.0f, 1.0f, 1.0f); // 흰색 글자

    // 안내 문구
    std::string title = "Enter Planet Name:";
    renderBitmapString(cx - 50, cy + 15, GLUT_BITMAP_HELVETICA_18, title.c_str());

    // 사용자가 입력 중인 글자 (커서 '_' 추가)
    std::string displayStr = inputString + "_";
    renderBitmapString(cx - 30, cy - 15, GLUT_BITMAP_HELVETICA_18, displayStr.c_str());

    // 4. 다시 3D 설정으로 복구
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glUseProgram(shaderProgramID); // 쉐이더 다시 켜기
}

GLuint loadTexture(char const* path) {
    GLuint textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format;
        if (nrComponents == 1) format = GL_RED;
        else if (nrComponents == 3) format = GL_RGB;
        else if (nrComponents == 4) format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else {
        // 텍스처 로드 실패 시 콘솔에 메시지 출력
        std::cout << "Texture load failed: " << path << std::endl;
        stbi_image_free(data);
    }
    return textureID;
}

void initSphereData() {
    std::vector<float> data;
    std::vector<unsigned int> indices;
    const int stackCount = 40;
    const int sectorCount = 40;
    float radius = 1.0f;

    for (int i = 0; i <= stackCount; ++i) {
        float stackAngle = PI / 2 - i * PI / stackCount;
        float xy = radius * cosf(stackAngle);
        float z = radius * sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j) {
            float sectorAngle = j * 2 * PI / sectorCount;
            float x = xy * cosf(sectorAngle);
            float y = xy * sinf(sectorAngle);

            data.push_back(x); data.push_back(y); data.push_back(z); // Pos
            data.push_back(x / radius); data.push_back(y / radius); data.push_back(z / radius); // Normal
            data.push_back((float)j / sectorCount); // U
            data.push_back((float)i / stackCount);  // V
        }
    }

    for (int i = 0; i < stackCount; ++i) {
        int k1 = i * (sectorCount + 1);
        int k2 = k1 + sectorCount + 1;
        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) { indices.push_back(k1); indices.push_back(k2); indices.push_back(k1 + 1); }
            if (i != (stackCount - 1)) { indices.push_back(k1 + 1); indices.push_back(k2); indices.push_back(k2 + 1); }
        }
    }
    sphereIndexCount = indices.size();

    glGenVertexArrays(1, &sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);

    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

    // Stride = 8 floats
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void initAsteroidModel() {
    std::vector<float> data;
    const int stackCount = 20;
    const int sectorCount = 20;
    float radius = 1.0f;

    for (int i = 0; i <= stackCount; ++i) {
        float stackAngle = PI / 2 - i * PI / stackCount;
        float xy = radius * cosf(stackAngle);
        float z = radius * sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j) {
            float sectorAngle = j * 2 * PI / sectorCount;
            float x = xy * cosf(sectorAngle);
            float y = xy * sinf(sectorAngle);

            float noise = 0.8f + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / 0.4f));
            x *= noise; y *= noise; z *= noise;

            data.push_back(x); data.push_back(y); data.push_back(z); // Pos
            glm::vec3 n = glm::normalize(glm::vec3(x, y, z));
            data.push_back(n.x); data.push_back(n.y); data.push_back(n.z); // Normal
            data.push_back((float)j / sectorCount); // U
            data.push_back((float)i / stackCount);  // V
        }
    }

    glGenVertexArrays(1, &asteroidVAO);
    glGenBuffers(1, &asteroidVBO);

    glBindVertexArray(asteroidVAO);
    glBindBuffer(GL_ARRAY_BUFFER, asteroidVBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void initStars() {
    std::vector<float> stars;
    for (int i = 0; i < starCount; ++i) {
        // 1. 위치 (x, y, z) - 범위를 좀 넓게 퍼뜨림
        float x = (rand() % 5000 - 2000) / 5.0f;
        float y = (rand() % 5000 - 2000) / 5.0f;
        float z = (rand() % 5000 - 2000) / 5.0f;

        stars.push_back(x); stars.push_back(y); stars.push_back(z);

        // 2. 법선 (Normal) - 쉐이더 오류 방지용 가짜 데이터 (0, 1, 0)
        stars.push_back(0.0f); stars.push_back(1.0f); stars.push_back(0.0f);

        // 3. 텍스처 좌표 (UV) - 쉐이더 오류 방지용 가짜 데이터 (0, 0)
        stars.push_back(0.0f); stars.push_back(0.0f);
    }

    // VAO, VBO 생성
    glGenVertexArrays(1, &starVAO);
    glGenBuffers(1, &starVBO);

    glBindVertexArray(starVAO);
    glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER, stars.size() * sizeof(float), &stars[0], GL_STATIC_DRAW);

    // ★ 쉐이더 포맷 맞추기 (8개의 float가 한 세트) ★

    // 0번: 위치 (3개)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 1번: 법선 (3개) - 이게 없으면 꺼짐
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 2번: 텍스처 (2개) - 이게 없으면 꺼짐
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // 안전하게 VAO 바인딩 해제
    glBindVertexArray(0);
}
void initPlanetSettings() {
    // 텍스처 로딩 (기존과 동일)
    sunTex = loadTexture("solar.jpg");
    mercuryTex = loadTexture("mercury.jpg");
    venusTex = loadTexture("venus.jpg");
    earthTex = loadTexture("earth.jpg");
    moonTex = loadTexture("moon.jpg");
    marsTex = loadTexture("mars.jpg");
    jupiterTex = loadTexture("jupiter.jpg");
    saturnTex = loadTexture("saturn.jpg");
    uranusTex = loadTexture("uranus.jpg");
    neptuneTex = loadTexture("neptune.jpg");
    backgroundTex = loadTexture("background.jpg");
    saturnRingTex = loadTexture("saturn_ring.png");

    planets.clear();

    // 행성 크기: 잘 보이게 1.2배 정도 (태양 대비 너무 작아 보이지 않게)
    float baseScale = 1.2f;

    float s_mer = 0.38f * baseScale;
    float s_ven = 0.95f * baseScale;
    float s_ear = 1.0f * baseScale;
    float s_mar = 0.53f * baseScale;
    float s_jup = 11.21f * baseScale;
    float s_sat = 9.45f * baseScale;
    float s_ura = 4.01f * baseScale;
    float s_nep = 3.88f * baseScale;

    // ★★★ [거리 대폭 수정] 태양 크기(30)를 고려하여 멀리 배치 ★★★
    // 거리: 60, 100, 140... 씩 띄움

    // 1. 수성 (거리 60)
    planets.push_back({ "mercury", 60.0f, s_mer, 365.0f / 88.0f, 1.0f / 58.65f, 0.03f, mercuryTex, (int)(s_mer * 10.0f), s_mer, true,
        "4,879 km", "58.6 Days", "88 Days", "Terrestrial Planet" });

    // 2. 금성 (거리 100)
    planets.push_back({ "venus", 100.0f, s_ven, 365.0f / 226.0f, 1.0f / -243.01f, 177.3f, venusTex, (int)(s_ven * 10.0f), s_ven, true,
        "12,104 km", "243 Days (Retrograde)", "225 Days", "Terrestrial Planet" });

    // 3. 지구 (거리 140)
    planets.push_back({ "earth", 140.0f, s_ear, 1.0f, 1.0f, 23.5f, earthTex, (int)(s_ear * 10.0f), s_ear, true,
        "12,742 km", "23.9 Hours", "365.25 Days", "Terrestrial Planet" });

    // 4. 화성 (거리 180)
    planets.push_back({ "mars", 180.0f, s_mar, 365.0f / 686.0f, 1.0f / 1.026f, 25.2f, marsTex, (int)(s_mar * 10.0f), s_mar, true,
        "6,779 km", "24.6 Hours", "687 Days", "Terrestrial Planet" });

    // 5. 목성 (거리 300 - 여기서부터 확 멀어짐)
    planets.push_back({ "jupiter", 300.0f, s_jup, 365.0f / 4343.5f * 5.0f, 1.0f / 0.41f, 3.1f, jupiterTex, (int)(s_jup * 10.0f), s_jup, true,
        "139,820 km", "9.9 Hours", "11.86 Years", "Gas Giant" });

    // 6. 토성 (거리 450)
    planets.push_back({ "saturn", 450.0f, s_sat, 365.0f / 10731.0f * 5.0f, 1.0f / 0.426f, 26.7f, saturnTex, (int)(s_sat * 10.0f), s_sat, true,
        "116,460 km", "10.7 Hours", "29.45 Years", "Gas Giant" });

    // 7. 천왕성 (거리 600)
    planets.push_back({ "uranus", 600.0f, s_ura, 365.0f / 30587.0f * 10.0f, 1.0f / -0.718f, 97.8f, uranusTex, (int)(s_ura * 10.0f), s_ura, true,
        "50,724 km", "17.2 Hours (Retrograde)", "84 Years", "Ice Giant" });

    // 8. 해왕성 (거리 750)
    planets.push_back({ "neptune", 750.0f, s_nep, 365.0f / 59787.0f * 10.0f, 1.0f / 0.67f, 28.3f, neptuneTex, (int)(s_nep * 10.0f), s_nep, true,
        "49,244 km", "16.1 Hours", "164.8 Years", "Ice Giant" });
}
// [추가] 특정 인덱스 행성의 현재 (x, y, z) 위치를 계산하여 반환하는 함수
glm::vec3 getPlanetPosition(int index) {
    if (index < 0 || index >= planets.size()) return glm::vec3(0, 0, 0);

    float angle = orbitTime * planets[index].orbitSpeed; // 현재 공전 각도
    // 행성은 (distance, 0, 0)에서 Y축 기준 회전함
    // 회전 행렬을 직접 계산하거나 삼각함수 사용
    // glm::rotate는 라디안 단위를 씁니다.

    // 단순화된 계산 (y축 회전)
    float rad = glm::radians(angle);
    float x = cos(rad) * planets[index].distance;
    float z = -sin(rad) * planets[index].distance;

    return glm::vec3(x, 0.0f, z);
}

void initAsteroids() {
    asteroids.clear();
    for (int i = 0; i < asteroidCount; ++i) {
        AsteroidData a;
        a.asteroidId = static_cast<std::uint32_t>(i + 1);
        float range = 450.0f;
        a.position = glm::vec3(
            (static_cast<float>(rand()) / RAND_MAX * 2.0f * range) - range,
            (static_cast<float>(rand()) / RAND_MAX * 2.0f * range) - range,
            (static_cast<float>(rand()) / RAND_MAX * 2.0f * range) - range
        );
        a.velocity = glm::vec3(
            (static_cast<float>(rand()) / RAND_MAX * 0.1f) - 0.05f,
            (static_cast<float>(rand()) / RAND_MAX * 0.1f) - 0.05f,
            (static_cast<float>(rand()) / RAND_MAX * 0.1f) - 0.05f
        );
        float r = static_cast<float>(rand()) / RAND_MAX;
        a.initialSize = 0.5f + r * 1.0f;
        a.size = a.initialSize;
        a.health = 2 + static_cast<int>(r * 3.0f);
        a.rotationSpeed = (static_cast<float>(rand()) / RAND_MAX * 4.0f) - 2.0f;
        a.rotationAxis = glm::normalize(glm::vec3(rand(), rand(), rand()));
        asteroids.push_back(a);
    }
    initAsteroidModel();
}

void spawnExplosion(glm::vec3 pos, glm::vec3 color, int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.position = pos;
        p.velocity = glm::normalize(glm::vec3(rand() % 100 - 50, rand() % 100 - 50, rand() % 100 - 50))
            * (0.2f + (rand() % 100 / 100.0f) * 0.3f);
        p.color = color;
        p.life = 1.0f;
        p.size = 0.2f + (rand() % 100 / 100.0f) * 0.3f;
        p.fadeSpeed = 0.02f + (rand() % 100 / 100.0f) * 0.02f;
        particles.push_back(p);
    }
}

void spawnTrail(glm::vec3 pos, glm::vec3 color) {
    Particle p;
    p.position = pos;
    p.velocity = glm::vec3((rand() % 100 - 50) / 400.0f, (rand() % 100 - 50) / 400.0f, (rand() % 100 - 50) / 400.0f);
    p.color = color;
    p.life = 1.0f;
    p.size = 0.3f;
    p.fadeSpeed = 0.05f;
    particles.push_back(p);
}

void updateParticles() {
    for (int i = 0; i < particles.size(); ) {
        particles[i].position += particles[i].velocity;
        particles[i].life -= particles[i].fadeSpeed;
        particles[i].size *= 0.95f;
        if (particles[i].life <= 0.0f) particles.erase(particles.begin() + i);
        else ++i;
    }
}

void updateMovement(glm::vec3& position, float yaw, float pitch,
    int forwardInput, int strafeInput, bool boosting) {
    glm::vec3 front;
    front.x = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(front);
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
    glm::vec3 moveDir = front * static_cast<float>(forwardInput)
        + right * static_cast<float>(strafeInput);

    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        float currentSpeed = 0.5f;
        glm::vec3 engineColor(0.2f, 0.6f, 1.0f);

        if (boosting) {
            currentSpeed = 1.5f;
            engineColor = glm::vec3(1.0f, 0.3f, 0.1f);
        }

        position += moveDir * currentSpeed;

        glm::vec3 shipBack = glm::normalize(front) * -1.5f;
        glm::vec3 engineOffset = glm::cross(glm::normalize(shipBack * -1.0f), glm::vec3(0, 1, 0)) * 0.5f;
        spawnTrail(position + shipBack + engineOffset, engineColor);
        spawnTrail(position + shipBack - engineOffset, engineColor);
    }
}

// [추가] 조준선(Crosshair) 그리기 함수
void drawCrosshair() {
    // 1인칭(1) 또는 3인칭(3)이 아닐 때는 그리지 않음
    if (cameraMode != 1 && cameraMode != 3) return;

    // 1. 2D 모드로 전환 (UI 그리기 위함)
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, width, 0, height);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // 2. 쉐이더 및 3D 설정 끄기
    glUseProgram(0);          // 쉐이더 해제
    glDisable(GL_DEPTH_TEST); // 깊이 테스트 끄기 (맨 앞에 보임)
    glDisable(GL_LIGHTING);   // 조명 끄기

    // 3. 조준선 그리기 (녹색 십자가)
    float cx = width / 2.0f;  // 화면 중심 X
    float cy = height / 2.0f; // 화면 중심 Y
    float size = 15.0f;       // 십자가 길이
    float gap = 5.0f;         // 중앙 빈 공간 (시야 확보용)

    glLineWidth(2.0f);        // 선 두께 설정
    glColor3f(0.0f, 1.0f, 0.0f); // 녹색 (Green)

    glBegin(GL_LINES);
    // 가로 선 (좌우)
    glVertex2f(cx - size, cy);
    glVertex2f(cx - gap, cy);
    glVertex2f(cx + gap, cy);
    glVertex2f(cx + size, cy);

    // 세로 선 (상하)
    glVertex2f(cx, cy - size);
    glVertex2f(cx, cy - gap);
    glVertex2f(cx, cy + gap);
    glVertex2f(cx, cy + size);
    glEnd();

    glLineWidth(1.0f); // 선 두께 원상복구

    // 4. 3D 설정 복구
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(shaderProgramID); // 쉐이더 다시 켜기

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

static bool canFireMissile(const MissileLauncher& launcher) {
    return launcher.ownerPlayerId != 0 && nextProjectileId != 0 &&
        (!launcher.hasFired || worldTick - launcher.lastFireTick >= missileFireDelayTicks);
}

void fireMissile() {
    if (!canFireMissile(hostMissileLauncher)) return;

    GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
    glm::vec3 cameraPos, cameraTarget, cameraUp(0, 1, 0);
    glm::vec3 shipDir;
    shipDir.x = sin(glm::radians(shipAngle)) * cos(glm::radians(shipPitch));
    shipDir.y = sin(glm::radians(shipPitch));
    shipDir.z = cos(glm::radians(shipAngle)) * cos(glm::radians(shipPitch));
    shipDir = glm::normalize(shipDir);

    if (cameraMode == 1) { cameraPos = shipPos + glm::vec3(0, 0.5, 0) + shipDir * 2.0f; cameraTarget = cameraPos + shipDir; }
    else if (cameraMode == 2) { cameraPos = glm::vec3(0, 100, 10); cameraTarget = glm::vec3(0, 0, 0); cameraUp = glm::vec3(0, 0, -1); }
    else { cameraPos = shipPos - (shipDir * 8.0f) + glm::vec3(0, 4, 0); cameraTarget = shipPos + shipDir * 5.0f; }

    glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, cameraUp);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)width / height, 0.1f, 5000.0f);

    // ★★★ [수정 1] 조준점 좌표 계산 (1인칭은 무조건 화면 중앙) ★★★
    float winX, winY;
    if (cameraMode == 1) {
        winX = (float)viewport[2] / 2.0f; // 화면 너비 절반
        winY = (float)viewport[3] / 2.0f; // 화면 높이 절반
    }
    else {
        winX = mouse_x;
        winY = (float)viewport[3] - mouse_y;
    }

    glm::vec3 nearPt = glm::unProject(glm::vec3(winX, winY, 0), view, proj, glm::vec4(viewport[0], viewport[1], viewport[2], viewport[3]));
    glm::vec3 farPt = glm::unProject(glm::vec3(winX, winY, 1), view, proj, glm::vec4(viewport[0], viewport[1], viewport[2], viewport[3]));
    glm::vec3 rayOrigin = cameraPos;
    glm::vec3 rayDir = glm::normalize(farPt - nearPt);

    glm::vec3 targetPos;
    bool hit = false;
    float minDist = 10000.0f;

    // (충돌 검사 로직은 기존 유지)
    for (const auto& a : asteroids) {
        if (a.size <= 0) continue;
        glm::vec3 aPos = a.position + (a.velocity * globalTime);
        glm::vec3 L = aPos - rayOrigin;
        float tca = dot(L, rayDir);
        if (tca < 0) continue;
        float d2 = dot(L, L) - tca * tca;
        float r2 = (a.size + 1.0f) * (a.size + 1.0f);
        if (d2 > r2) continue;
        float thc = sqrt(r2 - d2);
        float t0 = tca - thc;
        if (t0 < minDist) { minDist = t0; targetPos = aPos; hit = true; }
    }

    if (!hit) targetPos = rayOrigin + rayDir * 5000.0f;

    spawnMissile(hostMissileLauncher, shipPos, targetPos - shipPos);
}

void spawnMissile(MissileLauncher& launcher, const glm::vec3& origin, const glm::vec3& direction) {
    if (!canFireMissile(launcher)) return;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return;
    const float length = glm::length(direction);
    if (!std::isfinite(length) || length <= 0.0f) return;

    const glm::vec3 fireDir = direction / length;
    MissileData m{};
    m.projectileId = nextProjectileId;
    m.ownerPlayerId = launcher.ownerPlayerId;
    m.position = origin + fireDir * 2.0f;
    m.velocity = fireDir * missileSpeed;

    // 좌우 회전 (Y축 기준)
    m.yaw = glm::degrees(atan2(fireDir.x, fireDir.z));
    // 상하 회전 (X축 기준)
    m.pitch = glm::degrees(asin(glm::clamp(fireDir.y, -1.0f, 1.0f)));

    m.lifeTime = 0.0f;
    m.active = true;
    missiles.push_back(m);
    launcher.lastFireTick = worldTick;
    launcher.hasFired = true;
    ++nextProjectileId;
}


void initRing() {
    std::vector<float> data;
    const int segments = 72; // 원을 몇 조각으로 나눌지 (많을수록 부드러움)
    float innerRadius = 0.6f; // 안쪽 반지름 (행성보다 약간 커야 함)
    float outerRadius = 1.2f; // 바깥쪽 반지름

    for (int i = 0; i <= segments; ++i) {
        float angle = (float)i / segments * 2.0f * PI;
        float c = cosf(angle);
        float s = sinf(angle);

        // 안쪽 점 (Inner) -> 텍스처의 왼쪽(0.0)
        data.push_back(c * innerRadius); data.push_back(0.0f); data.push_back(s * innerRadius); // Pos
        data.push_back(0.0f); data.push_back(1.0f); data.push_back(0.0f);                       // Normal (위쪽)
        data.push_back(0.0f); data.push_back(0.0f);                                             // TexCoord (시작)

        // 바깥쪽 점 (Outer) -> 텍스처의 오른쪽(1.0)
        data.push_back(c * outerRadius); data.push_back(0.0f); data.push_back(s * outerRadius); // Pos
        data.push_back(0.0f); data.push_back(1.0f); data.push_back(0.0f);                       // Normal (위쪽)
        data.push_back(1.0f); data.push_back(0.0f);                                             // TexCoord (끝)
    }

    glGenVertexArrays(1, &ringVAO);
    glGenBuffers(1, &ringVBO);

    glBindVertexArray(ringVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ringVBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);

    // 쉐이더 속성 연결 (Pos, Normal, TexCoord)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

static float missileHitFraction(const glm::vec3& start, const glm::vec3& travel,
    const glm::vec3& center, float radius) {
    constexpr float noHit = std::numeric_limits<float>::infinity();
    const glm::vec3 offset = start - center;
    const float distanceFromSurface = glm::dot(offset, offset) - radius * radius;
    if (distanceFromSurface <= 0.0f) return 0.0f;

    const float approach = glm::dot(offset, travel);
    if (approach >= 0.0f) return noHit;
    const float travelSquared = glm::dot(travel, travel);
    const float discriminant = approach * approach - travelSquared * distanceFromSurface;
    if (discriminant < 0.0f) return noHit;
    // 선분 시작을 0, 끝을 1로 두고 구에 처음 들어가는 지점을 구한다.
    const float entry = distanceFromSurface / (-approach + std::sqrt(discriminant));
    return entry <= 1.0f ? entry : noHit;
}

static bool hostingNetworkGame = false;

static void resolveMissileHit(MissileData& missile, const glm::vec3& start) {
    float firstHit = std::numeric_limits<float>::infinity();
    PlanetData* hitPlanet = nullptr;
    AsteroidData* hitAsteroid = nullptr;
    glm::vec3 hitCenter{};
    for (int i = 0; i < static_cast<int>(planets.size()); ++i) {
        auto& planet = planets[i];
        if (!planet.active || planet.distance == 0.0f) continue;
        const glm::vec3 center = getPlanetPosition(i);
        const float hit = missileHitFraction(start, missile.velocity, center, planet.size + 0.5f);
        if (hit < firstHit) {
            firstHit = hit;
            hitPlanet = &planet;
            hitCenter = center;
        }
    }

    for (auto& asteroid : asteroids) {
        if (asteroid.health <= 0) continue;
        const glm::vec3 center = asteroid.position + asteroid.velocity * globalTime;
        const float hit = missileHitFraction(start, missile.velocity, center, asteroid.size + 0.3f);
        if (hit < firstHit) {
            firstHit = hit;
            hitPlanet = nullptr;
            hitAsteroid = &asteroid;
            hitCenter = center;
        }
    }

    if (!hitPlanet && !hitAsteroid) return;
    missile.active = false;
    if (hitPlanet) {
        auto& planet = *hitPlanet;
        planet.health -= 10;
        const glm::vec3 impact = start + missile.velocity * firstHit;
        spawnExplosion(impact, glm::vec3(0.1f, 0.5f, 0.2f), 5);
        if (planet.health > 0) {
            const float hpRatio = static_cast<float>(planet.health) / (planet.initialSize * 10.0f);
            planet.size = planet.initialSize * (0.5f + 0.5f * hpRatio);
        } else {
            planet.health = 0;
            planet.size = 0.0f;
            planet.active = false;
            spawnExplosion(hitCenter, glm::vec3(1.0f, 0.1f, 0.0f), 100);
            printf("Planet %s was destroyed!\n", planet.name.c_str());
        }
    } else {
        auto& asteroid = *hitAsteroid;
        --asteroid.health;
        spawnExplosion(hitCenter, glm::vec3(0.5f, 0.4f, 0.3f), 3);
        if (asteroid.health > 0) {
            const float hpRatio = static_cast<float>(asteroid.health) / (asteroid.initialSize * 3.0f);
            asteroid.size = asteroid.initialSize * (0.4f + 0.6f * hpRatio);
        } else {
            asteroid.size = 0.0f;
            spawnExplosion(hitCenter, glm::vec3(0.8f, 0.3f, 0.1f), 30);
        }
    }
}

void updateGameLogic() {
    for (auto missile = missiles.begin(); missile != missiles.end();) {
        if (!missile->active) {
            missile = missiles.erase(missile);
            continue;
        }

        const glm::vec3 start = missile->position;
        missile->position += missile->velocity;
        missile->lifeTime += worldTickSeconds;
        if (glm::length(missile->position) > missileRange) {
            missile = missiles.erase(missile);
            continue;
        }

        resolveMissileHit(*missile, start);
        ++missile;
    }
    asteroids.erase(std::remove_if(asteroids.begin(), asteroids.end(),
        [](const AsteroidData& a) { return a.size <= 0.0f; }), asteroids.end());

    if (!isGameCleared && !isSunDestroyed) { // 아직 클리어 안 했고 태양도 살아있다면
        int activePlanetCount = 0;
        for (const auto& p : planets) {
            if (p.distance != 0.0f && p.size > 0.001f) {
                activePlanetCount++;
            }
        }

        // 모든 행성이 파괴되었고, 아직 폭발이 시작되지 않았다면 -> 폭발 시작!
        if (activePlanetCount == 0 && !isSunExploding) {
            isSunExploding = true;
            printf("WARNING: SUN IS UNSTABLE!\n");
        }
    }

    // [추가] 태양 폭발 애니메이션 로직
    if (isSunExploding) {
        sunExplosionTimer += sunExplosionTimeStep; // 폭발 진행

        // 1. 지속적으로 태양 위치에서 파티클 생성 (점점 격렬하게)
        int particleCount = 5 + (int)(sunExplosionTimer * 2.0f);
        glm::vec3 sunColor = glm::mix(glm::vec3(1.0f, 0.5f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), sunExplosionTimer / 10.0f); // 주황 -> 흰색
        spawnExplosion(glm::vec3(0, 0, 0), sunColor, particleCount);

        // 2. 일정 시간이 지나면 태양 소멸 및 게임 클리어 처리
        if (sunExplosionTimer > 15.0f) { // 약 3초 정도 후 (타이머 속도에 따라 조절)
            isSunExploding = false;
            isSunDestroyed = true;
            isGameCleared = true; // 이때 텍스트가 뜨게 됨

            // 마지막 대폭발 이펙트
            spawnExplosion(glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), 500);
            printf("\n\n*** SUN DESTROYED! UNIVERSE IS DOOMED! ***\n\n");

            isRotating = false;
            isOrbiting = false;
        }
    }
}
bool isLeftButtonDown = false;
void Mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON) {
        if (state == GLUT_DOWN) {
            isLeftButtonDown = true;
            mouse_x = x;
            mouse_y = y;
        }
        else if (state == GLUT_UP) {
            isLeftButtonDown = false;
        }
    }


}

void drawPlanetInfo() {
    if (cameraMode != 4 || targetPlanetIndex == -1) return;
    if (targetPlanetIndex >= planets.size()) return;

    PlanetData& p = planets[targetPlanetIndex];

    // 1. 2D 설정
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, width, 0, height);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glUseProgram(0);

    // 2. 배경 박스 (좀 더 크게 조정)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.7f); // 좀 더 진하게

    float startX = 20.0f;
    float startY = height - 20.0f;
    float boxWidth = 300.0f;
    float boxHeight = 180.0f;

    glBegin(GL_QUADS);
    glVertex2f(startX, startY);
    glVertex2f(startX + boxWidth, startY);
    glVertex2f(startX + boxWidth, startY - boxHeight);
    glVertex2f(startX, startY - boxHeight);
    glEnd();
    glDisable(GL_BLEND);

    // 3. 실제 데이터 출력
    glColor3f(1.0f, 1.0f, 1.0f); // 흰색

    char buffer[256];
    float textX = startX + 15.0f;
    float textY = startY - 30.0f;
    float lineGap = 25.0f;

    // (1) 이름 (대문자 강조)
    glColor3f(0.3f, 0.8f, 1.0f); // 제목은 밝은 하늘색
    std::string nameUpper = p.name;
    std::transform(nameUpper.begin(), nameUpper.end(), nameUpper.begin(), ::toupper);
    sprintf(buffer, "=== %s ===", nameUpper.c_str());
    renderBitmapString(textX, textY, GLUT_BITMAP_HELVETICA_18, buffer);
    textY -= (lineGap + 5);

    glColor3f(0.9f, 0.9f, 0.9f); // 내용은 밝은 회색

    // (2) 행성 분류
    sprintf(buffer, "Type: %s", p.planetType.c_str());
    renderBitmapString(textX, textY, GLUT_BITMAP_HELVETICA_12, buffer);
    textY -= lineGap;

    // (3) 실제 지름
    sprintf(buffer, "Real Diameter: %s", p.realDiameter.c_str());
    renderBitmapString(textX, textY, GLUT_BITMAP_HELVETICA_18, buffer);
    textY -= lineGap;

    // (4) 자전 주기 (하루 길이)
    sprintf(buffer, "Day Length: %s", p.realDay.c_str());
    renderBitmapString(textX, textY, GLUT_BITMAP_HELVETICA_18, buffer);
    textY -= lineGap;

    // (5) 공전 주기 (1년 길이)
    sprintf(buffer, "Year Length: %s", p.realYear.c_str());
    renderBitmapString(textX, textY, GLUT_BITMAP_HELVETICA_18, buffer);
    textY -= lineGap;

    // (6) 자전축 기울기
    sprintf(buffer, "Axial Tilt: %.1f deg", p.axisTilt);
    renderBitmapString(textX, textY, GLUT_BITMAP_HELVETICA_18, buffer);
    textY -= (lineGap + 5);

    // (7) 현재 상태 (HP) - 게임 정보도 살짝 표시
    if (p.health > 50) glColor3f(0.2f, 1.0f, 0.2f);
    else if (p.health > 20) glColor3f(1.0f, 0.8f, 0.2f);
    else glColor3f(1.0f, 0.2f, 0.2f);

    sprintf(buffer, "[ Status: HP %d%% ]", (int)((float)p.health / (p.initialSize * 10.0f) * 100.0f));
    renderBitmapString(textX, textY, GLUT_BITMAP_HELVETICA_18, buffer);


    // 4. 복구
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(shaderProgramID);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void Motion(int x, int y) {
    // [수정] 1인칭 모드(cameraMode == 1)일 때만 shouldRotate가 true가 되며 커서 고정/숨김 로직이 실행됩니다.
    // 참고: isTyping 중에는 Motion이 호출되지 않도록 Keyboard 함수에서 막고 있습니다.

    bool shouldRotate = (cameraMode == 1 || cameraMode == 3 || isLeftButtonDown);

    if (firstMouse) { lastMouseX = x; lastMouseY = y; firstMouse = false; return; }

    if (shouldRotate) {
        mouse_x = x;
        mouse_y = y;
        float xoffset, yoffset;

        // 1인칭 모드일 때 마우스 커서를 중앙으로 워프시켜 무한 회전을 가능하게 합니다.
        if (cameraMode == 3 || cameraMode == 1) {
            int centerX = width / 2;
            int centerY = height / 2;

            xoffset = x - centerX;
            yoffset = y - centerY;

            // 중앙에 고정
            if (x != centerX || y != centerY) {
                glutWarpPointer(centerX, centerY);
            }
            // 1인칭 모드에서는 lastMouseX, Y는 더 이상 사용되지 않고 centerX, centerY가 기준이 됩니다.
        }
        else {
            // 그 외 모드에서는 기존 로직 (드래그)
            xoffset = x - lastMouseX;
            yoffset = y - lastMouseY;
            lastMouseX = x;
            lastMouseY = y;
        }

        // 오프셋이 0이 아니거나, 1인칭이 아닌 모드에서 회전 중일 때만 업데이트
        if (xoffset != 0.0f || yoffset != 0.0f) {
            shipAngle -= xoffset * mouseSensitivity;
            shipPitch -= yoffset * mouseSensitivity;
            if (shipPitch > 89.0f) shipPitch = 89.0f;
            if (shipPitch < -89.0f) shipPitch = -89.0f;
            glutPostRedisplay();
        }
    }
}
void drawScene() {
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(shaderProgramID);

    glm::vec3 cameraPos, cameraTarget, cameraUp(0, 1, 0);
    glm::vec3 shipDir;
    shipDir.x = sin(glm::radians(shipAngle)) * cos(glm::radians(shipPitch));
    shipDir.y = sin(glm::radians(shipPitch));
    shipDir.z = cos(glm::radians(shipAngle)) * cos(glm::radians(shipPitch));
    shipDir = glm::normalize(shipDir);

    if (cameraMode == 1) { cameraPos = shipPos + glm::vec3(0, 0.5, 0) + shipDir * 2.0f; cameraTarget = cameraPos + shipDir; }
    else if (cameraMode == 2) { cameraPos = glm::vec3(0, 800, 10); cameraTarget = glm::vec3(0, 0, 0); cameraUp = glm::vec3(0, 0, -1); }
    else if (cameraMode == 4 && targetPlanetIndex != -1) {
        // 타겟 행성의 현재 위치 계산
        glm::vec3 planetPos = getPlanetPosition(targetPlanetIndex);

        // 카메라는 행성 위치에서 (5, 3, 5) 정도 떨어진 곳에 배치
        // 행성 크기에 비례해서 멀리 떨어지게 하면 더 좋습니다.
        float distOffset = planets[targetPlanetIndex].size * 4.0f + 5.0f;

        cameraPos = planetPos + glm::vec3(distOffset, distOffset * 0.5f, distOffset);
        cameraTarget = planetPos; // 카메라는 행성을 바라봄
    }
    else if (cameraMode == 5) {
        // 1. 카메라 위치: 태양(0,0,0)의 바로 위
        // 태양 반지름이 30이니까, Y=150 정도면 적당히 높습니다.
        cameraPos = glm::vec3(0.0f, 150.0f, 0.0f);

        // 2. 바라보는 곳: 시간(globalTime)에 따라 회전
        float rotateSpeed = globalTime * 0.2f; // 회전 속도 (숫자가 작을수록 느림)
        float viewRadius = 1000.0f; // 멀리 바라봄

        // 원을 그리며 회전하는 타겟 좌표 계산
        cameraTarget.x = cos(glm::radians(rotateSpeed)) * viewRadius;
        cameraTarget.z = sin(glm::radians(rotateSpeed)) * viewRadius;

        // Y값을 0보다 낮게(-200) 주면 살짝 아래를 내려다보게 되어 궤도가 더 잘 보입니다.
        cameraTarget.y = -200.0f;

        cameraUp = glm::vec3(0, 1, 0);
    }
    else { cameraPos = shipPos - (shipDir * 8.0f) + glm::vec3(0, 4, 0); cameraTarget = shipPos + shipDir * 5.0f; }

    glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, cameraUp);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)width / height, 0.1f, 5000.0f);

    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(proj));
    glUniform3f(lightPosLoc, 0, 0, 0);
    glUniform3f(viewPosLoc, cameraPos.x, cameraPos.y, cameraPos.z);

    // 배경
    glUniform1i(isSunLoc, 1);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f))));
    glUniform4f(colorLoc, 1, 1, 1, 1);
    glBindTexture(GL_TEXTURE_2D, backgroundTex);
    glBindVertexArray(sphereVAO);
    glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);

    // [1] 별 그리기
    // ---------------------------------------------------------
    glUniform1i(isSunLoc, 1);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
    glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, whiteTex);

    glPointSize(2.0f);
    glBindVertexArray(starVAO); // ★ 여기서 '별' 모델을 선택함
    glDrawArrays(GL_POINTS, 0, starCount); // 별 그리기

    // ---------------------------------------------------------
    // [2] ★★★ 핵심 수정 사항 ★★★
    // 별 그리기가 끝났으니, 다시 '구(행성)' 모델로 돌아와야 합니다!
    // 이 줄이 없으면 태양을 그릴 때 꺼집니다.
    // ---------------------------------------------------------
    glBindVertexArray(sphereVAO);

    // ★★★ 여기가 제일 중요합니다! ★★★
    // glDrawElements가 아니라 "glDrawArrays"여야 합니다.
    // 또한 sphereIndexCount가 아니라 "starCount"여야 합니다.
    glDrawArrays(GL_POINTS, 0, starCount);


    if (!isSunDestroyed) { // [수정] 태양이 파괴되지 않았을 때만 그림

        float sunScale = 30.0f; // 기본 크기

        if (isSunExploding) {
            // [추가] 폭발 중이면 크기가 기하급수적으로 커짐
            // 진동 효과 (Shake) 추가
            float shakeX = (rand() % 100 - 50) / 50.0f * (sunExplosionTimer * 0.5f);
            float shakeY = (rand() % 100 - 50) / 50.0f * (sunExplosionTimer * 0.5f);
            float shakeZ = (rand() % 100 - 50) / 50.0f * (sunExplosionTimer * 0.5f);

            // 시간이 지날수록 크기 증가 (초반엔 천천히, 나중엔 빠르게)
            sunScale = 30.0f + (sunExplosionTimer * sunExplosionTimer * 2.0f);

            glm::mat4 sunModel = glm::translate(glm::mat4(1.0f), glm::vec3(shakeX, shakeY, shakeZ));
            sunModel = glm::rotate(sunModel, glm::radians(rotateTime * 0.2f), glm::vec3(0, 1, 0));
            sunModel = glm::scale(sunModel, glm::vec3(sunScale));

            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(sunModel));

            // 폭발 중에는 색상이 점점 하얗게 변함
            float whiteness = std::min(sunExplosionTimer / 10.0f, 1.0f);
            glUniform4f(colorLoc, 1.0f, 1.0f - whiteness * 0.5f, 1.0f - whiteness * 0.5f, 1.0f);
        }
        else {
            // 평상시 태양
            glm::mat4 sunModel = glm::rotate(glm::mat4(1.0f), glm::radians(rotateTime * 0.2f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(glm::scale(sunModel, glm::vec3(30.0f))));
            glUniform4f(colorLoc, 1, 1, 1, 1);
        }

        glBindTexture(GL_TEXTURE_2D, sunTex);
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);
    }

    // 행성
    for (auto& p : planets) {
        glUniform1i(isSunLoc, 1); // 궤도선
        glBindTexture(GL_TEXTURE_2D, whiteTex); // [수정] 흰색 텍스처 사용
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
        glUniform4f(colorLoc, 0.2f, 0.2f, 0.4f, 1.0f);
        glBegin(GL_LINE_LOOP);
        for (int a = 0; a < 360; a++) glVertex3f(cos(glm::radians((float)a)) * p.distance, 0, sin(glm::radians((float)a)) * p.distance);
        glEnd();
        if (p.size < 0.001f) continue;
        glUniform1i(isSunLoc, 0); // 행성 본체
        // [수정] 공전 계산: globalTime -> orbitTime 사용
        glm::mat4 pModel = glm::rotate(glm::mat4(1.0f), glm::radians(orbitTime * p.orbitSpeed), glm::vec3(0, 1, 0));
        pModel = glm::translate(pModel, glm::vec3(p.distance, 0, 0));
        glm::mat4 earthPos = pModel; // 달 위치 저장용

        // ★★★ [수정 파트 시작] 자전축 기울기 및 자전 적용 ★★★

      // [중요] 기울기를 먼저 적용해야 올바르게 돕니다.
        // 1. 자전축 기울기 (Z축 회전)
        pModel = glm::rotate(pModel, glm::radians(p.axisTilt), glm::vec3(0, 0, 1));

        // 2. 자전 (Y축 회전)
        // 표의 데이터가 반영된 자전 속도 적용
        pModel = glm::rotate(pModel, glm::radians(rotateTime * p.selfRotateSpeed * planetSpinMultiplier), glm::vec3(0, 1, 0));

        // 3. 크기 적용
        pModel = glm::scale(pModel, glm::vec3(p.size));

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(pModel));
        glUniform4f(colorLoc, 1, 1, 1, 1);
        glBindTexture(GL_TEXTURE_2D, p.textureID);
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);

        if (p.name == "earth" && p.size > 0.0f) {
            glm::mat4 mModel = glm::rotate(earthPos, glm::radians(orbitTime * 3.0f), glm::vec3(0, 1, 0));
            mModel = glm::translate(mModel, glm::vec3(3.0f, 0, 0)); // 달과 지구 사이 거리도 살짝 벌림 (1.0 -> 3.0)
            mModel = glm::scale(mModel, glm::vec3(0.25f)); // 달 크기 조절

            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(mModel));
            glBindTexture(GL_TEXTURE_2D, moonTex);
            glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);
        }
        if (p.name == "saturn" && p.size > 0.0f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);

            glUniform1i(isSunLoc, 1); // 조명 무시 (밝게)

            // 고리 크기 설정
            glm::mat4 rModel = glm::scale(pModel, glm::vec3(2.0f, 2.0f, 2.0f)); // 고리 크기 배율
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(rModel));

            glUniform4f(colorLoc, 1.2f, 1.2f, 1.2f, 0.8f); // 약간 투명하게

            glBindTexture(GL_TEXTURE_2D, saturnRingTex);
            glBindVertexArray(ringVAO);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, (72 + 1) * 2);

            glUniform1i(isSunLoc, 0); // 조명 복구

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glBindVertexArray(sphereVAO); // 다시 구 모델로 복구
        }
    }

    // 운석
    glUniform1i(isSunLoc, 0);
    glBindVertexArray(asteroidVAO);
    glBindTexture(GL_TEXTURE_2D, moonTex); // 운석 텍스처
    for (const auto& a : asteroids) {
        if (a.size <= 0.001f) continue;
        glm::mat4 aModel = glm::translate(glm::mat4(1.0f), a.position + a.velocity * globalTime);
        aModel = glm::rotate(aModel, glm::radians(globalTime * a.rotationSpeed), a.rotationAxis);
        aModel = glm::scale(aModel, glm::vec3(a.size));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(aModel));

        float dmgRatio = 1.0f - (a.size / a.initialSize);
        glm::vec3 tint = glm::mix(glm::vec3(1, 1, 1), glm::vec3(1, 0, 0), dmgRatio * 1.5f);
        glUniform4f(colorLoc, tint.r, tint.g, tint.b, 1.0f);
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);
    }

    // ================== [수정됨] 미사일 그리기 (그럴듯한 모양) ==================
    glUniform1i(isSunLoc, 1);
    glBindVertexArray(sphereVAO);
    glBindTexture(GL_TEXTURE_2D, whiteTex); // [수정] 흰색 텍스처 사용

    for (const auto& m : missiles) {
        // 1. 미사일의 진행 방향(Direction) 계산
        glm::vec3 dir = glm::normalize(m.velocity);

        // 2. 진행 방향을 바라보는 회전 행렬(Model Matrix) 생성 (★핵심★)
        // glm::lookAt은 '카메라'가 보는 행렬이므로, 이를 '뒤집으면(inverse)' 물체가 그 방향을 보는 행렬이 됩니다.
        // 위치(m.position)에서 -> 목표지점(m.position + dir)을 바라보게 만듦
        // 등(Up)은 항상 (0,1,0) 하늘을 향하게 해서 기우뚱하지 않게 함
        glm::mat4 missileModel = glm::inverse(glm::lookAt(m.position, m.position + dir, glm::vec3(0.0f, 1.0f, 0.0f)));

        // 3. 모델 방향 보정 (필요한 경우)
        // 미사일 모델이 원래 -Z(앞)가 아니라 +Z(뒤)를 보고 만들어졌다면 거꾸로 날아갑니다.
        // 만약 미사일이 꼬리부터 날아가면 아래 주석을 풀어서 180도 돌려주세요.
        missileModel = glm::rotate(missileModel, glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        // --- 미사일 몸체 ---
        glm::mat4 bodyModel = glm::scale(missileModel, glm::vec3(0.1f, 0.1f, 0.8f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(bodyModel));
        glUniform4f(colorLoc, 0.2f, 0.8f, 1.0f, 1.0f);
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);

        // --- 미사일 탄두 ---
        glm::mat4 headModel = glm::translate(missileModel, glm::vec3(0.0f, 0.0f, 0.8f)); // 몸체 앞쪽으로(Z축)
        // *주의: 여기서 translate는 이미 회전된 축 기준(Local)으로 이동하므로 Z축으로 이동하면 됨
        headModel = glm::scale(headModel, glm::vec3(0.1f, 0.1f, 0.3f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(headModel));
        glUniform4f(colorLoc, 1.0f, 0.2f, 0.0f, 1.0f);
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);

        // --- 미사일 날개 ---
        for (int i = 0; i < 4; ++i) {
            glm::mat4 finModel = glm::rotate(missileModel, glm::radians(i * 90.0f), glm::vec3(0, 0, 1));
            finModel = glm::translate(finModel, glm::vec3(0.15f, 0.0f, -0.6f));
            finModel = glm::scale(finModel, glm::vec3(0.15f, 0.02f, 0.3f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(finModel));
            glUniform4f(colorLoc, 0.6f, 0.6f, 0.6f, 1.0f);
            glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);
        }
    }

    // ================== [수정됨] 우주선 그리기 (그럴듯한 모양) ==================
    if (cameraMode != 1) {
        glBindTexture(GL_TEXTURE_2D, whiteTex); // [수정] 흰색 텍스처 사용

        // 1. 우주선 기본 변환 행렬 (위치, 회전 적용)
        glm::mat4 shipBaseModel = glm::mat4(1.0f);
        shipBaseModel = glm::translate(shipBaseModel, shipPos);
        shipBaseModel = glm::rotate(shipBaseModel, glm::radians(shipAngle), glm::vec3(0.0f, 1.0f, 0.0f));
        // 위아래 회전 (Pitch) 적용
        shipBaseModel = glm::rotate(shipBaseModel, glm::radians(-shipPitch), glm::vec3(1.0f, 0.0f, 0.0f));
        shipBaseModel = glm::scale(shipBaseModel, glm::vec3(0.5f)); // 전체 크기 조절

        // --- 본체 (길쭉한 몸통) ---
        glm::mat4 bodyModel = glm::scale(shipBaseModel, glm::vec3(0.7f, 0.5f, 2.5f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(bodyModel));
        glUniform4f(colorLoc, 0.6f, 0.6f, 0.7f, 1.0f); // 회색 본체
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);

        // --- 조종석 (파란색 유리창 느낌) ---
        glm::mat4 cockpitModel = glm::translate(shipBaseModel, glm::vec3(0.0f, 0.3f, 0.5f)); // 약간 위쪽, 앞쪽으로
        cockpitModel = glm::scale(cockpitModel, glm::vec3(0.4f, 0.3f, 0.8f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(cockpitModel));
        glUniform4f(colorLoc, 0.2f, 0.6f, 1.0f, 1.0f); // 파란색 조종석
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);

        // --- 날개 (양옆으로 넓게) ---
        // 왼쪽 날개
        glm::mat4 leftWingModel = glm::translate(shipBaseModel, glm::vec3(-1.2f, 0.0f, -0.5f));
        leftWingModel = glm::scale(leftWingModel, glm::vec3(1.5f, 0.1f, 1.2f));
        leftWingModel = glm::rotate(leftWingModel, glm::radians(20.0f), glm::vec3(0, 0, 1)); // 약간 기울임
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(leftWingModel));
        glUniform4f(colorLoc, 0.7f, 0.2f, 0.2f, 1.0f); // 빨간색 날개
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);

        // 오른쪽 날개 (대칭)
        glm::mat4 rightWingModel = glm::translate(shipBaseModel, glm::vec3(1.2f, 0.0f, -0.5f));
        rightWingModel = glm::scale(rightWingModel, glm::vec3(1.5f, 0.1f, 1.2f));
        rightWingModel = glm::rotate(rightWingModel, glm::radians(-20.0f), glm::vec3(0, 0, 1)); // 반대로 기울임
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(rightWingModel));
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0); // 색상은 위에서 설정한 빨간색 그대로

        // --- 엔진 (뒤쪽에 두 개) ---
        for (int i = -1; i <= 1; i += 2) {
            glm::mat4 engineModel = glm::translate(shipBaseModel, glm::vec3(0.6f * i, 0.0f, -1.5f)); // 양옆, 뒤쪽
            engineModel = glm::scale(engineModel, glm::vec3(0.3f, 0.3f, 0.8f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(engineModel));
            glUniform4f(colorLoc, 0.3f, 0.3f, 0.3f, 1.0f); // 진한 회색 엔진
            glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);

            // 엔진 노즐 (주황색 불꽃 나오는 부분)
            glm::mat4 nozzleModel = glm::translate(engineModel, glm::vec3(0.0f, 0.0f, -1.0f)); // 엔진 뒤쪽
            nozzleModel = glm::scale(nozzleModel, glm::vec3(0.8f, 0.8f, 0.2f)); // 약간 작고 납작하게
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(nozzleModel));
            glUniform4f(colorLoc, 1.0f, 0.5f, 0.0f, 1.0f); // 주황색 노즐
            glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);
        }
    }

    // 파티클
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glUniform1i(isSunLoc, 1);
    glBindVertexArray(sphereVAO);
    glBindTexture(GL_TEXTURE_2D, whiteTex); // [수정] 흰색 텍스처 사용
    for (const auto& p : particles) {
        glm::mat4 pModel = glm::scale(glm::translate(glm::mat4(1.0f), p.position), glm::vec3(p.size));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(pModel));
        glUniform4f(colorLoc, p.color.r, p.color.g, p.color.b, p.life);
        glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_INT, 0);
    }
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    if (isGameCleared) {
        // 1. 2D 모드로 전환
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        gluOrtho2D(0, width, 0, height);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glUseProgram(0); // 쉐이더 끄기

        // ★★★ [해결책] 조명과 텍스처를 꺼야 색상이 제대로 나옵니다 ★★★
        glDisable(GL_LIGHTING);   // 조명 끄기 (이게 켜져 있으면 검게 나옴)
        glDisable(GL_TEXTURE_2D); // 텍스처 끄기 (혹시 텍스처가 섞이는 것 방지)

        // 2. 텍스트 설정 및 출력
        glColor3f(1.0f, 0.0f, 0.0f); // 빨간색 (이제 잘 보일 겁니다)

        float cx = width / 2.0f;
        float cy = height / 2.0f;

        std::string winText = "G A M E   C L E A R E D !";
        glRasterPos2f(cx - 140, cy);
        renderBitmapString(cx - 140, cy, GLUT_BITMAP_TIMES_ROMAN_24, winText.c_str());

        // 3. 3D 설정으로 복구 (다시 켜주는 것을 잊지 마세요)
        glEnable(GL_TEXTURE_2D); // 텍스처 다시 켜기 (필요하다면)
        glEnable(GL_LIGHTING);   // 조명 다시 켜기
        glEnable(GL_DEPTH_TEST);

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glUseProgram(shaderProgramID);
    }
    drawCrosshair();
    drawPlanetInfo();
    drawInputBox();
    glutSwapBuffers();
}

void Keyboard(unsigned char key, int x, int y) {
    // 1. 입력 모드일 때 (글자 타이핑 중)
    if (isTyping) {
        if (key == 13) { // [Enter] 키: 검색 실행
            bool found = false;
            for (int i = 0; i < planets.size(); ++i) {
                if (planets[i].name == inputString) {
                    targetPlanetIndex = i;
                    cameraMode = 4; // 행성 추적 모드
                    found = true;
                    break;
                }
            }
            if (!found) {
                printf("Planet '%s' not found!\n", inputString.c_str());
                // 못 찾았어도 일단 창은 닫거나, 혹은 유지하거나 선택 가능
                // 여기선 그냥 닫겠습니다.
            }
            isTyping = false; // 입력창 닫기
            inputString = ""; // 내용 초기화
        }
        else if (key == 8) { // [Backspace] 키: 글자 지우기
            if (inputString.length() > 0) {
                inputString.pop_back();
            }
        }
        else if (key == 27) { // [ESC] 키: 취소
            isTyping = false;
            inputString = "";
        }
        else if (key >= 32 && key <= 126) { // 일반 문자 입력
            inputString += (char)key;
        }
        return; 
    }

    if (key == 27) exit(0);

    if (key == 'v' || key == 'V') {
        isTyping = true;
        inputString = ""; 
        keyState[key] = false;
        return;
    }

    if (key >= 'A' && key <= 'Z') key = key + ('a' - 'A');

    keyState[key] = true;

    if (key == 'y') isRotating = !isRotating;
    if (key == 'u') isOrbiting = !isOrbiting;
    if (key == '1')
    {
        cameraMode = 1;
        glutSetCursor(GLUT_CURSOR_NONE);
    }
    if (key == '2') cameraMode = 2;
    if (key == '3')
    {
        cameraMode = 3;
        glutSetCursor(GLUT_CURSOR_NONE);
    }
    if (key == 't' || key == 'T') {
        cameraMode = 5; 
        glutSetCursor(GLUT_CURSOR_NONE); 
        targetPlanetIndex = -1; 
    }
}

void KeyboardUp(unsigned char key, int x, int y) {
    if (key >= 'A' && key <= 'Z') key = key + ('a' - 'A');
    keyState[key] = false;
}

void make_vertexShaders() {
    GLchar* vertexSource = filetobuf("vertex3.glsl");
    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, NULL);
    glCompileShader(vertexShader);
    free(vertexSource);
}

void make_fragmentShaders() {
    GLchar* fragmentSource = filetobuf("fragment3.glsl");
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
    glCompileShader(fragmentShader);
    free(fragmentSource);
}

GLuint make_shaderProgram() {
    GLuint shaderID = glCreateProgram();
    glAttachShader(shaderID, vertexShader);
    glAttachShader(shaderID, fragmentShader);
    glLinkProgram(shaderID);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glUseProgram(shaderID);
    return shaderID;
}
GLvoid Reshape(int w, int h)
{
    width = w;
    height = h;
    glViewport(0, 0, w, h);
}

// 서버 접속과 참가 승인 정보를 보관한다.
ConnectionCheck connection;
// 공용 전송 모듈이 가져가기 전까지 최신 입력 본문 하나를 보관한다.
std::vector<char> pendingInputBody;
// 별도 입력 구조체 대신 Protocol.h의 공용 자료형을 사용한다.
static Protocol::PlayerInput clientInput{};
// 입력을 기록한 클라이언트 업데이트 번호다. 실제 시계 시간이 아니다.
std::uint32_t clientTick = 0;

// 현재 입력을 전송 규칙에 맞는 14바이트 본문으로 만든다.
// 실제 소켓 송신은 공용 전송 모듈이 처리한다.
bool sendInput() {
    // 허용하지 않은 버튼 비트, 유효하지 않은 실수, 범위를 벗어난 각도는 거절한다.
    if ((clientInput.buttons & ~Protocol::INPUT_MASK) != 0 ||
        !std::isfinite(clientInput.yaw) || !std::isfinite(clientInput.pitch) ||
        clientInput.yaw < 0.0f || clientInput.yaw >= 360.0f ||
        clientInput.pitch < -89.0f || clientInput.pitch > 89.0f) {
        return false;
    }

    // 실수의 값이 아닌 비트 모양을 복사하려면 두 자료형이 모두 4바이트여야 한다.
    static_assert(sizeof(float) == sizeof(std::uint32_t), "Expected 32-bit float");
    std::uint32_t yawBits = 0;
    std::uint32_t pitchBits = 0;
    // float를 정수로 형변환하지 않고, 원래 실수의 비트 표현을 그대로 복사한다.
    std::memcpy(&yawBits, &clientInput.yaw, sizeof(yawBits));
    std::memcpy(&pitchBits, &clientInput.pitch, sizeof(pitchBits));

    // htonl은 4바이트, htons는 2바이트 값을 네트워크 바이트 순서로 바꾼다.
    const std::uint32_t tick = htonl(clientInput.clientTick);
    const std::uint16_t buttons = htons(clientInput.buttons);
    yawBits = htonl(yawBits);
    pitchBits = htonl(pitchBits);

    // 아직 가져가지 않은 이전 입력은 최신 상태로 교체한다.
    // 본문 배치: 업데이트 번호 0~3, 버튼 4~5, 좌우 각도 6~9, 상하 각도 10~13.
    // 구조체에 생길 수 있는 패딩을 제외하고 필드만 순서대로 기록한다.
    pendingInputBody.resize(Protocol::PLAYER_INPUT_BODY_SIZE);
    std::memcpy(pendingInputBody.data(), &tick, sizeof(tick));
    std::memcpy(pendingInputBody.data() + 4, &buttons, sizeof(buttons));
    std::memcpy(pendingInputBody.data() + 6, &yawBits, sizeof(yawBits));
    std::memcpy(pendingInputBody.data() + 10, &pitchBits, sizeof(pitchBits));
    return true;
}

// 공용 전송 모듈이 게임 시작 승인(MATCH_START) 후 게임 스레드에서 호출할 연결점이다.
// 전송 모듈이 실제 순번을 전달하면 기존 encodeFrame()으로 12바이트 헤더를 붙인다.
// 반환된 프레임의 소켓 송신과 부분 송신 처리는 전송 모듈이 담당한다.
bool takeClientInputFrame(std::uint32_t sequence, std::vector<char>& frame) {
    if (pendingInputBody.empty() || sequence == 0) return false;
    if (!encodeFrame(Protocol::MessageType::PLAYER_INPUT,
        sequence, pendingInputBody, frame)) {
        return false;
    }
    // 전송 모듈에 넘긴 본문은 비워 같은 입력을 다시 가져가지 않게 한다.
    pendingInputBody.clear();
    return true;
}

// 스냅샷과 참가 입력에서 같은 기체 좌표와 방향 기준을 사용한다.
Protocol::PlayerState captureLocalPlayerState(std::uint32_t playerId) {
    float yaw = std::fmod(shipAngle, 360.0f);
    if (yaw < 0.0f) yaw += 360.0f;
    if (yaw == 360.0f) yaw = 0.0f; // 작은 음수가 반올림되면 360이 될 수 있다.

    return { playerId, { shipPos.x, shipPos.y, shipPos.z }, yaw, shipPitch };
}

std::vector<Protocol::ProjectileState> captureActiveProjectiles() {
    std::vector<Protocol::ProjectileState> projectiles;
    projectiles.reserve(missiles.size());
    for (const auto& missile : missiles) {
        if (!missile.active) continue;

        // 게임은 틱당 이동량을 쓰고 스냅샷은 초당 속도를 쓴다.
        const glm::vec3 velocityPerSecond = missile.velocity / worldTickSeconds;
        projectiles.push_back({
            missile.projectileId, missile.ownerPlayerId,
            { missile.position.x, missile.position.y, missile.position.z },
            { velocityPerSecond.x, velocityPerSecond.y, velocityPerSecond.z },
            missile.lifeTime
        });
    }
    return projectiles;
}

std::vector<Protocol::PlanetState> capturePlanetStates() {
    std::vector<Protocol::PlanetState> states;
    states.reserve(planets.size());
    constexpr float timeUnitsPerSecond = planetTimeStep / worldTickSeconds;
    // 수성부터 해왕성까지 순서가 고정되어 있고 파괴된 행성도 목록에 남는다.
    for (std::size_t i = 0; i < planets.size(); ++i) {
        const auto& planet = planets[i];
        states.push_back({
            static_cast<std::uint32_t>(i + 1), planet.distance, planet.size, planet.initialSize,
            planet.orbitSpeed * timeUnitsPerSecond,
            planet.selfRotateSpeed * planetSpinMultiplier * timeUnitsPerSecond,
            planet.axisTilt, planet.health, static_cast<std::uint8_t>(planet.active)
        });
    }
    return states;
}

struct WorldSnapshot {
    Protocol::WorldSnapshotHeader header;
    std::vector<Protocol::PlayerState> players;
    std::vector<Protocol::ProjectileState> projectiles;
    std::vector<Protocol::PlanetState> planets;
};

WorldSnapshot captureWorldSnapshot() {
    WorldSnapshot snapshot{};
    auto& header = snapshot.header;
    auto& world = header.world;
    header.serverTick = static_cast<std::uint32_t>(worldTick);
    world.phase = Protocol::WorldPhase::Playing;
    if (isGameCleared) world.phase = Protocol::WorldPhase::Cleared;
    else if (isSunExploding) world.phase = Protocol::WorldPhase::SunExploding;
    world.rotating = isRotating;
    world.orbiting = isOrbiting;

    // 각 시계가 실제로 진행한 시뮬레이션 시간을 초 단위로 기록한다.
    world.globalTime = static_cast<float>(worldTick) * worldTickSeconds;
    constexpr float secondsPerPlanetTimeUnit = worldTickSeconds / planetTimeStep;
    world.rotateTime = rotateTime * secondsPerPlanetTimeUnit;
    world.orbitTime = orbitTime * secondsPerPlanetTimeUnit;
    world.sunExplosionTime = sunExplosionTimer * (worldTickSeconds / sunExplosionTimeStep);

    snapshot.players.push_back(captureLocalPlayerState(Protocol::HOST_PLAYER_ID));
    snapshot.projectiles = captureActiveProjectiles();
    snapshot.planets = capturePlanetStates();
    header.playerCount = static_cast<std::uint32_t>(snapshot.players.size());
    header.projectileCount = static_cast<std::uint32_t>(snapshot.projectiles.size());
    header.planetCount = static_cast<std::uint32_t>(snapshot.planets.size());
    return snapshot;
}

static void appendSnapshotU32(std::vector<char>& bytes, std::uint32_t value) {
    const std::uint32_t networkValue = htonl(value);
    const char* first = reinterpret_cast<const char*>(&networkValue);
    bytes.insert(bytes.end(), first, first + sizeof(networkValue));
}

static bool appendSnapshotFloats(std::vector<char>& bytes, std::initializer_list<float> values) {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559,
        "Snapshot requires IEEE 754 binary32");
    for (float value : values) {
        if (!std::isfinite(value)) return false;
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        appendSnapshotU32(bytes, bits);
    }
    return true;
}

// WORLD_SNAPSHOT 논리 본문 순서: 헤더, 기체, 활성 미사일, 행성. 조각 헤더는 전송 시 붙인다.
bool encodeSnapshotBody(const WorldSnapshot& snapshot, std::vector<char>& body) {
    const auto& header = snapshot.header;
    const auto& world = header.world;
    if (header.playerCount != snapshot.players.size() ||
        header.projectileCount != snapshot.projectiles.size() ||
        header.planetCount != snapshot.planets.size() ||
        header.projectileCount > Protocol::MAX_PROJECTILES || header.planetCount > Protocol::MAX_PLANETS) {
        return false;
    }
    const std::uint64_t bodySize = Protocol::WORLD_SNAPSHOT_HEADER_SIZE
        + std::uint64_t(header.playerCount) * Protocol::PLAYER_STATE_SIZE
        + std::uint64_t(header.projectileCount) * Protocol::PROJECTILE_STATE_SIZE
        + std::uint64_t(header.planetCount) * Protocol::PLANET_STATE_SIZE;
    if (bodySize > UINT32_MAX || bodySize > body.max_size()) return false;

    if (world.phase > Protocol::WorldPhase::Cleared || world.rotating > 1 || world.orbiting > 1 ||
        world.globalTime < 0 || world.rotateTime < 0 || world.orbitTime < 0 || world.sunExplosionTime < 0) {
        return false;
    }
    std::vector<char> bytes;
    bytes.reserve(static_cast<std::size_t>(bodySize));
    appendSnapshotU32(bytes, header.serverTick);
    bytes.push_back(static_cast<char>(world.phase));
    bytes.push_back(static_cast<char>(world.rotating));
    bytes.push_back(static_cast<char>(world.orbiting));
    if (!appendSnapshotFloats(bytes, { world.globalTime, world.rotateTime, world.orbitTime, world.sunExplosionTime })) {
        return false;
    }
    appendSnapshotU32(bytes, header.playerCount);
    appendSnapshotU32(bytes, header.projectileCount);
    appendSnapshotU32(bytes, header.planetCount);

    std::unordered_set<std::uint32_t> seenIds;
    for (const auto& player : snapshot.players) {
        if (player.playerId == 0 || !seenIds.insert(player.playerId).second ||
            player.yaw < 0 || player.yaw >= 360 || player.pitch < -89 || player.pitch > 89) return false;
        appendSnapshotU32(bytes, player.playerId);
        if (!appendSnapshotFloats(bytes, { player.position.x, player.position.y, player.position.z,
            player.yaw, player.pitch })) return false;
    }
    seenIds.clear();
    for (const auto& projectile : snapshot.projectiles) {
        if (projectile.projectileId == 0 || !seenIds.insert(projectile.projectileId).second ||
            projectile.ownerPlayerId == 0 || projectile.lifeTime < 0) return false;
        appendSnapshotU32(bytes, projectile.projectileId);
        appendSnapshotU32(bytes, projectile.ownerPlayerId);
        if (!appendSnapshotFloats(bytes, { projectile.position.x, projectile.position.y, projectile.position.z,
            projectile.velocity.x, projectile.velocity.y, projectile.velocity.z, projectile.lifeTime })) return false;
    }
    seenIds.clear();
    for (const auto& planet : snapshot.planets) {
        if (planet.planetId == 0 || planet.planetId > Protocol::MAX_PLANETS || !seenIds.insert(planet.planetId).second ||
            planet.active > 1 || planet.health < 0 || planet.distance < 0 || planet.size < 0 || planet.initialSize < 0) {
            return false;
        }
        appendSnapshotU32(bytes, planet.planetId);
        if (!appendSnapshotFloats(bytes, { planet.distance, planet.size, planet.initialSize,
            planet.orbitSpeed, planet.selfRotateSpeed, planet.axisTilt })) return false;
        appendSnapshotU32(bytes, static_cast<std::uint32_t>(planet.health));
        bytes.push_back(static_cast<char>(planet.active));
    }
    body.swap(bytes);
    return true;
}

// 수신 모듈은 조각을 전부 조립하고 해석한 뒤 게임 스레드에서 호출합니다.
// 원격 기체는 ID와 함께 보관하고, 내 기체는 기존 카메라와 이동 변수에 반영합니다.
std::vector<Protocol::PlayerState> snapshotPlayers;

bool applySnapshot(const WorldSnapshot& snapshot, std::uint32_t localPlayerId)
{
    // 송신 측과 같은 필드·ID·개수 검사를 통과한 상태만 적용합니다.
    std::vector<char> checkedBody;
    if (!encodeSnapshotBody(snapshot, checkedBody) ||
        snapshot.planets.size() != planets.size() || localPlayerId == 0) {
        return false;
    }

    const auto localPlayer = std::find_if(snapshot.players.begin(), snapshot.players.end(),
        [localPlayerId](const Protocol::PlayerState& player) {
            return player.playerId == localPlayerId;
        });
    if (localPlayer == snapshot.players.end()) return false;
    for (const auto& planet : snapshot.planets) {
        if (planet.planetId > planets.size()) return false;
    }

    // 새 세션의 첫 스냅샷은 이전 세션의 틱과 비교하지 않습니다.
    static std::uint32_t lastSessionId = 0;
    static std::uint32_t lastServerTick = 0;
    static bool hasSnapshot = false;
    const auto sessionId = connection.sessionId();
    if (hasSnapshot && lastSessionId == sessionId) {
        const std::uint32_t difference = snapshot.header.serverTick - lastServerTick;
        if (difference == 0 || difference >= 0x80000000u) return false;
    }

    const auto& world = snapshot.header.world;
    constexpr float timeUnitsPerSecond = planetTimeStep / worldTickSeconds;
    const float nextGlobalTime = world.globalTime * timeUnitsPerSecond;
    const float nextRotateTime = world.rotateTime * timeUnitsPerSecond;
    const float nextOrbitTime = world.orbitTime * timeUnitsPerSecond;
    const float nextExplosionTime = world.sunExplosionTime * (sunExplosionTimeStep / worldTickSeconds);
    if (!std::isfinite(nextGlobalTime) || !std::isfinite(nextRotateTime) ||
        !std::isfinite(nextOrbitTime) || !std::isfinite(nextExplosionTime)) return false;

    // 임시 목록을 먼저 완성하므로 검증 실패 시 기존 월드는 바뀌지 않습니다.
    auto nextPlayers = snapshot.players;
    auto nextPlanets = planets;
    std::vector<MissileData> nextMissiles;
    nextMissiles.reserve(snapshot.projectiles.size());
    for (const auto& projectile : snapshot.projectiles) {
        const glm::vec3 velocity(projectile.velocity.x, projectile.velocity.y, projectile.velocity.z);
        const double horizontal = std::hypot(double(velocity.x), double(velocity.z));
        MissileData missile{};
        missile.projectileId = projectile.projectileId;
        missile.ownerPlayerId = projectile.ownerPlayerId;
        missile.position = { projectile.position.x, projectile.position.y, projectile.position.z };
        missile.velocity = velocity * worldTickSeconds;
        missile.yaw = static_cast<float>(glm::degrees(std::atan2(double(velocity.x), double(velocity.z))));
        missile.pitch = static_cast<float>(glm::degrees(std::atan2(double(velocity.y), horizontal)));
        missile.lifeTime = projectile.lifeTime;
        missile.active = true;
        nextMissiles.push_back(missile);
    }
    for (const auto& state : snapshot.planets) {
        auto& planet = nextPlanets[state.planetId - 1];
        planet.distance = state.distance;
        planet.size = state.size;
        planet.initialSize = state.initialSize;
        planet.orbitSpeed = state.orbitSpeed / timeUnitsPerSecond;
        planet.selfRotateSpeed = state.selfRotateSpeed / (planetSpinMultiplier * timeUnitsPerSecond);
        planet.axisTilt = state.axisTilt;
        planet.health = state.health;
        planet.active = state.active != 0;
    }

    shipPos = { localPlayer->position.x, localPlayer->position.y, localPlayer->position.z };
    shipAngle = localPlayer->yaw;
    shipPitch = localPlayer->pitch;
    snapshotPlayers.swap(nextPlayers);
    planets.swap(nextPlanets);
    missiles.swap(nextMissiles); // 서버 목록에서 빠진 미사일도 함께 제거됩니다.
    worldTick = snapshot.header.serverTick;
    globalTime = nextGlobalTime;
    rotateTime = nextRotateTime;
    orbitTime = nextOrbitTime;
    sunExplosionTimer = nextExplosionTime;
    isRotating = world.rotating != 0;
    isOrbiting = world.orbiting != 0;
    isGameCleared = world.phase == Protocol::WorldPhase::Cleared;
    isSunExploding = world.phase == Protocol::WorldPhase::SunExploding;
    isSunDestroyed = isGameCleared;

    lastSessionId = sessionId;
    lastServerTick = snapshot.header.serverTick;
    hasSnapshot = true;
    return true;
}

struct PublishedWorldSnapshot {
    std::uint32_t id;
    std::vector<char> body;
};

static std::shared_ptr<const PublishedWorldSnapshot> latestSnapshot;

void publishSnapshot() {
    auto next = std::make_shared<PublishedWorldSnapshot>();
    if (!encodeSnapshotBody(captureWorldSnapshot(), next->body)) return;
    next->id = latestSnapshot && latestSnapshot->id != UINT32_MAX ? latestSnapshot->id + 1 : 1;
    latestSnapshot = next;
}

bool SnapshotSender::takeFrame(std::uint32_t sequence, std::vector<char>& frame) {
    if (sequence == 0 || !latestSnapshot) return false;
    // 나머지 조각이 있으면 같은 본문을 끝까지 넘긴 뒤 최신 스냅샷으로 넘어간다.
    const bool continuing = snapshot_ && offset_ < snapshot_->body.size();
    if (!continuing && snapshot_ == latestSnapshot) return false;
    const auto source = continuing ? snapshot_ : latestSnapshot;
    const std::size_t offset = continuing ? offset_ : 0;
    const std::size_t count = std::min(Protocol::MAX_INIT_CHUNK_DATA_SIZE, source->body.size() - offset);

    std::vector<char> chunk;
    chunk.reserve(Protocol::WORLD_INIT_CHUNK_HEADER_SIZE + count);
    appendSnapshotU32(chunk, source->id);
    appendSnapshotU32(chunk, static_cast<std::uint32_t>(source->body.size()));
    appendSnapshotU32(chunk, static_cast<std::uint32_t>(offset));
    const auto first = source->body.begin() + offset;
    chunk.insert(chunk.end(), first, first + count);
    if (!encodeFrame(Protocol::MessageType::WORLD_SNAPSHOT, sequence, chunk, frame)) return false;

    snapshot_ = source;
    offset_ = offset + count;
    return true;
}

// 전부 조립한 논리 본문을 Protocol.h의 순서대로 읽습니다.
bool decodeSnapshotBody(const std::vector<char>& body, WorldSnapshot& snapshot)
{
    if (body.size() < Protocol::WORLD_SNAPSHOT_HEADER_SIZE) return false;
    std::size_t offset = 0;
    auto readU32 = [&]() {
        std::uint32_t value;
        std::memcpy(&value, body.data() + offset, 4);
        offset += 4;
        return ntohl(value);
    };
    auto readFloat = [&]() {
        const auto bits = readU32();
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    };
    auto readByte = [&]() { return static_cast<std::uint8_t>(body[offset++]); };

    WorldSnapshot next{};
    auto& header = next.header;
    auto& world = header.world;
    header.serverTick = readU32();
    world.phase = static_cast<Protocol::WorldPhase>(readByte());
    world.rotating = readByte();
    world.orbiting = readByte();
    world.globalTime = readFloat();
    world.rotateTime = readFloat();
    world.orbitTime = readFloat();
    world.sunExplosionTime = readFloat();
    header.playerCount = readU32();
    header.projectileCount = readU32();
    header.planetCount = readU32();

    // 개수에 맞는 바이트가 정확히 있는지 확인한 뒤에만 목록을 읽습니다.
    const std::uint64_t expected = Protocol::WORLD_SNAPSHOT_HEADER_SIZE
        + std::uint64_t(header.playerCount) * Protocol::PLAYER_STATE_SIZE
        + std::uint64_t(header.projectileCount) * Protocol::PROJECTILE_STATE_SIZE
        + std::uint64_t(header.planetCount) * Protocol::PLANET_STATE_SIZE;
    if (expected != body.size() || header.projectileCount > Protocol::MAX_PROJECTILES ||
        header.planetCount > Protocol::MAX_PLANETS) return false;

    next.players.resize(header.playerCount);
    for (auto& player : next.players) {
        player.playerId = readU32();
        player.position = { readFloat(), readFloat(), readFloat() };
        player.yaw = readFloat();
        player.pitch = readFloat();
    }
    next.projectiles.resize(header.projectileCount);
    for (auto& projectile : next.projectiles) {
        projectile.projectileId = readU32();
        projectile.ownerPlayerId = readU32();
        projectile.position = { readFloat(), readFloat(), readFloat() };
        projectile.velocity = { readFloat(), readFloat(), readFloat() };
        projectile.lifeTime = readFloat();
    }
    next.planets.resize(header.planetCount);
    for (auto& planet : next.planets) {
        planet.planetId = readU32();
        planet.distance = readFloat();
        planet.size = readFloat();
        planet.initialSize = readFloat();
        planet.orbitSpeed = readFloat();
        planet.selfRotateSpeed = readFloat();
        planet.axisTilt = readFloat();
        const auto health = readU32();
        if (health > INT32_MAX) return false;
        planet.health = static_cast<std::int32_t>(health);
        planet.active = readByte();
    }
    std::vector<char> checked;
    if (!encodeSnapshotBody(next, checked)) return false;
    snapshot = std::move(next);
    return true;
}

// Protocol.h의 공용 타입을 검증하며, 이미 사라진 대상도 위치만으로 효과를 재생합니다.
bool isValidCombatEvent(const Protocol::CombatEvent& event)
{
    if (event.eventId == 0 || event.damage < 0 ||
        !std::isfinite(event.position.x) || !std::isfinite(event.position.y) ||
        !std::isfinite(event.position.z)) return false;

    switch (event.eventType) {
    case Protocol::CombatEventType::Hit:
    case Protocol::CombatEventType::Destroyed:
    case Protocol::CombatEventType::SunExplosion:
        break;
    default:
        return false;
    }
    switch (event.targetKind) {
    case Protocol::TargetKind::None:
    case Protocol::TargetKind::Sun:
        return event.targetId == 0;
    case Protocol::TargetKind::Planet:
        return event.targetId != 0 && event.targetId <= Protocol::MAX_PLANETS;
    case Protocol::TargetKind::Player:
    case Protocol::TargetKind::Asteroid:
        return event.targetId != 0;
    default:
        return false;
    }
}

// 구조체의 패딩을 복사하지 않고, 공용 본문 순서와 네트워크 바이트 순서로 읽습니다.
bool decodeCombatEventBody(const std::vector<char>& body, Protocol::CombatEvent& event)
{
    if (body.size() != Protocol::COMBAT_EVENT_BODY_SIZE) return false;
    std::size_t offset = 0;
    auto readU32 = [&]() {
        std::uint32_t value;
        std::memcpy(&value, body.data() + offset, sizeof(value));
        offset += sizeof(value);
        return ntohl(value);
    };
    auto readU16 = [&]() {
        std::uint16_t value;
        std::memcpy(&value, body.data() + offset, sizeof(value));
        offset += sizeof(value);
        return ntohs(value);
    };
    auto readFloat = [&]() {
        const auto bits = readU32();
        float value;
        static_assert(sizeof(value) == sizeof(bits), "Expected 32-bit float");
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };

    Protocol::CombatEvent next{};
    next.serverTick = readU32();
    next.eventId = readU32();
    next.eventType = static_cast<Protocol::CombatEventType>(readU16());
    next.sourcePlayerId = readU32();
    next.projectileId = readU32();
    next.targetKind = static_cast<Protocol::TargetKind>(static_cast<std::uint8_t>(body[offset++]));
    next.targetId = readU32();
    next.position = { readFloat(), readFloat(), readFloat() };
    const auto damage = readU32();
    if (damage > INT32_MAX) return false;
    next.damage = static_cast<std::int32_t>(damage);
    if (!isValidCombatEvent(next)) return false;
    event = next;
    return true;
}

struct EventReceiveState {
    std::uint32_t lastEventId = 0;
};

// 게임 스레드에서 효과만 재생합니다. 체력·파괴 상태·클리어는 snapshot/delta가 확정합니다.
bool applyEvent(const Protocol::CombatEvent& event, EventReceiveState& state)
{
    if (!isValidCombatEvent(event)) return false;
    // TCP에서 이벤트 ID는 세션 동안 증가합니다. 중복/지난 ID는 정상적으로 무시합니다.
    if (event.eventId <= state.lastEventId) return true;

    const glm::vec3 position(event.position.x, event.position.y, event.position.z);
    const bool asteroid = event.targetKind == Protocol::TargetKind::Asteroid;
    switch (event.eventType) {
    case Protocol::CombatEventType::Hit:
        spawnExplosion(position, asteroid ? glm::vec3(0.5f, 0.4f, 0.3f)
            : glm::vec3(0.1f, 0.5f, 0.2f), asteroid ? 3 : 5);
        break;
    case Protocol::CombatEventType::Destroyed:
        spawnExplosion(position, asteroid ? glm::vec3(0.8f, 0.3f, 0.1f)
            : glm::vec3(1.0f, 0.1f, 0.0f), asteroid ? 30 : 100);
        break;
    case Protocol::CombatEventType::SunExplosion:
        spawnExplosion(position, glm::vec3(1.0f), 500);
        break;
    }
    state.lastEventId = event.eventId;
    return true;
}

// 한 세션에서 한 스냅샷씩 조립하며 이벤트 중복 기록도 새 접속마다 초기화합니다.
struct SnapshotReceiveState {
    std::uint32_t sessionId = 0;
    std::uint32_t nextSequence = 2; // 참가 승인 프레임은 1입니다.
    std::uint32_t snapshotId = 0;
    std::uint32_t totalBytes = 0;
    std::vector<char> body;
    std::chrono::steady_clock::time_point startedAt{};
    EventReceiveState events;
};

bool receiveSnapshotChunk(const std::vector<char>& chunk, SnapshotReceiveState& state)
{
    if (chunk.size() <= Protocol::WORLD_INIT_CHUNK_HEADER_SIZE ||
        chunk.size() > Protocol::MAX_BODY_SIZE) return false;
    auto readU32 = [&](std::size_t offset) {
        std::uint32_t value;
        std::memcpy(&value, chunk.data() + offset, 4);
        return ntohl(value);
    };
    const auto id = readU32(0);
    const auto total = readU32(4);
    const auto offset = readU32(8);
    const auto count = chunk.size() - Protocol::WORLD_INIT_CHUNK_HEADER_SIZE;
    if (id == 0 || total < Protocol::WORLD_SNAPSHOT_HEADER_SIZE ||
        offset > total || count > total - offset) return false;

    if (state.body.empty()) {
        if (offset != 0) return false;
        if (state.snapshotId != 0) {
            const std::uint32_t difference = id - state.snapshotId;
            if (difference == 0 || difference >= 0x80000000u) return false;
        }
        state.snapshotId = id;
        state.totalBytes = total;
        state.startedAt = std::chrono::steady_clock::now();
    }
    if (id != state.snapshotId || total != state.totalBytes ||
        offset != state.body.size()) return false;
    // 광고한 전체 길이로 미리 할당하지 않고 실제 도착한 바이트만 저장합니다.
    state.body.insert(state.body.end(),
        chunk.begin() + Protocol::WORLD_INIT_CHUNK_HEADER_SIZE, chunk.end());
    if (state.body.size() != state.totalBytes) return true;

    WorldSnapshot snapshot;
    if (!decodeSnapshotBody(state.body, snapshot)) return false;
    if (!applySnapshot(snapshot, connection.playerId())) return false;
    state.body.clear();
    return true;
}

void receiveServerSnapshots()
{
    static SnapshotReceiveState state;
    if (connection.sessionId() == 0) {
        state = {};
        return;
    }
    if (state.sessionId != connection.sessionId()) {
        state = {};
        state.sessionId = connection.sessionId();
    }
    auto fail = [&]() {
        std::cerr << u8"[클라이언트] 서버 메시지 수신 실패로 연결을 종료합니다." << std::endl;
        connection.disconnect();
        state = {};
    };
    try {
        if (!state.body.empty() &&
            std::chrono::steady_clock::now() - state.startedAt >=
                std::chrono::milliseconds(Protocol::RECEIVE_TIMEOUT_MS)) {
            fail();
            return;
        }
        std::vector<DecodedFrame> frames;
        if (!connection.receiveFrames(frames)) {
            fail();
            return;
        }
        for (const auto& frame : frames) {
            if (frame.header.sequence != state.nextSequence) {
                fail();
                return;
            }
            state.nextSequence = state.nextSequence == UINT32_MAX ? 1 : state.nextSequence + 1;
            if (frame.header.type == Protocol::MessageType::SESSION_CLOSE) {
                connection.disconnect();
                state = {};
                return;
            }
            if (frame.header.type == Protocol::MessageType::WORLD_SNAPSHOT &&
                !receiveSnapshotChunk(frame.body, state)) {
                fail();
                return;
            }
            if (frame.header.type == Protocol::MessageType::COMBAT_EVENT) {
                Protocol::CombatEvent event{};
                if (!decodeCombatEventBody(frame.body, event) || !applyEvent(event, state.events)) {
                    fail();
                    return;
                }
            }
        }
    } catch (const std::bad_alloc&) {
        fail();
    } catch (const std::length_error&) {
        fail();
    }
}

// 매 게임 틱마다 현재 키와 마우스 상태를 수집해 sendInput()에 넘긴다.
void runClientTick() {
    // 참가 승인을 받지 않았다면 대기 중인 입력과 업데이트 번호를 초기화한다.
    if (connection.sessionId() == 0) {
        pendingInputBody.clear();
        clientInput = {};
        clientTick = 0;
        return;
    }

    // 매번 0부터 시작하여 키를 뗀 상태도 반영한다.
    clientInput = {};
    clientInput.clientTick = clientTick;
    // 행성 이름을 입력하는 동안에는 이동, 가속, 발사 입력을 보내지 않는다.
    if (!isTyping) {
        // 누른 동작의 비트를 OR로 합쳐 동시 입력을 표현한다. 예: W와 R은 1 | 16 = 17.
        if (keyState['w']) clientInput.buttons |= static_cast<std::uint16_t>(Protocol::InputBits::Forward);
        if (keyState['s']) clientInput.buttons |= static_cast<std::uint16_t>(Protocol::InputBits::Backward);
        if (keyState['a']) clientInput.buttons |= static_cast<std::uint16_t>(Protocol::InputBits::Left);
        if (keyState['d']) clientInput.buttons |= static_cast<std::uint16_t>(Protocol::InputBits::Right);
        if (keyState['r']) clientInput.buttons |= static_cast<std::uint16_t>(Protocol::InputBits::Boost);
        if (isLeftButtonDown) clientInput.buttons |= static_cast<std::uint16_t>(Protocol::InputBits::Fire);
    }
    const auto player = captureLocalPlayerState(connection.playerId());
    clientInput.yaw = player.yaw;
    clientInput.pitch = player.pitch;
    // 준비한 입력 본문은 전송 모듈이 가져가도록 보관하고 다음 틱으로 진행한다.
    sendInput();
    ++clientTick;
}

void runHostTick() {
    ++worldTick;
    globalTime = static_cast<float>(worldTick) * 0.5f;
    if (isLeftButtonDown) fireMissile();
    if (isRotating) rotateTime += planetTimeStep;
    if (isOrbiting) orbitTime += planetTimeStep;

    const int forwardInput = keyState['w'] - keyState['s'];
    const int strafeInput = keyState['d'] - keyState['a'];
    updateMovement(shipPos, shipAngle, shipPitch, forwardInput, strafeInput, keyState['r']);
    updateGameLogic();

    // 한 틱은 16ms다. 남은 시간을 이월해 시뮬레이션 1초당 20회 발행한다.
    constexpr unsigned int snapshotIntervalMs = 50;
    static unsigned int elapsedSnapshotMs = 0;
    elapsedSnapshotMs += worldTickMs;
    if (elapsedSnapshotMs >= snapshotIntervalMs) {
        elapsedSnapshotMs -= snapshotIntervalMs;
        publishSnapshot();
    }
}

void Timer(int value) {
    receiveServerSnapshots();
    runClientTick();
    if (connection.sessionId() == 0) {
        runHostTick();
        if (hostingNetworkGame && !pollHostServer()) {
            glutLeaveMainLoop();
            return;
        }
    }
    updateParticles();
    glutPostRedisplay();
    glutTimerFunc(worldTickMs, Timer, 0);
}

int main(int argc, char** argv)
{
    const bool hostMode = argc == 2 && std::strcmp(argv[1], "--host") == 0;
    const bool joinMode = argc == 3 && std::strcmp(argv[1], "--join") == 0;
    if (hostMode || joinMode) {
        if (joinMode && !connection.connectTo(argv[2], Protocol::PORT)) return 1;
        argc = 1;
        argv[1] = nullptr;
    } else {
        connection.connectTo("127.0.0.1", Protocol::PORT);
    }

    glutInit(&argc, argv);
    int screenWidth = glutGet(GLUT_SCREEN_WIDTH);
    int screenHeight = glutGet(GLUT_SCREEN_HEIGHT);
    width = screenWidth;
    height = screenHeight;
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowPosition(0, 0);
    glutInitWindowSize(width, height);
    glutCreateWindow("Solar System Adventure");
    glutFullScreen();
    glewExperimental = GL_TRUE;
    glewInit();
    glEnable(GL_DEPTH_TEST);

    make_vertexShaders();
    make_fragmentShaders();
    shaderProgramID = make_shaderProgram();

    modelLoc = glGetUniformLocation(shaderProgramID, "model");
    viewLoc = glGetUniformLocation(shaderProgramID, "view");
    projLoc = glGetUniformLocation(shaderProgramID, "projection");
    colorLoc = glGetUniformLocation(shaderProgramID, "objectColor");
    lightPosLoc = glGetUniformLocation(shaderProgramID, "lightPos");
    viewPosLoc = glGetUniformLocation(shaderProgramID, "viewPos");
    isSunLoc = glGetUniformLocation(shaderProgramID, "isSun");

    initSphereData();
    initPlanetSettings();
    initStars();
    initAsteroids();
    initRing();
    whiteTex = createWhiteTexture();
    if (hostMode) {
        if (!startHostServer()) return 1;
        hostingNetworkGame = true;
    }

    glutDisplayFunc(drawScene);
    glutReshapeFunc(Reshape);
    glutKeyboardFunc(Keyboard);
    glutKeyboardUpFunc(KeyboardUp);
    glutMouseFunc(Mouse);
    glutPassiveMotionFunc(Motion);
    glutMotionFunc(Motion);
    glutTimerFunc(worldTickMs, Timer, 0);
    glutSetCursor(GLUT_CURSOR_NONE);

    glutMainLoop();

    return 0; 
}
