#include <cmath>

#include "pad_fixtures.hpp"
#include "test.hpp"

using namespace fx;

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

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
    CHECK_EQ(pid(pkt(o, 0)), 0x00a7);
    CHECK(decodePos3(pkt(o, 0).data(), 5) == (Cell{100, 100}));
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
    CHECK(o.rumble == Rumble::PadOff);
    CHECK(!c.tick(p, w, view(), 40).mouse.active);
    o = c.tick(pad(kPadBack), w, view(), 50);
    CHECK(c.enabled());
    CHECK(o.rumble == Rumble::PadOn);
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

TEST(ctl_buttons_held_before_focus_returns_do_nothing) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{101, 100});
    ViewInfo unfocused = view();
    unfocused.focused = false;
    c.tick(pad(kPadA | kPadY), w, unfocused, 1000);  // pressed while the game window was in the background
    TickOutput o = c.tick(pad(kPadA | kPadY), w, view(), 1100);
    CHECK(o.packets.empty());
    CHECK_EQ(c.target(), 0u);
    o = c.tick(pad(kPadA | kPadY), w, view(), 2000);  // no repeat and no held attack either
    CHECK(o.packets.empty());
    CHECK_EQ(c.target(), 0u);
}

// --- A: attack ---

TEST(ctl_attack_takes_nearest_and_marks_it) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000003, Cell{106, 100});  // distance 6
    addMob(w, 110000001, Cell{102, 101});  // distance 2
    TickOutput o = c.tick(pad(kPadA), w, view(), 1000);
    CHECK_EQ(c.target(), 110000001u);
    CHECK(c.attacking());
    CHECK(o.rumble == Rumble::TargetSwitch);
    CHECK_EQ(o.mark, 110000001u);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0190);
    CHECK_EQ(rd32(pkt(o, 0).data(), 5), 110000001u);
    CHECK_EQ(pkt(o, 0)[18], 7);
    o = c.tick(pad(), w, view(), 1100);  // released: the server keeps attacking on its own
    CHECK(o.packets.empty());
    CHECK_EQ(o.mark, 0u);
}

