#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct PublishedWorldSnapshot;

// 전송 모듈은 연결마다 하나를 두고 게임 스레드에서 사용한다.
class SnapshotSender {
public:
    // MATCH_START 후 이전 프레임을 다 보냈을 때 호출한다.
    // true면 frame 전체를 맡아 부분 송신하고, false면 보낼 프레임이 없다.
    bool takeFrame(std::uint32_t sequence, std::vector<char>& frame);

private:
    std::shared_ptr<const PublishedWorldSnapshot> snapshot_;
    std::size_t offset_ = 0;
};
