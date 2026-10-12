#include "core/logic/slots.hpp"
#include "fixtures.hpp"
#include "test.hpp"

using namespace uapad;

static const std::vector<uint16_t> kGroundSelf{12, 25, 27, 70};

static SlotDecision slot(const WorldState& w, int index, uint32_t target, uint32_t nearest) {
    return decideSlot(w.hotkey(index), w, target, nearest, kGroundSelf, 0);
}

TEST(slots_item_uses_inventory_index) {
    WorldState w = fx::slotWorld();
    SlotDecision d = slot(w, 4, 0, 0);
    CHECK(d.kind == SlotDecision::Kind::Send);
    CHECK_EQ(rd16(d.packet.data(), 0), 0x009f);
    CHECK_EQ(rd16(d.packet.data(), 4), 2);
    CHECK_EQ(rd32(d.packet.data(), 10), fx::kSelf);
}

TEST(slots_missing_item_empty_unlearned_passive_are_denied) {
    WorldState w = fx::slotWorld();
    CHECK(slot(w, 5, 0, 0).kind == SlotDecision::Kind::Deny);   // Orange Potion not in inventory
    CHECK(slot(w, 7, 0, 0).kind == SlotDecision::Kind::Deny);   // empty slot
    CHECK(slot(w, 9, 0, 0).kind == SlotDecision::Kind::Deny);   // passive skill
    CHECK(!slot(w, 9, 0, 0).reason.empty());
    w.setHotkey(8, Hotkey{1, 46, 5});                           // Double Strafe: not learned
    CHECK(slot(w, 8, 0, 0).kind == SlotDecision::Kind::Deny);
}

TEST(slots_support_skill_on_self_with_clamped_level) {
    WorldState w = fx::slotWorld();
    SlotDecision d = slot(w, 1, 0, 0);  // Heal lv 10 on the bar, learned lv 5
    CHECK(d.kind == SlotDecision::Kind::Send);
    CHECK_EQ(rd16(d.packet.data(), 0), 0x0072);
    CHECK_EQ(rd16(d.packet.data(), 6), 5);
    CHECK_EQ(rd16(d.packet.data(), 10), 28);
    CHECK_EQ(rd32(d.packet.data(), 21), fx::kSelf);
    CHECK_EQ(d.newTarget, 0u);
}

TEST(slots_level_zero_on_bar_means_learned_level) {
    WorldState w = fx::slotWorld();
    w.setHotkey(8, Hotkey{1, 28, 0});
    SlotDecision d = slot(w, 8, 0, 0);
    CHECK_EQ(rd16(d.packet.data(), 6), 5);
}

TEST(slots_enemy_skill_on_target_in_range) {
    WorldState w = fx::slotWorld();
    fx::addMob(w, 110000001, Cell{105, 100});
    SlotDecision d = slot(w, 6, 110000001, 0);  // Fire Bolt range 9, distance 5
    CHECK(d.kind == SlotDecision::Kind::Send);
    CHECK_EQ(rd16(d.packet.data(), 0), 0x0072);
    CHECK_EQ(rd16(d.packet.data(), 6), 5);
    CHECK_EQ(rd16(d.packet.data(), 10), 19);
    CHECK_EQ(rd32(d.packet.data(), 21), 110000001u);
    CHECK_EQ(d.newTarget, 0u);
}

TEST(slots_enemy_skill_without_target_takes_nearest) {
    WorldState w = fx::slotWorld();
    fx::addMob(w, 110000001, Cell{105, 100});
    SlotDecision d = slot(w, 6, 0, 110000001);
    CHECK(d.kind == SlotDecision::Kind::Send);
    CHECK_EQ(d.newTarget, 110000001u);
    CHECK(slot(w, 6, 0, 0).kind == SlotDecision::Kind::Deny);  // no monster at all
}

TEST(slots_enemy_skill_out_of_range_approaches) {
    WorldState w = fx::slotWorld();
    fx::addMob(w, 110000001, Cell{105, 100});
    SlotDecision d = slot(w, 0, 110000001, 0);  // Bash range 1, distance 5
    CHECK(d.kind == SlotDecision::Kind::Approach);
    CHECK_EQ(d.actor, 110000001u);
    CHECK(!d.ground);
    CHECK_EQ(d.skill, 5);
    CHECK_EQ(d.lv, 10);
    CHECK_EQ(d.range, 1);
}

TEST(slots_ground_skill_cells) {
    WorldState w = fx::slotWorld();
    SlotDecision d = slot(w, 3, 0, 0);  // Thunderstorm without a target: under ourselves
    CHECK(d.kind == SlotDecision::Kind::Send);
    CHECK_EQ(rd16(d.packet.data(), 0), 0x0113);
    CHECK_EQ(rd16(d.packet.data(), 9), 21);
    CHECK_EQ(rd16(d.packet.data(), 12), 100);
    CHECK_EQ(rd16(d.packet.data(), 20), 100);
    fx::addMob(w, 110000001, Cell{104, 102});
    d = slot(w, 3, 110000001, 0);  // with a target: on its cell
    CHECK_EQ(rd16(d.packet.data(), 12), 104);
    CHECK_EQ(rd16(d.packet.data(), 20), 102);
    d = slot(w, 2, 110000001, 0);  // Safety Wall is in ground_self: under ourselves anyway
    CHECK_EQ(rd16(d.packet.data(), 9), 12);
    CHECK_EQ(rd16(d.packet.data(), 12), 100);
    CHECK_EQ(rd16(d.packet.data(), 20), 100);
    d = slot(w, 3, 0, 110000001);  // ground skills never pick the nearest monster on their own
    CHECK_EQ(rd16(d.packet.data(), 12), 100);
    CHECK_EQ(d.newTarget, 0u);
}

TEST(slots_ground_skill_out_of_range_approaches) {
    WorldState w = fx::slotWorld();
    fx::addMob(w, 110000001, Cell{112, 100});  // distance 12, Thunderstorm range 9
    SlotDecision d = slot(w, 3, 110000001, 0);
    CHECK(d.kind == SlotDecision::Kind::Approach);
    CHECK(d.ground);
    CHECK_EQ(d.range, 9);
}