TEST(ctl_attack_tap_stops_when_target_dies) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    addMob(w, 110000002, Cell{104, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    w.apply(EvVanish{110000001, 1}, 1200);
    TickOutput o = c.tick(pad(), w, view(), 1300);
    CHECK_EQ(c.target(), 0u);
    CHECK(!c.attacking());
    CHECK(o.packets.empty());
    CHECK(c.tick(pad(), w, view(), 2000).packets.empty());  // the next one is not taken
}

TEST(ctl_held_attack_moves_on_to_next) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    addMob(w, 110000002, Cell{104, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(kPadA), w, view(), 1100);
    w.apply(EvVanish{110000001, 1}, 1200);
    TickOutput o = c.tick(pad(kPadA), w, view(), 1400);
    CHECK_EQ(c.target(), 110000002u);
    CHECK_EQ(o.mark, 110000002u);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0190);
    CHECK_EQ(rd32(pkt(o, 0).data(), 5), 110000002u);
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

TEST(ctl_held_attack_with_no_monsters_waits_quietly) {
    Controller c(Config{});
    WorldState w = world();
    TickOutput o = c.tick(pad(kPadA), w, view(), 1000);
    CHECK(o.rumble == Rumble::Denied);
    o = c.tick(pad(kPadA), w, view(), 1300);  // still held: no buzzing
    CHECK(o.rumble == Rumble::None);
    CHECK(o.packets.empty());
    addMob(w, 110000001, Cell{102, 100});
    o = c.tick(pad(kPadA), w, view(), 1600);  // one shows up while A is held
    CHECK_EQ(c.target(), 110000001u);
    CHECK_EQ(o.packets.size(), 1u);
    c.tick(pad(), w, view(), 1700);
    w.apply(EvVanish{110000001, 1}, 1800);
    c.tick(pad(), w, view(), 1900);
    CHECK(!c.attacking());
    addMob(w, 110000002, Cell{105, 100});
    o = c.tick(padR(1, 0), w, view(), 2000);  // picking a new target later does not attack it
    CHECK_EQ(c.target(), 110000002u);
    CHECK(o.packets.empty());
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

TEST(ctl_held_attack_gives_up_on_unreachable_target) {
    Controller c(Config{});  // give up after 4 s, blacklist for 10 s
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});  // e.g. an NPC with a monster sprite: our hits never land
    addMob(w, 110000002, Cell{105, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    CHECK_EQ(c.target(), 110000001u);
    c.tick(pad(kPadA), w, view(), 3000);
    CHECK_EQ(c.target(), 110000001u);  // 2 s without hits: keep trying
    TickOutput o = c.tick(pad(kPadA), w, view(), 5000);
    CHECK_EQ(c.target(), 110000002u);  // 4 s without hits: the next one, since A is held
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(rd32(pkt(o, 0).data(), 5), 110000002u);
    w.apply(EvAction{kSelf, 110000002, 0}, 8000);  // our hit lands
    c.tick(pad(kPadA), w, view(), 9500);
    CHECK_EQ(c.target(), 110000002u);
    w.apply(EvVanish{110000002, 1}, 9600);  // it died
    c.tick(pad(kPadA), w, view(), 9700);
    CHECK_EQ(c.target(), 0u);  // 110000001 stays blacklisted until 15000
    c.tick(pad(kPadA), w, view(), 15000);
    CHECK_EQ(c.target(), 110000001u);
}

TEST(ctl_attack_tap_gives_up_without_taking_next) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    addMob(w, 110000002, Cell{105, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    TickOutput o = c.tick(pad(), w, view(), 5000);
    CHECK_EQ(c.target(), 0u);
    CHECK(!c.attacking());
    CHECK(o.packets.empty());
}

TEST(ctl_walking_interrupts_attack) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    PadState p = pad();
    p.ly = -1.0f;
    c.tick(p, w, view(), 1500);
    TickOutput o = c.tick(pad(), w, view(), 2000);
    CHECK_EQ(o.packets.size(), 1u);  // only the stop request
    CHECK_EQ(pid(pkt(o, 0)), 0x00a7);
    CHECK_EQ(c.target(), 110000001u);  // the target stays for the next A
    CHECK(!c.attacking());
}

TEST(ctl_held_attack_resumes_after_walk) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    PadState p = pad(kPadA);
    p.lx = 1.0f;
    TickOutput o = c.tick(p, w, view(), 1500);
    CHECK(o.mouse.active);
    CHECK(o.packets.empty());  // no attack while walking
    o = c.tick(pad(kPadA), w, view(), 2000);  // stick released, A still held: stop, then attack again
    CHECK_EQ(o.packets.size(), 2u);
    CHECK_EQ(pid(pkt(o, 0)), 0x00a7);
    CHECK_EQ(pid(pkt(o, 1)), 0x0190);
}

TEST(ctl_held_attack_waits_while_sitting) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvAction{kSelf, 0, 2}, 0);  // sitting
    addMob(w, 110000001, Cell{102, 100});
    TickOutput o = c.tick(pad(kPadA), w, view(), 1000);
    CHECK_EQ(c.target(), 110000001u);
    CHECK(o.packets.empty());
    w.apply(EvAction{kSelf, 0, 3}, 1100);  // stood up
    o = c.tick(pad(kPadA), w, view(), 1200);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0190);
}

TEST(ctl_timers_survive_tick_wraparound) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    addMob(w, 110000002, Cell{105, 100});
    const uint32_t t0 = 0xFFFFFC18u;  // GetTickCount() wraps 1000 ms later
    c.tick(pad(kPadA), w, view(), t0);
    CHECK_EQ(c.target(), 110000001u);
    c.tick(pad(kPadA), w, view(), t0 + 3000u);  // past zero, 3 s elapsed
    CHECK_EQ(c.target(), 110000001u);
    c.tick(pad(kPadA), w, view(), t0 + 4000u);  // 4 s without hits
    CHECK_EQ(c.target(), 110000002u);
}

