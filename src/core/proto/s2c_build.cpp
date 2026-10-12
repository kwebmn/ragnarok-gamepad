#include "core/proto/s2c_build.hpp"

namespace uapad {

Bytes buildEmotion(uint32_t actor, uint8_t type) {
    Bytes b(7, 0);
    wr16(b, 0, 0x00c0);
    wr32(b, 2, actor);
    wr8(b, 6, type);
    return b;
}

}  // namespace uapad
