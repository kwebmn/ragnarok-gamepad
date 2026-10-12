#include <cmath>

#include "core/logic/camera.hpp"
#include "test.hpp"

using namespace uapad;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
double deg(double r) { return r * 180.0 / 3.14159265358979323846; }

}  // namespace

TEST(camera_default_is_north_up) {
    CameraYaw c;
    CHECK_EQ(c.samples(), 0u);
    CHECK(near(c.radians(), 0));
    double wx = 0, wy = 0;
    c.toWorld(0, -1, wx, wy);  // screen up
    CHECK(near(wx, 0) && near(wy, 1));  // north
    c.toWorld(3, 0, wx, wy);  // screen right, any length
    CHECK(near(wx, 1) && near(wy, 0));  // east, unit length
}

TEST(camera_learns_rotation) {
    CameraYaw c;
    c.addSample(132, 0, 0, 7);  // the cursor was right of the centre and the client walked north
    CHECK_EQ(c.samples(), 1u);
    CHECK(near(deg(c.radians()), 90));
    double wx = 0, wy = 0;
    c.toWorld(1, 0, wx, wy);  // screen right -> north
    CHECK(near(wx, 0) && near(wy, 1));
    c.toWorld(0, -1, wx, wy);  // screen up -> west
    CHECK(near(wx, -1) && near(wy, 0));
    CameraYaw d;
    d.addSample(93, -93, 5, 5);  // up-right on screen, north-east on the map: not turned
    CHECK(near(deg(d.radians()), 0));
}

TEST(camera_ignores_tiny_samples) {
    CameraYaw c;
    c.addSample(132, 0, 1, 1);  // the destination is too close to tell a direction
    c.addSample(5, 3, 0, 7);    // the cursor sat almost on the centre
    CHECK_EQ(c.samples(), 0u);
    CHECK(near(c.radians(), 0));
}

TEST(camera_trims_outlier) {
    CameraYaw c;
    for (int i = 0; i < 7; ++i) c.addSample(0, -132, 0, 7);  // up -> north
    c.addSample(132, 0, 0, 7);  // a stale cursor: looks like a 90 degree turn
    CHECK(near(deg(c.radians()), 0, 1e-6));
}

TEST(camera_follows_a_turn) {
    CameraYaw c;
    for (int i = 0; i < 8; ++i) c.addSample(0, -132, 0, 7);
    for (int i = 0; i < 6; ++i) c.addSample(0, -132, -7, 0);  // the camera turned: up is west now
    CHECK_EQ(c.samples(), 8u);
    CHECK(near(deg(c.radians()), 90, 1e-6));  // 6 new against 2 old samples
}

TEST(camera_reset_forgets_the_turn) {
    CameraYaw c;
    c.addSample(132, 0, 0, 7);
    c.reset();
    CHECK_EQ(c.samples(), 0u);
    CHECK(near(c.radians(), 0));
}