TEST(ctl_skill_during_attack_resumes_it) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{103, 100});  // within Fire Bolt's 9 cells
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    w.apply(EvAction{kSelf, 110000001, 0}, 1500);         // our hit
    TickOutput o = c.tick(pad(kPadUp), w, view(), 2000);  // F7: Fire Bolt; the server ends the attack for it
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0072);
    CHECK(c.tick(pad(), w, view(), 2200).packets.empty());
    o = c.tick(pad(), w, view(), 2300);  // 300 ms after the skill: the attack is asked for again
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0190);
    CHECK_EQ(rd32(pkt(o, 0).data(), 5), 110000001u);
}

TEST(ctl_client_skill_during_attack_resumes_it) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    w.apply(EvAction{kSelf, 110000001, 0}, 1500);
    c.onClientSkill(1600);  // a skill from the keyboard ends the attack too
    CHECK(c.tick(pad(), w, view(), 1800).packets.empty());
    TickOutput o = c.tick(pad(), w, view(), 1900);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0190);
    Controller calm(Config{});
    calm.onClientSkill(100);  // not attacking: nothing to resume
    CHECK(calm.tick(pad(), w, view(), 500).packets.empty());
}

TEST(ctl_attack_asked_again_when_hits_stop) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    w.apply(EvAction{kSelf, 110000001, 0}, 1500);  // our hit
    CHECK(c.tick(pad(), w, view(), 2400).packets.empty());  // 0.9 s since the hit: fine
    TickOutput o = c.tick(pad(), w, view(), 2500);         // 1 s without a hit: the attack stopped somehow
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0190);
    CHECK(c.tick(pad(), w, view(), 2600).packets.empty());
    w.apply(EvSelfWalk{Cell{100, 100}, Cell{101, 100}}, 3400);  // the client walks us up to it
    CHECK(c.tick(pad(), w, view(), 3500).packets.empty());     // a request would stop that walk
    o = c.tick(pad(), w, view(), 3600);                        // arrived, still no hit: ask again
    CHECK_EQ(o.packets.size(), 1u);
}

TEST(ctl_give_up_counts_from_the_first_request) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});  // our hits never land
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    CHECK_EQ(c.tick(pad(), w, view(), 2000).packets.size(), 1u);  // asked again
    CHECK_EQ(c.tick(pad(), w, view(), 3000).packets.size(), 1u);  // and again
    c.tick(pad(), w, view(), 5000);  // 4 s after the first request: give up anyway
    CHECK_EQ(c.target(), 0u);
}

TEST(ctl_mouse_click_hands_the_fight_to_the_mouse) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    ViewInfo clicked = view();
    clicked.mouseClicked = true;  // the player clicked with the real mouse, e.g. to walk away
    c.tick(pad(), w, clicked, 1200);
    CHECK(!c.attacking());
    CHECK_EQ(c.target(), 110000001u);  // the target stays for A and the skills
    CHECK(c.tick(pad(), w, view(), 3000).packets.empty());  // nothing pulls the player back
}

// --- slots ---

