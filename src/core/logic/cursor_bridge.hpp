#pragma once

namespace uapad {

// Puts the legacy client's own cursor at an absolute point. While its window is active the client
// only adds relative DirectInput mouse deltas to a cursor it clamps to its client area, so we keep
// an estimate of that cursor and report target - estimate. The position is unknown at first (and
// after reset() or a resize), so that step pushes the cursor into the top-left corner instead.
class CursorBridge {
public:
    struct Step {
        long dx = 0;
        long dy = 0;
        bool settling = false;  // this frame only parks the cursor in the corner: report no clicks
    };

    Step step(int targetX, int targetY, int width, int height);
    void reset() { synced_ = false; }
    bool synced() const { return synced_; }

private:
    static constexpr long kSlam = 1L << 20;

    bool synced_ = false;
    int x_ = 0;
    int y_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace uapad
