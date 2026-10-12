#include "core/logic/camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace uapad {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr size_t kKeep = 8;
constexpr double kTrim = 30.0 * kPi / 180.0;
constexpr double kMinScreenPx = 10.0;
constexpr int kMinCells = 2;

double wrap(double a) { return std::remainder(a, 2.0 * kPi); }  // to [-pi, pi]

}  // namespace

void CameraYaw::addSample(int sx, int sy, int wx, int wy) {
    if (std::hypot(double(sx), double(sy)) < kMinScreenPx || std::max(std::abs(wx), std::abs(wy)) < kMinCells) return;
    diffs_.push_back(wrap(std::atan2(double(wy), double(wx)) - std::atan2(-double(sy), double(sx))));
    if (diffs_.size() > kKeep) diffs_.pop_front();
    double s = 0, c = 0;
    for (double d : diffs_) {
        s += std::sin(d);
        c += std::cos(d);
    }
    const double all = std::atan2(s, c);
    // Drop samples far from the mean (a cursor that had already moved on, a camera mid-turn) and average again.
    double ks = 0, kc = 0;
    int kept = 0;
    for (double d : diffs_) {
        if (std::fabs(wrap(d - all)) > kTrim) continue;
        ks += std::sin(d);
        kc += std::cos(d);
        ++kept;
    }
    yaw_ = kept ? std::atan2(ks, kc) : all;
}

void CameraYaw::toWorld(double sx, double sy, double& wx, double& wy) const {
    const double a = std::atan2(-sy, sx) + yaw_;
    wx = std::cos(a);
    wy = std::sin(a);
}

}  // namespace uapad