TEST(ctl_slot_buttons_cover_the_first_row) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{101, 100});
    uint32_t t = 1000;
    auto tap = [&](const PadState& p) {
        TickOutput o = c.tick(p, w, view(), t);
        c.tick(pad(), w, view(), t + 10);
        t += 200;
        return o;
    };
    auto one = [](const TickOutput& o, uint16_t id) { return o.packets.size() == 1 && pid(pkt(o, 0)) == id; };
    TickOutput o = tap(pad(kPadX));  // F1: Bash on the nearest monster, which becomes the target
    CHECK(one(o, 0x0072) && rd16(pkt(o, 0).data(), 10) == 5);
    CHECK_EQ(c.target(), 110000001u);
    o = tap(pad(kPadY));  // F2: Heal on ourselves
    CHECK(one(o, 0x0072) && rd16(pkt(o, 0).data(), 10) == 28 && rd32(pkt(o, 0).data(), 21) == kSelf);
    o = tap(pad(kPadB));  // F3: Safety Wall under ourselves
    CHECK(one(o, 0x0113) && rd16(pkt(o, 0).data(), 9) == 12 && rd16(pkt(o, 0).data(), 12) == 100);
    o = tap(pad(kPadRB));  // F4: Thunderstorm on the target's cell
    CHECK(one(o, 0x0113) && rd16(pkt(o, 0).data(), 9) == 21 && rd16(pkt(o, 0).data(), 12) == 101);
    o = tap(padRT(0));  // F5: Red Potion
    CHECK(one(o, 0x009f));
    o = tap(pad(kPadLB));  // F6: Orange Potion, none in the inventory
    CHECK(o.packets.empty() && o.rumble == Rumble::Denied);
    o = tap(pad(kPadUp));  // F7: Fire Bolt on the target
    CHECK(one(o, 0x0072) && rd16(pkt(o, 0).data(), 10) == 19 && rd32(pkt(o, 0).data(), 21) == 110000001u);
    o = tap(pad(kPadRight));  // F8: empty
    CHECK(o.packets.empty() && o.rumble == Rumble::Denied);
    o = tap(pad(kPadDown));  // F9: Ankle Snare, next to us towards the target
    CHECK(one(o, 0x0113) && rd16(pkt(o, 0).data(), 9) == 117);
    CHECK(!c.attacking());  // no button but A starts an attack
}

TEST(ctl_triggers_have_hysteresis) {
    Controller c(Config{});
    WorldState w = slotWorld();
    PadState p = pad();
    p.rt = 0.6f;  // past half-way: F5
    CHECK_EQ(c.tick(p, w, view(), 1000).packets.size(), 1u);
    p.rt = 0.45f;  // eased off a little: still held, no new press
    CHECK(c.tick(p, w, view(), 1100).packets.empty());
    p.rt = 0.2f;  // released
    c.tick(p, w, view(), 1200);
    p.rt = 0.6f;  // pressed again
    CHECK_EQ(c.tick(p, w, view(), 1300).packets.size(), 1u);
}

TEST(ctl_held_slot_repeats) {
    Controller c(Config{});  // every 250 ms
    WorldState w = slotWorld();
    CHECK_EQ(c.tick(pad(kPadY), w, view(), 1000).packets.size(), 1u);  // Heal
    CHECK(c.tick(pad(kPadY), w, view(), 1100).packets.empty());
    CHECK_EQ(c.tick(pad(kPadY), w, view(), 1250).packets.size(), 1u);
    CHECK(c.tick(pad(kPadY), w, view(), 1400).packets.empty());
    CHECK_EQ(c.tick(pad(kPadY), w, view(), 1500).packets.size(), 1u);
    Config once;
    once.repeatMs = 0;
    Controller c2(once);
    CHECK_EQ(c2.tick(pad(kPadY), w, view(), 1000).packets.size(), 1u);
    CHECK(c2.tick(pad(kPadY), w, view(), 2000).packets.empty());
}

TEST(ctl_held_slot_waits_while_approaching) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{105, 100});
    TickOutput o = c.tick(pad(kPadX), w, view(), 1000);  // Bash, range 1: walk there first
    CHECK(c.approaching());
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x00a7);
    o = c.tick(pad(kPadX), w, view(), 1300);  // held: no repeat on the way, only the next step
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x00a7);
    w.apply(EvSetPos{kSelf, Cell{104, 100}}, 1400);
    o = c.tick(pad(kPadX), w, view(), 1450);  // arrived: the Bash goes out
    CHECK(!c.approaching());
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0072);
    CHECK(c.tick(pad(kPadX), w, view(), 1460).packets.empty());  // the slot just fired
    o = c.tick(pad(kPadX), w, view(), 1710);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0072);
}

