#include "core/logic/targeting.hpp"
#include "fixtures.hpp"
#include "test.hpp"

using namespace uapad;

TEST(targeting_nearest_first_monsters_only) {
    WorldState w = fx::world();
    fx::addMob(w, 110000003, Cell{106, 100});  // distance 6
    fx::addMob(w, 110000001, Cell{102, 101});  // distance 2
    fx::addMob(w, 110000002, Cell{97, 104});   // distance 4
    w.apply(EvActor{2000002, 7, 3, 150, Cell{101, 100}, Cell{101, 100}}, 0);          // player
    w.apply(EvActor{110000009, 1002, 100, 200, Cell{101, 101}, Cell{101, 101}}, 0);  // pet
    auto c = targetCandidates(w, 0, {});
    CHECK(c == (std::vector<uint32_t>{110000001, 110000002, 110000003}));
}

TEST(targeting_ties_broken_by_id) {
    WorldState w = fx::world();
    fx::addMob(w, 110000020, Cell{103, 100});
    fx::addMob(w, 110000010, Cell{100, 97});
    auto c = targetCandidates(w, 0, {});
    CHECK(c == (std::vector<uint32_t>{110000010, 110000020}));
}

TEST(targeting_blacklist_until_expiry) {
    WorldState w = fx::world();
    fx::addMob(w, 110000001, Cell{101, 100});
    fx::addMob(w, 110000002, Cell{103, 100});
    std::unordered_map<uint32_t, uint32_t> blacklist{{110000001, 5000}};
    CHECK(targetCandidates(w, 4999, blacklist) == (std::vector<uint32_t>{110000002}));
    CHECK(targetCandidates(w, 5000, blacklist) == (std::vector<uint32_t>{110000001, 110000002}));
}

TEST(targeting_uses_interpolated_positions) {
    WorldState w = fx::world();
    w.apply(EvActor{110000001, 1002, 0, 100, Cell{110, 100}, Cell{101, 100}}, 0);  // walking towards us
    fx::addMob(w, 110000002, Cell{104, 100});
    CHECK(targetCandidates(w, 0, {}).front() == 110000002u);
    CHECK(targetCandidates(w, 800, {}).front() == 110000001u);  // 8 cells walked: now at (102, 100)
}

TEST(targeting_cycle_wraps) {
    const std::vector<uint32_t> c{10, 20, 30};
    CHECK_EQ(cycleTarget(c, 0, +1), 10u);
    CHECK_EQ(cycleTarget(c, 0, -1), 10u);
    CHECK_EQ(cycleTarget(c, 10, +1), 20u);
    CHECK_EQ(cycleTarget(c, 30, +1), 10u);
    CHECK_EQ(cycleTarget(c, 10, -1), 30u);
    CHECK_EQ(cycleTarget({}, 10, +1), 0u);
    CHECK_EQ(cycleTarget({10}, 10, +1), 10u);
}

TEST(targeting_give_up_after_silence) {
    WorldState w = fx::world();
    fx::addMob(w, 110000001, Cell{102, 100});
    CHECK(!shouldGiveUp(w, 110000001, 1000, 4999, 4000));
    CHECK(shouldGiveUp(w, 110000001, 1000, 5000, 4000));
    w.apply(EvAction{fx::kSelf, 110000001, 0}, 4500);
    CHECK(!shouldGiveUp(w, 110000001, 1000, 8000, 4000));
    CHECK(shouldGiveUp(w, 110000001, 1000, 8500, 4000));
}
