#include "pad_fixtures.hpp"
#include "test.hpp"

using namespace fx;

// --- walking ---

TEST(ctl_walk_ring_point_and_stop) {
    Controller c(Config{});
    WorldState w = world();
    PadState p = pad();
    p.lx = 1.0f;  // full right
    TickOutput o = c.tick(p, w, view(), 100);
    CHECK(o.mouse.active && o.mouse.lmb);
    CHECK_EQ(o.mouse.x, 532);  // centre (400, 300) + 0.22 * 600 = 132 px
    CHECK_EQ(o.mouse.y, 300);
    p.lx = 0;
    p.ly = 1.0f;  // stick up -> screen up
    o = c.tick(p, w, view(), 150);
    CHECK_EQ(o.mouse.x, 400);
    CHECK_EQ(o.mouse.y, 168);
    o = c.tick(pad(), w, view(), 200);  // released
    CHECK(!o.mouse.active);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x00a7);
    CHECK(decodePos3(o.packets[0].data(), 5) == (Cell{100, 100}));
}

TEST(ctl_walk_deadzone_and_center_press) {
    Config cfg;
    cfg.press = PressMode::Center;
    Controller c(cfg);
    WorldState w = world();
    PadState p = pad();
    p.lx = 0.2f;  // inside the 0.25 dead zone
    CHECK(!c.tick(p, w, view(), 10).mouse.active);
    p.lx = -1.0f;
    TickOutput o = c.tick(p, w, view(), 20);
    CHECK(o.mouse.active && o.mouse.lmb);
    CHECK_EQ(o.mouse.x, 400);  // the first frame presses at the centre
    CHECK_EQ(o.mouse.y, 300);
    o = c.tick(p, w, view(), 30);
    CHECK_EQ(o.mouse.x, 268);
    CHECK_EQ(o.mouse.y, 300);
}

TEST(ctl_inactive_when_unfocused_disabled_or_disconnected) {
    Controller c(Config{});
    WorldState w = world();
    PadState p = pad();
    p.lx = 1.0f;
    ViewInfo unfocused = view();
    unfocused.focused = false;
    CHECK(!c.tick(p, w, unfocused, 10).mouse.active);
    PadState off = p;
    off.connected = false;
    CHECK(!c.tick(off, w, view(), 20).mouse.active);
    TickOutput o = c.tick(pad(kPadBack), w, view(), 30);
    CHECK(!c.enabled());
    CHECK(o.rumble == Rumble::AutoOff);
    CHECK(!c.tick(p, w, view(), 40).mouse.active);
    c.tick(pad(kPadBack), w, view(), 50);
    CHECK(c.enabled());
    Config cfg;
    cfg.enabled = false;
    Controller startsOff(cfg);
    CHECK(!startsOff.enabled());
}

TEST(ctl_nothing_happens_while_dead_or_before_login) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    w.apply(EvVanish{kSelf, 1}, 0);
    PadState p = pad(kPadA);
    p.lx = 1.0f;
    TickOutput o = c.tick(p, w, view(), 1000);
    CHECK(!o.mouse.active);
    CHECK(o.packets.empty());
    WorldState notYet;
    notYet.reset(kSelf);
    CHECK(!c.tick(p, notYet, view(), 1100).mouse.active);
}

// --- targeting ---

TEST(ctl_attack_nearest_and_cycle) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000003, Cell{106, 100});  // distance 6
    addMob(w, 110000001, Cell{102, 101});  // distance 2
    addMob(w, 110000002, Cell{97, 104});   // distance 4
    TickOutput o = c.tick(pad(kPadA), w, view(), 1000);
    CHECK_EQ(c.target(), 110000001u);
    CHECK(o.rumble == Rumble::TargetSwitch);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x0190);
    CHECK_EQ(rd32(o.packets[0].data(), 5), 110000001u);
    CHECK_EQ(o.packets[0][18], 7);
    c.tick(pad(), w, view(), 1010);
    o = c.tick(pad(kPadRight), w, view(), 1400);
    CHECK_EQ(c.target(), 110000002u);
    CHECK_EQ(o.packets.size(), 1u);
    c.tick(pad(), w, view(), 1410);
    c.tick(pad(kPadRight), w, view(), 1800);
    CHECK_EQ(c.target(), 110000003u);
    c.tick(pad(), w, view(), 1810);
    c.tick(pad(kPadRight), w, view(), 2200);  // wraps around
    CHECK_EQ(c.target(), 110000001u);
    c.tick(pad(), w, view(), 2210);
    c.tick(pad(kPadLeft), w, view(), 2600);
    CHECK_EQ(c.target(), 110000003u);
}