TEST(ctl_no_walking_up_while_the_stick_walks) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{112, 100});  // 12 cells east: beyond Fire Bolt's 9
    PadState p = pad(kPadUp);              // F7: Fire Bolt while walking west, away from it
    p.lx = -1.0f;
    TickOutput o = c.tick(p, w, view(), 1000);
    CHECK(!c.approaching());
    CHECK(o.packets.empty());  // no walk towards it, against the stick
    CHECK(o.rumble == Rumble::Denied);
    CHECK_EQ(c.target(), 0u);
    w.apply(EvSetPos{110000001, Cell{108, 100}}, 1200);  // it came within range
    o = c.tick(p, w, view(), 1250);                     // F7 still held: the repeat casts while we walk on
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0072);
    CHECK(o.mouse.active && o.mouse.lmb);
}

TEST(ctl_no_pickup_walk_while_the_stick_walks) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvGroundItem{4242, 909, Cell{104, 100}}, 0);  // 4 cells away
    PadState p = padLT(0);
    p.lx = -1.0f;
    TickOutput o = c.tick(p, w, view(), 1000);
    CHECK(!c.approaching());
    CHECK(o.packets.empty());
    CHECK(o.rumble == Rumble::Denied);
    w.apply(EvGroundItem{4243, 909, Cell{99, 100}}, 1100);  // one right next to us
    o = c.tick(p, w, view(), 1250);                        // LT still held: picked up on the way
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x00f5);
}

TEST(ctl_slot_throttle_is_per_slot) {
    Controller c(Config{});
    WorldState w = slotWorld();
    TickOutput o = c.tick(padRT(kPadY), w, view(), 1000);  // two slots at once: both go out
    CHECK_EQ(o.packets.size(), 2u);
    c.tick(pad(), w, view(), 1050);
    o = c.tick(pad(kPadY), w, view(), 1100);  // the same slot again 100 ms later: too soon
    CHECK(o.packets.empty());
    CHECK(o.rumble == Rumble::None);
    c.tick(pad(), w, view(), 1120);
    CHECK_EQ(c.tick(pad(kPadY), w, view(), 1160).packets.size(), 1u);
}

TEST(ctl_slot_enemy_skill_targets_nearest_then_approaches) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{105, 100});  // distance 5
    TickOutput o = c.tick(pad(kPadUp), w, view(), 1000);  // F7: Fire Bolt, range 9
    CHECK_EQ(c.target(), 110000001u);
    CHECK_EQ(o.mark, 110000001u);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0072);
    CHECK_EQ(rd16(pkt(o, 0).data(), 10), 19);
    CHECK_EQ(rd32(pkt(o, 0).data(), 21), 110000001u);
    c.tick(pad(), w, view(), 1100);
    o = c.tick(pad(kPadX), w, view(), 1300);  // F1: Bash, range 1: walk first
    CHECK(c.approaching());
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x00a7);
    CHECK(decodePos3(pkt(o, 0).data(), 5) == (Cell{105, 100}));
    w.apply(EvSetPos{kSelf, Cell{104, 100}}, 1400);  // arrived next to it
    o = c.tick(pad(), w, view(), 1450);
    CHECK(!c.approaching());
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0072);
    CHECK_EQ(rd16(pkt(o, 0).data(), 10), 5);
}

TEST(ctl_empty_slot_hint_when_server_sent_no_bar) {
    Controller c(Config{});
    WorldState w = world();  // the server never sent 0x02b9 (HOTKEY_SAVING off)
    TickOutput o = c.tick(pad(kPadX), w, view(), 1000);
    CHECK(o.rumble == Rumble::Denied);
    CHECK(!o.notes.empty() && o.notes.back().find("hotkey bar") != std::string::npos);
    Controller c2(Config{});
    WorldState known = slotWorld();  // the bar arrived; F8 is just empty
    o = c2.tick(pad(kPadRight), known, view(), 1000);
    CHECK(!o.notes.empty() && o.notes.back().find("hotkey bar") == std::string::npos);
}

TEST(ctl_held_empty_slot_stays_quiet) {
    Controller c(Config{});
    WorldState w = slotWorld();
    TickOutput o = c.tick(pad(kPadRight), w, view(), 1000);  // F8 is empty
    CHECK(o.rumble == Rumble::Denied);
    o = c.tick(pad(kPadRight), w, view(), 1300);  // held: no buzzing every 250 ms
    CHECK(o.rumble == Rumble::None);
    CHECK(o.notes.empty());
}

