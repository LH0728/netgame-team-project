#pragma once

#include "Protocol.h"
#include <vector>

bool encodeFrame(
    Protocol::MessageType type,
    std::uint32_t sequence,
    const std::vector<char>& body,
    std::vector<char>& outFrame
);