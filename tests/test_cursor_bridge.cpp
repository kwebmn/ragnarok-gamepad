#include "core/logic/cursor_bridge.hpp"
#include "test.hpp"

using namespace uapad;

TEST(cursor_bridge_first_step_parks_in_the_corner) {
    CursorBridge b;
    CursorBridge::Step s = b.step(500, 300, 1920, 1080);
    CHECK(s.settling);           // the client's cursor position is unknown: push it into the corner
    CHECK(s.dx <= -1920 && s.dy <= -1080);
    s = b.step(500, 300, 1920, 1080);
    CHECK(!s.settling);
    CHECK_EQ(s.dx, 500L);        // from the corner straight to the target
    CHECK_EQ(s.dy, 300L);
}

TEST(cursor_bridge_follows_the_target_exactly) {
    CursorBridge b;
    b.step(0, 0, 800, 600);
    b.step(100, 100, 800, 600);
    CursorBridge::Step s = b.step(90, 130, 800, 600);
    CHECK_EQ(s.dx, -10L);
    CHECK_EQ(s.dy, 30L);
    s = b.step(90, 130, 800, 600);  // nothing moved: report nothing
    CHECK_EQ(s.dx, 0L);
    CHECK_EQ(s.dy, 0L);
}

TEST(cursor_bridge_clamps_targets_to_the_client_area) {
    CursorBridge b;
    b.step(0, 0, 800, 600);
    b.step(10, 10, 800, 600);
    CursorBridge::Step s = b.step(5000, -40, 800, 600);  // the Windows cursor left the game window
    CHECK_EQ(s.dx, 789L);   // to x = 799
    CHECK_EQ(s.dy, -10L);   // to y = 0
}

TEST(cursor_bridge_resyncs_after_reset_or_resize) {
    CursorBridge b;
    b.step(0, 0, 800, 600);
    b.step(50, 50, 800, 600);
    b.reset();  // e.g. the window lost focus and the user moved the real mouse meanwhile
    CHECK(b.step(60, 60, 800, 600).settling);
    CHECK_EQ(b.step(60, 60, 800, 600).dx, 60L);
    CHECK(b.step(60, 60, 1024, 768).settling);  // the client area changed size
}

TEST(cursor_bridge_ignores_an_empty_client_area) {
    CursorBridge b;
    CursorBridge::Step s = b.step(10, 10, 0, 0);
    CHECK(!s.settling);
    CHECK_EQ(s.dx, 0L);
    CHECK_EQ(s.dy, 0L);
    CHECK(!b.synced());
}
