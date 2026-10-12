#include "core/world/world.hpp"
#include "test.hpp"

using namespace uapad;

static constexpr uint32_t kMe = 2000001;

static WorldState inGame() {
    WorldState w;
    w.reset(kMe);
    w.apply(EvAuthOk{Cell{100, 100}}, 0);
    return w;
}

TEST(world_self_position_and_walk_interpolation) {
    WorldState w;
    w.reset(kMe);
    CHECK(!w.inGame());
    w.apply(EvAuthOk{Cell{100, 100}}, 0);
    CHECK(w.inGame());
    CHECK(w.selfPos(0) == (Cell{100, 100}));
    w.apply(EvSelfWalk{Cell{100, 100}, Cell{104, 100}}, 1000);
    CHECK(w.selfPos(1000) == (Cell{100, 100}));
    CHECK(w.selfPos(1300) == (Cell{102, 100}));  // 150 ms per cell
    CHECK(w.selfPos(5000) == (Cell{104, 100}));
    w.apply(EvSetPos{kMe, Cell{50, 60}}, 6000);
    CHECK(w.selfPos(6000) == (Cell{50, 60}));
}

TEST(world_monsters_pets_players) {
    WorldState w = inGame();
    w.apply(EvActor{110000001, 1002, 0, 400, Cell{105, 100}, Cell{105, 100}}, 0);    // Poring
    w.apply(EvActor{110000002, 1002, 100, 200, Cell{106, 100}, Cell{106, 100}}, 0);  // someone's pet
    w.apply(EvActor{2000002, 7, 3, 150, Cell{90, 90}, Cell{90, 90}}, 0);             // a player (job 7)
    w.apply(EvActor{110000003, 1113, 0, 200, Cell{110, 100}, Cell{110, 100}}, 0);    // our own pet
    w.apply(EvPetInfo{0, 110000003}, 0);
    w.apply(EvActor{kMe, 0, 0, 150, Cell{1, 1}, Cell{1, 1}}, 0);                     // ourselves: ignored
    CHECK_EQ(w.actors().size(), 4u);
    CHECK(w.isMonster(w.actors().at(110000001)));
    CHECK(!w.isMonster(w.actors().at(110000002)));
    CHECK(!w.isMonster(w.actors().at(2000002)));
    CHECK(!w.isMonster(w.actors().at(110000003)));
    w.apply(EvVanish{110000001, 1}, 10);
    CHECK_EQ(w.actors().count(110000001), 0u);
}

TEST(world_actor_walk_interpolation_and_move) {
    WorldState w = inGame();
    w.apply(EvActor{110000001, 1002, 0, 200, Cell{10, 10}, Cell{10, 20}}, 1000);
    CHECK(*w.actorPos(110000001, 1000) == (Cell{10, 10}));
    CHECK(*w.actorPos(110000001, 2000) == (Cell{10, 15}));
    w.apply(EvActorMove{110000001, Cell{10, 15}, Cell{20, 15}}, 2000);
    CHECK(*w.actorPos(110000001, 2400) == (Cell{12, 15}));
    CHECK(!w.actorPos(999, 0).has_value());
}

TEST(world_hits_sitting_death) {
    WorldState w = inGame();
    w.apply(EvAction{kMe, 110000001, 0}, 500);
    CHECK(w.lastHitMs(110000001) && *w.lastHitMs(110000001) == 500u);
    w.apply(EvSkillHit{19, kMe, 110000002}, 600);
    CHECK(w.lastHitMs(110000002).has_value());
    w.apply(EvAction{110000001, kMe, 0}, 700);  // a monster hits us: not our hit
    CHECK(!w.lastHitMs(kMe).has_value());
    w.apply(EvAction{kMe, 0, 2}, 800);
    CHECK(w.sitting());
    CHECK(!w.lastHitMs(0).has_value());
    w.apply(EvAction{kMe, 0, 3}, 900);
    CHECK(!w.sitting());
    w.apply(EvVanish{kMe, 1}, 1000);
    CHECK(w.dead());
    w.apply(EvResurrect{kMe}, 1100);
    CHECK(!w.dead());
}

