#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace net {

constexpr std::size_t kFrameHeaderSize = 12;
constexpr std::size_t kMaxFramePayloadSize = 64 * 1024;

// Returns an empty vector if the payload exceeds 64 KiB.
inline std::vector<std::uint8_t> encodeFrame(
    std::uint16_t version,
    std::uint16_t messageType,
    std::uint32_t sequence,
    const std::vector<std::uint8_t>& payload) {
    if (payload.size() > kMaxFramePayloadSize) {
        return {};
    }

    const auto payloadSize = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame(kFrameHeaderSize + payloadSize);

    // Wire order: version (2), type (2), payload size (4), sequence (4).
    // Store each field most significant byte first.
    frame[0] = static_cast<std::uint8_t>(version >> 8);
    frame[1] = static_cast<std::uint8_t>(version);
    frame[2] = static_cast<std::uint8_t>(messageType >> 8);
    frame[3] = static_cast<std::uint8_t>(messageType);
    frame[4] = static_cast<std::uint8_t>(payloadSize >> 24);
    frame[5] = static_cast<std::uint8_t>(payloadSize >> 16);
    frame[6] = static_cast<std::uint8_t>(payloadSize >> 8);
    frame[7] = static_cast<std::uint8_t>(payloadSize);
    frame[8] = static_cast<std::uint8_t>(sequence >> 24);
    frame[9] = static_cast<std::uint8_t>(sequence >> 16);
    frame[10] = static_cast<std::uint8_t>(sequence >> 8);
    frame[11] = static_cast<std::uint8_t>(sequence);

    std::copy(payload.begin(), payload.end(), frame.data() + kFrameHeaderSize);
    return frame;
}

} // namespace net
