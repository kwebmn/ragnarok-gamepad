#include "core/config/config.hpp"
#include "test.hpp"

using namespace uapad;

TEST(config_defaults_when_empty) {
    Config c = parseConfig("");
    CHECK(c.enabled);
    CHECK(c.logLevel == LogLevel::Info);
    CHECK_EQ(c.deadzone, 0.25f);
    CHECK_EQ(c.walkRadius, 0.22f);
    CHECK(c.press == PressMode::Ring);
    CHECK(c.buttons == ButtonChannel::DirectInput);
    CHECK_EQ(c.giveUpSeconds, 4.0f);
    CHECK_EQ(c.blacklistSeconds, 10.0f);
    CHECK_EQ(c.attackIntervalMs, 300);
    CHECK(c.groundSelf == (std::vector<uint16_t>{12, 25, 27, 70}));
    CHECK(c.rumble);
}

TEST(config_reads_all_keys) {
    Config c = parseConfig(
        "[general]\nenabled = 0\nlog_level = debug ; comment\n"
        "[stick]\ndeadzone=0.3\nwalk_radius = 0.3\npress = center\n"
        "[mouse]\nbuttons = messages\n"
        "[target]\ngive_up_seconds = 6\nblacklist_seconds = 20\nattack_interval_ms = 500\n"
        "[skills]\nground_self = 12, 25\n"
        "[rumble]\nenabled = off\n");
    CHECK(!c.enabled);
    CHECK(c.logLevel == LogLevel::Debug);
    CHECK_EQ(c.deadzone, 0.3f);
    CHECK_EQ(c.walkRadius, 0.3f);
    CHECK(c.press == PressMode::Center);
    CHECK(c.buttons == ButtonChannel::Messages);
    CHECK_EQ(c.giveUpSeconds, 6.0f);
    CHECK_EQ(c.blacklistSeconds, 20.0f);
    CHECK_EQ(c.attackIntervalMs, 500);
    CHECK(c.groundSelf == (std::vector<uint16_t>{12, 25}));
    CHECK(!c.rumble);
}

TEST(config_bad_values_keep_defaults) {
    Config c = parseConfig(
        "\xEF\xBB\xBF; file saved with a UTF-8 BOM\n"
        "[stick]\ndeadzone = abc\nwalk_radius = 5\npress = sideways\n"
        "[target]\nattack_interval_ms = 10\n"
        "[skills]\nground_self = 12,x\n"
        "[nonsense]\nfoo = bar\nnot a line\n");
    CHECK_EQ(c.deadzone, 0.25f);
    CHECK_EQ(c.walkRadius, 0.22f);
    CHECK(c.press == PressMode::Ring);
    CHECK_EQ(c.attackIntervalMs, 300);
    CHECK_EQ(c.groundSelf.size(), 4u);
}

TEST(config_empty_ground_self_list_is_allowed) {
    Config c = parseConfig("[skills]\nground_self =\n");
    CHECK(c.groundSelf.empty());
}

TEST(config_mouse_freedom) {
    CHECK(parseConfig("").mouseFreedom);  // on by default: the uaRO client hides the cursor without it
    CHECK(!parseConfig("[mouse]\nfreedom = 0\n").mouseFreedom);
    CHECK(parseConfig("[mouse]\nfreedom = 1\n").mouseFreedom);
}