TEST(ctl_cycle_without_target_takes_nearest) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000003, Cell{106, 100});
    addMob(w, 110000001, Cell{102, 101});
    c.tick(pad(kPadLeft), w, view(), 100);
    CHECK_EQ(c.target(), 110000001u);
}

TEST(ctl_candidates_are_monsters_only) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvActor{2000002, 7, 3, 150, Cell{101, 100}, Cell{101, 100}}, 0);          // player
    w.apply(EvActor{110000009, 1002, 100, 200, Cell{101, 101}, Cell{101, 101}}, 0);  // pet
    addMob(w, 110000001, Cell{105, 100});
    c.tick(pad(kPadA), w, view(), 100);
    CHECK_EQ(c.target(), 110000001u);
}

TEST(ctl_no_monsters_denied) {
    Controller c(Config{});
    WorldState w = world();
    TickOutput o = c.tick(pad(kPadA), w, view(), 100);
    CHECK_EQ(c.target(), 0u);
    CHECK(o.packets.empty());
    CHECK(o.rumble == Rumble::Denied);
}

TEST(ctl_attack_throttled) {
    Controller c(Config{});  // 300 ms between attack requests
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    TickOutput o1 = c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1050);
    TickOutput o2 = c.tick(pad(kPadA), w, view(), 1100);
    c.tick(pad(), w, view(), 1150);
    TickOutput o3 = c.tick(pad(kPadA), w, view(), 1400);
    CHECK_EQ(o1.packets.size(), 1u);
    CHECK(o2.packets.empty());
    CHECK_EQ(o3.packets.size(), 1u);
}

TEST(ctl_give_up_blacklists_unreachable_target) {
    Controller c(Config{});  // give up after 4 s, blacklist for 10 s
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});  // e.g. an NPC with a monster sprite: our hits never land
    addMob(w, 110000002, Cell{105, 100});
    c.tick(pad(kPadX), w, view(), 1000);  // auto-attack on -> picks 110000001
    CHECK(c.autoAttack());
    CHECK_EQ(c.target(), 110000001u);
    c.tick(pad(), w, view(), 3000);
    CHECK_EQ(c.target(), 110000001u);  // 2 s without hits: keep trying
    TickOutput o = c.tick(pad(), w, view(), 5000);
    CHECK_EQ(c.target(), 110000002u);  // 4 s without hits: next one
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(rd32(o.packets[0].data(), 5), 110000002u);
    w.apply(EvAction{kSelf, 110000002, 0}, 8000);  // our hit lands
    c.tick(pad(), w, view(), 9500);
    CHECK_EQ(c.target(), 110000002u);
    w.apply(EvVanish{110000002, 1}, 9600);  // it died
    c.tick(pad(), w, view(), 9700);
    CHECK_EQ(c.target(), 0u);  // 110000001 stays blacklisted until 15000
    c.tick(pad(), w, view(), 15000);
    CHECK_EQ(c.target(), 110000001u);
}

TEST(ctl_walking_pauses_auto_attack_and_resumes) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    c.tick(pad(kPadX), w, view(), 1000);
    CHECK_EQ(c.target(), 110000001u);
    PadState p = pad();
    p.lx = 1.0f;
    TickOutput o = c.tick(p, w, view(), 1500);
    CHECK(o.mouse.active);
    CHECK(o.packets.empty());
    o = c.tick(pad(), w, view(), 2000);  // released: stop, then attack again
    CHECK_EQ(o.packets.size(), 2u);
    CHECK_EQ(pid(o.packets[0]), 0x00a7);
    CHECK_EQ(pid(o.packets[1]), 0x0190);
}

TEST(ctl_manual_attack_not_resumed_after_walk) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    PadState p = pad();
    p.ly = -1.0f;
    c.tick(p, w, view(), 1500);
    TickOutput o = c.tick(pad(), w, view(), 2000);
    CHECK_EQ(o.packets.size(), 1u);  // only the stop request
    CHECK_EQ(pid(o.packets[0]), 0x00a7);
}

