#pragma once
#include "core/logic/controller.hpp"
#include "fixtures.hpp"

namespace fx {

inline ViewInfo view() { return ViewInfo{true, 800, 600}; }

inline PadState pad(uint16_t buttons = 0) {
    PadState p;
    p.connected = true;
    p.buttons = buttons;
    return p;
}

inline PadState padLT(uint16_t buttons) {
    PadState p = pad(buttons);
    p.lt = 1.0f;
    return p;
}

inline PadState padRT(uint16_t buttons) {
    PadState p = pad(buttons);
    p.rt = 1.0f;
    return p;
}

// The right stick pushed to (rx, ry), +y up.
inline PadState padR(float rx, float ry, uint16_t buttons = 0) {
    PadState p = pad(buttons);
    p.rx = rx;
    p.ry = ry;
    return p;
}

inline uint16_t pid(const Bytes& b) { return rd16(b.data(), 0); }

}  // namespace fx