TEST(world_map_change_clears_area) {
    WorldState w = inGame();
    w.apply(EvActor{110000001, 1002, 0, 200, Cell{2, 2}, Cell{2, 2}}, 0);
    w.apply(EvGroundItem{4242, 501, Cell{3, 3}}, 0);
    w.apply(EvAction{kMe, 110000001, 0}, 50);
    w.apply(EvVanish{kMe, 1}, 60);
    w.apply(EvMapChange{"prt_fild08.gat", Cell{50, 50}}, 100);
    CHECK(w.actors().empty());
    CHECK(w.groundItems().empty());
    CHECK(!w.lastHitMs(110000001).has_value());
    CHECK(!w.dead());
    CHECK(w.selfPos(100) == (Cell{50, 50}));
}

TEST(world_inventory_tracking) {
    WorldState w = inGame();
    w.apply(EvInventory{{InvEntry{2, 501, 15, false}, InvEntry{3, 601, 4, false}}}, 0);
    auto red = w.findUsable(501);
    CHECK(red && red->index == 2 && red->amount == 15);
    w.apply(EvItemUsed{2, 14, kMe, true}, 10);
    red = w.findUsable(501);
    CHECK(red && red->amount == 14);
    w.apply(EvItemUsed{3, 0, 2000002, true}, 20);  // someone else used an item nearby: ignored
    CHECK(w.findUsable(601).has_value());
    w.apply(EvItemRemove{3, 4}, 30);
    CHECK(!w.findUsable(601).has_value());
    w.apply(EvItemAdd{InvEntry{2, 501, 5, false}}, 40);
    red = w.findUsable(501);
    CHECK(red && red->amount == 19);
    w.apply(EvItemAdd{InvEntry{9, 1201, 1, true}}, 50);  // Knife: equipment is not "usable"
    CHECK(!w.findUsable(1201).has_value());
}

TEST(world_skills_and_hotkeys) {
    WorldState w = inGame();
    w.apply(EvSkills{{SkillInfo{5, 1, 10, 1}, SkillInfo{28, 16, 5, 9}}}, 0);
    CHECK(w.skill(5) && w.skill(5)->inf == 1u);
    w.apply(EvSkillUp{28, 6, 9}, 10);
    CHECK(w.skill(28) && w.skill(28)->lv == 6 && w.skill(28)->inf == 16u);
    CHECK(!w.skill(999).has_value());
    EvHotkeys hk;
    hk.keys[0] = Hotkey{1, 5, 10};
    w.apply(hk, 20);
    CHECK(w.hotkey(0) == (Hotkey{1, 5, 10}));
    w.setHotkey(4, Hotkey{0, 501, 0});
    CHECK_EQ(w.hotkey(4).id, 501u);
    CHECK_EQ(w.hotkey(99).id, 0u);
    CHECK_EQ(w.hotkey(-1).id, 0u);
}

TEST(world_self_speed_changes_interpolation) {
    WorldState w = inGame();
    w.apply(EvSelfSpeed{100}, 0);
    w.apply(EvSelfWalk{Cell{100, 100}, Cell{110, 100}}, 1000);
    CHECK(w.selfPos(1300) == (Cell{103, 100}));  // 100 ms per cell, not the default 150
    w.apply(EvSelfSpeed{50}, 1300);              // sped up in the middle of the walk
    CHECK(w.selfPos(1300) == (Cell{103, 100}));  // no jump at the change
    CHECK(w.selfPos(1500) == (Cell{107, 100}));  // 4 more cells in 200 ms
}

TEST(world_last_move) {
    WorldState w = inGame();
    CHECK(!w.lastMove());
    w.apply(EvSelfWalk{Cell{100, 100}, Cell{97, 104}}, 1000);
    CHECK(w.lastMove() && *w.lastMove() == (Cell{-3, 4}));
    w.apply(EvSelfWalk{Cell{97, 104}, Cell{97, 104}}, 2000);  // a walk to where we stand keeps the last step
    CHECK(w.lastMove() && *w.lastMove() == (Cell{-3, 4}));
    w.apply(EvMapChange{"prontera", Cell{150, 150}}, 3000);
    CHECK(!w.lastMove());
}

TEST(world_self_moving) {
    WorldState w = inGame();
    CHECK(!w.selfMoving(0));
    w.apply(EvSelfWalk{Cell{100, 100}, Cell{104, 100}}, 1000);
    CHECK(w.selfMoving(1000));
    CHECK(w.selfMoving(1500));
    CHECK(!w.selfMoving(1600));  // 4 cells at 150 ms
}