TEST(ctl_timers_survive_tick_wraparound) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    addMob(w, 110000002, Cell{105, 100});
    const uint32_t t0 = 0xFFFFFC18u;  // GetTickCount() wraps 1000 ms later
    c.tick(pad(kPadX), w, view(), t0);
    CHECK_EQ(c.target(), 110000001u);
    c.tick(pad(), w, view(), t0 + 3000u);  // past zero, 3 s elapsed
    CHECK_EQ(c.target(), 110000001u);
    c.tick(pad(), w, view(), t0 + 4000u);  // 4 s without hits
    CHECK_EQ(c.target(), 110000002u);
}

// --- slots, pickup, sit ---

TEST(ctl_slot_mapping_and_modifier_swallows_face_buttons) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{102, 100});
    TickOutput o = c.tick(padLT(kPadB), w, view(), 1000);  // LT+B -> slot 1: Heal on self
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x0072);
    CHECK_EQ(rd16(o.packets[0].data(), 10), 28);
    CHECK_EQ(rd32(o.packets[0].data(), 21), kSelf);
    CHECK_EQ(c.target(), 0u);  // a support skill does not pick a target
    c.tick(pad(), w, view(), 1100);
    PadState lb = pad(kPadLB | kPadA);  // LB+A -> slot 9: passive skill
    o = c.tick(lb, w, view(), 1300);
    CHECK(o.packets.empty());
    CHECK(o.rumble == Rumble::Denied);
    CHECK_EQ(c.target(), 0u);  // A did not attack
}

TEST(ctl_first_pressed_modifier_wins) {
    Controller c(Config{});
    WorldState w = slotWorld();
    PadState p = padRT(0);
    c.tick(p, w, view(), 1000);  // RT first
    p.lt = 1.0f;
    p.buttons = kPadA;  // then LT too, and A
    TickOutput o = c.tick(p, w, view(), 1100);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x009f);  // RT+A = slot 4 (Red Potion), not LT+A (Bash)
}

TEST(ctl_slot_item_and_missing_item) {
    Controller c(Config{});
    WorldState w = slotWorld();
    TickOutput o = c.tick(padRT(kPadA), w, view(), 1000);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x009f);
    CHECK_EQ(rd16(o.packets[0].data(), 4), 2);
    c.tick(padRT(0), w, view(), 1100);
    o = c.tick(padRT(kPadB), w, view(), 1300);  // Orange Potion: not in the inventory
    CHECK(o.packets.empty());
    CHECK(o.rumble == Rumble::Denied);
}

TEST(ctl_slot_enemy_skill_targets_nearest_then_approaches) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{105, 100});  // distance 5
    TickOutput o = c.tick(padRT(kPadX), w, view(), 1000);  // Fire Bolt, range 9
    CHECK_EQ(c.target(), 110000001u);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x0072);
    CHECK_EQ(rd16(o.packets[0].data(), 10), 19);
    CHECK_EQ(rd32(o.packets[0].data(), 21), 110000001u);
    c.tick(padRT(0), w, view(), 1100);
    o = c.tick(padLT(kPadA), w, view(), 1300);  // Bash, range 1: walk first
    CHECK(c.approaching());
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x00a7);
    CHECK(decodePos3(o.packets[0].data(), 5) == (Cell{105, 100}));
    w.apply(EvSetPos{kSelf, Cell{104, 100}}, 1400);  // arrived next to it
    o = c.tick(padLT(0), w, view(), 1450);
    CHECK(!c.approaching());
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x0072);
    CHECK_EQ(rd16(o.packets[0].data(), 10), 5);
}

TEST(ctl_slot_throttle_and_empty_slot) {
    Controller c(Config{});
    WorldState w = slotWorld();
    TickOutput o = c.tick(padRT(kPadY), w, view(), 1000);  // slot 7: empty
    CHECK(o.packets.empty());
    CHECK(o.rumble == Rumble::Denied);
    c.tick(pad(), w, view(), 1010);
    o = c.tick(padLT(kPadB), w, view(), 1400);
    CHECK_EQ(o.packets.size(), 1u);
    c.tick(padLT(0), w, view(), 1420);
    o = c.tick(padLT(kPadB), w, view(), 1450);  // 50 ms later: too soon
    CHECK(o.packets.empty());
    CHECK(o.rumble == Rumble::None);
}