// --- LT: pickup, D-pad left: sit ---

TEST(ctl_pickup_nearest_with_approach) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvGroundItem{4242, 909, Cell{104, 100}}, 0);  // distance 4
    w.apply(EvGroundItem{4243, 909, Cell{100, 101}}, 0);  // distance 1
    w.apply(EvGroundItem{4244, 909, Cell{120, 100}}, 0);  // distance 20: out of reach
    TickOutput o = c.tick(padLT(0), w, view(), 1000);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x00f5);
    CHECK_EQ(rd32(pkt(o, 0).data(), 4), 4243u);
    c.tick(pad(), w, view(), 1010);
    w.apply(EvGroundItemGone{4243}, 1020);
    o = c.tick(padLT(0), w, view(), 1100);
    CHECK(c.approaching());
    CHECK_EQ(pid(pkt(o, 0)), 0x00a7);
    o = c.tick(pad(), w, view(), 1200);  // 100 ms: no new step yet
    CHECK(o.packets.empty());
    o = c.tick(pad(), w, view(), 1350);  // 250 ms: step again
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x00a7);
    w.apply(EvSetPos{kSelf, Cell{103, 100}}, 1400);
    o = c.tick(pad(), w, view(), 1450);
    CHECK_EQ(pid(pkt(o, 0)), 0x00f5);
    CHECK_EQ(rd32(pkt(o, 0).data(), 4), 4242u);
    CHECK(!c.approaching());
}

TEST(ctl_held_pickup_takes_items_one_by_one) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvGroundItem{4242, 909, Cell{101, 100}}, 0);
    w.apply(EvGroundItem{4243, 909, Cell{100, 101}}, 0);
    TickOutput o = c.tick(padLT(0), w, view(), 1000);
    CHECK_EQ(rd32(pkt(o, 0).data(), 4), 4242u);  // equally close: the lower id
    w.apply(EvGroundItemGone{4242}, 1100);
    o = c.tick(padLT(0), w, view(), 1250);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(rd32(pkt(o, 0).data(), 4), 4243u);
}

TEST(ctl_pickup_nothing_denied_and_approach_timeout) {
    Controller c(Config{});
    WorldState w = world();
    TickOutput o = c.tick(padLT(0), w, view(), 1000);
    CHECK(o.rumble == Rumble::Denied);
    c.tick(pad(), w, view(), 1010);
    w.apply(EvGroundItem{4242, 909, Cell{108, 100}}, 1020);
    c.tick(padLT(0), w, view(), 1100);
    CHECK(c.approaching());
    c.tick(pad(), w, view(), 6100);  // 5 s and still not there
    CHECK(!c.approaching());
}

TEST(ctl_stick_cancels_approach) {
    Controller c(Config{});
    WorldState w = world();
    w.apply(EvGroundItem{4242, 909, Cell{108, 100}}, 0);
    c.tick(padLT(0), w, view(), 1000);
    CHECK(c.approaching());
    PadState p = pad();
    p.lx = 1.0f;
    c.tick(p, w, view(), 1100);
    CHECK(!c.approaching());
}

TEST(ctl_sit_stand_on_dpad_left) {
    Controller c(Config{});
    WorldState w = world();
    TickOutput o = c.tick(pad(kPadLeft), w, view(), 1000);
    CHECK_EQ(pid(pkt(o, 0)), 0x0190);
    CHECK_EQ(pkt(o, 0)[18], 2);  // sit
    c.tick(pad(), w, view(), 1010);
    w.apply(EvAction{kSelf, 0, 2}, 1050);  // the server confirms: sitting
    o = c.tick(pad(kPadLeft), w, view(), 1100);
    CHECK_EQ(pkt(o, 0)[18], 3);  // stand
    CHECK(c.tick(pad(kPadLeft), w, view(), 1500).packets.empty());  // held: no repeat
}

// --- R3 and the right stick ---

