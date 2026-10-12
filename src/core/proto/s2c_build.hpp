#pragma once
#include <cstdint>

#include "core/bytes.hpp"

namespace uapad {

// Server -> client packets the pad makes up itself. They only ever go to the client, never to the server.
constexpr uint8_t kEmotionGasp = 0;  // "!"

Bytes buildEmotion(uint32_t actor, uint8_t type);  // 0x00c0: an emotion bubble over `actor`

}  // namespace uapad