TEST(ctl_pickup_nearest_with_approach) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvGroundItem{4242, 909, Cell{104, 100}}, 0);  // distance 4
    w.apply(EvGroundItem{4243, 909, Cell{100, 101}}, 0);  // distance 1
    w.apply(EvGroundItem{4244, 909, Cell{120, 100}}, 0);  // distance 20: out of reach
    TickOutput o = c.tick(pad(kPadB), w, view(), 1000);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x00f5);
    CHECK_EQ(rd32(o.packets[0].data(), 4), 4243u);
    c.tick(pad(), w, view(), 1010);
    w.apply(EvGroundItemGone{4243}, 1020);
    o = c.tick(pad(kPadB), w, view(), 1100);
    CHECK(c.approaching());
    CHECK_EQ(pid(o.packets[0]), 0x00a7);
    o = c.tick(pad(), w, view(), 1200);  // 100 ms: no new step yet
    CHECK(o.packets.empty());
    o = c.tick(pad(), w, view(), 1350);  // 250 ms: step again
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(o.packets[0]), 0x00a7);
    w.apply(EvSetPos{kSelf, Cell{103, 100}}, 1400);
    o = c.tick(pad(), w, view(), 1450);
    CHECK_EQ(pid(o.packets[0]), 0x00f5);
    CHECK_EQ(rd32(o.packets[0].data(), 4), 4242u);
    CHECK(!c.approaching());
}

TEST(ctl_pickup_nothing_denied_and_approach_timeout) {
    Controller c(Config{});
    WorldState w = world();
    TickOutput o = c.tick(pad(kPadB), w, view(), 1000);
    CHECK(o.rumble == Rumble::Denied);
    c.tick(pad(), w, view(), 1010);
    w.apply(EvGroundItem{4242, 909, Cell{108, 100}}, 1020);
    c.tick(pad(kPadB), w, view(), 1100);
    CHECK(c.approaching());
    c.tick(pad(), w, view(), 6100);  // 5 s and still not there
    CHECK(!c.approaching());
}

TEST(ctl_stick_cancels_approach) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvGroundItem{4242, 909, Cell{108, 100}}, 0);
    c.tick(pad(kPadB), w, view(), 1000);
    CHECK(c.approaching());
    PadState p = pad();
    p.lx = 1.0f;
    c.tick(p, w, view(), 1100);
    CHECK(!c.approaching());
}

TEST(ctl_sit_stand_and_auto_toggle) {
    Controller c(Config{});
    WorldState w = world();
    TickOutput o = c.tick(pad(kPadY), w, view(), 1000);
    CHECK_EQ(pid(o.packets[0]), 0x0190);
    CHECK_EQ(o.packets[0][18], 2);  // sit
    c.tick(pad(), w, view(), 1010);
    w.apply(EvAction{kSelf, 0, 2}, 1050);  // the server confirms: sitting
    o = c.tick(pad(kPadY), w, view(), 1100);
    CHECK_EQ(o.packets[0][18], 3);  // stand
    c.tick(pad(), w, view(), 1110);
    o = c.tick(pad(kPadX), w, view(), 1200);
    CHECK(c.autoAttack());
    CHECK(o.rumble == Rumble::AutoOn);
    c.tick(pad(), w, view(), 1210);
    o = c.tick(pad(kPadX), w, view(), 1300);
    CHECK(!c.autoAttack());
    CHECK(o.rumble == Rumble::AutoOff);
}

TEST(ctl_auto_attack_waits_while_sitting) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvAction{kSelf, 0, 2}, 0);  // sitting
    addMob(w, 110000001, Cell{102, 100});
    TickOutput o = c.tick(pad(kPadX), w, view(), 1000);
    CHECK_EQ(c.target(), 0u);
    CHECK(o.packets.empty());
    w.apply(EvAction{kSelf, 0, 3}, 1100);  // stood up
    o = c.tick(pad(), w, view(), 1200);
    CHECK_EQ(c.target(), 110000001u);
    CHECK_EQ(o.packets.size(), 1u);
}

TEST(ctl_empty_slot_hint_when_server_sent_no_bar) {
    Controller c(Config{});
    WorldState w = world();  // the server never sent 0x02b9 (HOTKEY_SAVING off)
    TickOutput o = c.tick(padLT(kPadA), w, view(), 1000);
    CHECK(o.rumble == Rumble::Denied);
    CHECK(!o.notes.empty() && o.notes.back().find("hotkey bar") != std::string::npos);
    Controller c2(Config{});
    WorldState known = slotWorld();  // the bar arrived; slot 7 is just empty
    o = c2.tick(padRT(kPadY), known, view(), 1000);
    CHECK(!o.notes.empty() && o.notes.back().find("hotkey bar") == std::string::npos);
}