TEST(ctl_r3_clears_target_and_stops_attack) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});
    c.tick(pad(kPadA), w, view(), 1000);
    c.tick(pad(), w, view(), 1100);
    TickOutput o = c.tick(pad(kPadR3), w, view(), 1200);
    CHECK_EQ(c.target(), 0u);
    CHECK(!c.attacking());
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0118);
    CHECK(o.rumble == Rumble::TargetCleared);
    c.tick(pad(), w, view(), 1300);
    o = c.tick(pad(kPadR3), w, view(), 1400);  // nothing to clear
    CHECK(o.packets.empty());
    CHECK(o.rumble == Rumble::None);
}

TEST(ctl_r3_also_stops_a_walk_up) {
    Controller c(Config{});
    WorldState w = slotWorld();
    addMob(w, 110000001, Cell{112, 100});  // out of Fire Bolt's range
    c.tick(pad(kPadUp), w, view(), 1000);  // walking up to cast
    CHECK(c.approaching());
    w.apply(EvSelfWalk{Cell{100, 100}, Cell{111, 100}}, 1050);  // the server walks us
    c.tick(pad(), w, view(), 1100);
    TickOutput o = c.tick(pad(kPadR3), w, view(), 1300);
    CHECK(!c.approaching());
    CHECK_EQ(o.packets.size(), 2u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0118);
    CHECK_EQ(pid(pkt(o, 1)), 0x00a7);  // and stop where we are
    CHECK(decodePos3(pkt(o, 1).data(), 5) == w.selfPos(1300));
}

TEST(ctl_right_stick_picks_in_direction) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{105, 100});  // east
    addMob(w, 110000002, Cell{100, 106});  // north
    TickOutput o = c.tick(padR(1, 0), w, view(), 1000);
    CHECK_EQ(c.target(), 110000001u);
    CHECK(o.rumble == Rumble::TargetSwitch);
    CHECK_EQ(o.mark, 110000001u);
    CHECK(o.packets.empty());  // choosing is not attacking
    CHECK(!c.attacking());
    o = c.tick(padR(0, 1), w, view(), 1100);  // stick up: north
    CHECK_EQ(c.target(), 110000002u);
    CHECK_EQ(o.mark, 110000002u);
    c.tick(pad(), w, view(), 1200);  // released: the choice stays
    CHECK_EQ(c.target(), 110000002u);
    o = c.tick(padR(0, -1), w, view(), 1300);  // nothing to the south
    CHECK_EQ(c.target(), 110000002u);
    CHECK(o.rumble == Rumble::None);
}

TEST(ctl_right_stick_thresholds_and_switch_margin) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{105, 100});  // east, aim score 10
    addMob(w, 110000002, Cell{104, 101});  // 14 degrees north of east, 4.1 cells
    c.tick(padR(0.5f, 0), w, view(), 1000);  // below 0.6: not aiming yet
    CHECK_EQ(c.target(), 0u);
    c.tick(padR(1, 0), w, view(), 1100);
    CHECK_EQ(c.target(), 110000001u);
    c.tick(padR(1, 0.15f), w, view(), 1200);  // the other one is better now, but only by 4.8
    CHECK_EQ(c.target(), 110000001u);
    c.tick(padR(1, 0.25f), w, view(), 1300);  // straight at it: better by 15.8
    CHECK_EQ(c.target(), 110000002u);
    c.tick(padR(0.5f, 0), w, view(), 1400);  // 0.5 keeps aiming once started (off below 0.35)
    CHECK_EQ(c.target(), 110000001u);
    c.tick(padR(0.3f, 0), w, view(), 1500);  // let go
    c.tick(padR(0, 0.5f), w, view(), 1600);  // 0.5 does not start aiming again
    CHECK_EQ(c.target(), 110000001u);
}

TEST(ctl_right_stick_redirects_running_attack) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{102, 100});  // east, nearest
    addMob(w, 110000002, Cell{100, 103});  // north
    c.tick(pad(kPadA), w, view(), 1000);
    CHECK_EQ(c.target(), 110000001u);
    c.tick(pad(), w, view(), 1100);
    TickOutput o = c.tick(padR(0, 1), w, view(), 1400);
    CHECK_EQ(c.target(), 110000002u);
    CHECK_EQ(o.packets.size(), 1u);
    CHECK_EQ(pid(pkt(o, 0)), 0x0190);
    CHECK_EQ(rd32(pkt(o, 0).data(), 5), 110000002u);
}

