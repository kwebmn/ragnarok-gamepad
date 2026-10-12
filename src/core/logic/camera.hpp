#pragma once
#include <cstddef>
#include <deque>

namespace uapad {

// Which way the game camera is turned, learned from the client's own walk requests: the cursor sat
// (sx, sy) px from the centre of the client area and the client asked to walk to a cell (wx, wy) away
// from us. Until a sample arrives the camera counts as not turned: north up, as the client starts.
class CameraYaw {
public:
    // sx, sy: screen offset, +y down. wx, wy: map offset in cells, +y north.
    void addSample(int sx, int sy, int wx, int wy);
    // Map bearing minus screen bearing (both counter-clockwise from east / from the right), radians.
    double radians() const { return yaw_; }
    // Unit map direction (+y north) for a non-zero screen direction (+y down).
    void toWorld(double sx, double sy, double& wx, double& wy) const;
    size_t samples() const { return diffs_.size(); }

private:
    std::deque<double> diffs_;  // map minus screen bearing of the latest samples
    double yaw_ = 0;
};

}  // namespace uapad
