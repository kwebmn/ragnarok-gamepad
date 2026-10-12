#include "core/logic/cursor_bridge.hpp"

#include <algorithm>

namespace uapad {

CursorBridge::Step CursorBridge::step(int targetX, int targetY, int width, int height) {
    Step s;
    if (width <= 0 || height <= 0) return s;
    if (width != width_ || height != height_) {
        synced_ = false;
        width_ = width;
        height_ = height;
    }
    const int x = std::clamp(targetX, 0, width - 1);
    const int y = std::clamp(targetY, 0, height - 1);
    if (!synced_) {
        synced_ = true;
        x_ = 0;
        y_ = 0;
        s.dx = -kSlam;
        s.dy = -kSlam;
        s.settling = true;
        return s;
    }
    s.dx = x - x_;
    s.dy = y - y_;
    x_ = x;
    y_ = y;
    return s;
}

}  // namespace uapad