TEST(ctl_r3_holds_aim_until_stick_is_released) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{105, 100});
    c.tick(padR(1, 0), w, view(), 1000);
    CHECK_EQ(c.target(), 110000001u);
    TickOutput o = c.tick(padR(1, 0, kPadR3), w, view(), 1100);  // clicked the stick while pushing it
    CHECK_EQ(c.target(), 0u);
    CHECK(o.rumble == Rumble::TargetCleared);
    c.tick(padR(1, 0), w, view(), 1200);
    CHECK_EQ(c.target(), 0u);  // no re-pick until the stick comes back
    c.tick(pad(), w, view(), 1300);
    c.tick(padR(1, 0), w, view(), 1400);
    CHECK_EQ(c.target(), 110000001u);
}

TEST(ctl_mark_each_monster_at_most_once_a_second) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{105, 100});  // east
    addMob(w, 110000002, Cell{100, 106});  // north
    CHECK_EQ(c.tick(padR(1, 0), w, view(), 1000).mark, 110000001u);
    CHECK_EQ(c.tick(padR(0, 1), w, view(), 1100).mark, 110000002u);
    TickOutput o = c.tick(padR(1, 0), w, view(), 1200);  // back to the first one
    CHECK_EQ(c.target(), 110000001u);
    CHECK_EQ(o.mark, 0u);  // its "!" is still showing
    c.tick(padR(0, 1), w, view(), 2050);
    o = c.tick(padR(1, 0), w, view(), 2100);
    CHECK_EQ(o.mark, 110000001u);
}

// --- camera ---

TEST(ctl_right_stick_follows_camera_turn) {
    Controller c(Config{});
    WorldState w = world();
    addMob(w, 110000001, Cell{105, 100});  // east
    addMob(w, 110000002, Cell{100, 106});  // north
    PadState walkRight = pad();
    walkRight.lx = 1.0f;
    c.tick(walkRight, w, view(), 1000);       // the pad holds the walking point right of the centre
    c.onClientWalk(Cell{100, 107}, w, 1000);  // and the client walks north: the camera is turned
    CHECK(std::fabs(c.camera().radians() - kPi / 2) < 1e-6);
    c.tick(pad(), w, view(), 1100);
    c.tick(padR(1, 0), w, view(), 1200);  // right on the screen is north on the map now
    CHECK_EQ(c.target(), 110000002u);
}

TEST(ctl_camera_learns_only_from_a_steady_walking_point) {
    Config cfg;
    cfg.press = PressMode::Center;
    Controller c(cfg);
    WorldState w = world();
    PadState p = pad();
    p.lx = 1.0f;
    c.tick(p, w, view(), 1000);  // the first frame presses in the centre
    c.onClientWalk(Cell{100, 107}, w, 1000);
    CHECK_EQ(c.camera().samples(), 0u);
    c.tick(p, w, view(), 1020);  // on the ring now; the client may still act on the old point for a moment
    c.onClientWalk(Cell{100, 107}, w, 1030);
    CHECK_EQ(c.camera().samples(), 0u);
    p.ly = 0.1f;  // a slight wobble keeps the point steady
    c.tick(p, w, view(), 1100);
    c.onClientWalk(Cell{100, 107}, w, 1170);
    CHECK_EQ(c.camera().samples(), 1u);
    p.lx = 0;
    p.ly = 1.0f;  // turned the stick up
    c.tick(p, w, view(), 1200);
    c.onClientWalk(Cell{93, 100}, w, 1250);
    CHECK_EQ(c.camera().samples(), 1u);
    c.tick(pad(), w, view(), 1400);  // stopped: walks the player clicks with the mouse teach nothing
    c.onClientWalk(Cell{93, 100}, w, 1450);
    CHECK_EQ(c.camera().samples(), 1u);
}
