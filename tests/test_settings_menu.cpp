#include "core/logic/settings_menu.hpp"
#include "test.hpp"

using namespace uapad;

namespace {

PadState padOf(uint16_t buttons, float lx = 0, float ly = 0) {
    PadState p;
    p.connected = true;
    p.buttons = buttons;
    p.lx = lx;
    p.ly = ly;
    return p;
}

size_t rowOf(uint16_t button) {
    for (size_t i = 0; i < kBindableButtons.size(); ++i)
        if (kBindableButtons[i] == button) return i;
    return 0;
}

SettingsMenu opened(uint16_t held = kPadStart) {
    SettingsMenu m;
    m.open(defaultBindings(), {}, held, 0);
    return m;
}

}  // namespace

TEST(menu_opening_press_is_not_a_press) {
    SettingsMenu m = opened();
    CHECK(m.isOpen());
    CHECK(m.update(padOf(kPadStart), 10) == SettingsMenu::Result::None);  // the Start that opened it
    CHECK(m.update(padOf(0), 20) == SettingsMenu::Result::None);
    CHECK(m.update(padOf(kPadStart), 30) == SettingsMenu::Result::Save);  // a new press closes and keeps
}

TEST(menu_moves_and_wraps) {
    SettingsMenu m = opened(0);
    CHECK_EQ(m.selected(), 0u);
    m.update(padOf(kPadDown), 10);
    m.update(padOf(0), 20);
    m.update(padOf(kPadDown), 30);
    CHECK_EQ(m.selected(), 2u);
    m.update(padOf(0), 40);
    m.update(padOf(kPadUp), 50);
    m.update(padOf(0), 60);
    m.update(padOf(kPadUp), 70);
    m.update(padOf(0), 80);
    m.update(padOf(kPadUp), 90);  // past the top: the last row ("reset all")
    CHECK_EQ(m.selected(), m.rowCount() - 1);
}

TEST(menu_hold_repeats_after_a_delay) {
    SettingsMenu m = opened(0);
    m.update(padOf(kPadDown), 1000);
    CHECK_EQ(m.selected(), 1u);
    m.update(padOf(kPadDown), 1200);
    CHECK_EQ(m.selected(), 1u);  // still the first press
    m.update(padOf(kPadDown), 1350);
    CHECK_EQ(m.selected(), 2u);  // 350 ms: repeating
    m.update(padOf(kPadDown), 1470);
    CHECK_EQ(m.selected(), 3u);  // then every 120 ms
    m.update(padOf(kPadDown), 1500);
    CHECK_EQ(m.selected(), 3u);
}

TEST(menu_changes_actions) {
    SettingsMenu m = opened(0);
    m.select(rowOf(kPadX));
    m.update(padOf(kPadRight), 10);  // slot F1 -> slot F2
    CHECK(m.bindings()[rowOf(kPadX)] == (Action{Action::Kind::Slot, 1}));
    m.update(padOf(0), 20);
    m.update(padOf(kPadLeft), 30);
    m.update(padOf(0), 40);
    m.update(padOf(kPadLeft), 50);  // -> slot F1 -> attack
    CHECK(m.bindings()[rowOf(kPadX)] == (Action{Action::Kind::Attack, 0}));
}

TEST(menu_stick_navigates_too) {
    SettingsMenu m = opened(0);
    m.update(padOf(0, 0, -1.0f), 10);  // stick down
    CHECK_EQ(m.selected(), 1u);
    m.update(padOf(0, 0, 0), 20);
    m.update(padOf(0, 1.0f, 0), 30);  // stick right: the B row's action moves on
    CHECK(m.bindings()[rowOf(kPadB)] == (Action{Action::Kind::Slot, 3}));
    m.update(padOf(0, 0.3f, 0), 40);  // inside the dead zone: released
    m.update(padOf(0, 1.0f, 0), 50);
    CHECK(m.bindings()[rowOf(kPadB)] == (Action{Action::Kind::Slot, 4}));
}

TEST(menu_save_and_cancel) {
    SettingsMenu m = opened(0);
    m.select(rowOf(kPadA));
    m.change(rowOf(kPadA), -1);  // attack -> none
    CHECK(m.update(padOf(kPadA), 10) == SettingsMenu::Result::Save);
    CHECK(m.bindings()[rowOf(kPadA)] == Action{});
    SettingsMenu c = opened(0);
    c.change(rowOf(kPadA), -1);
    CHECK(c.update(padOf(kPadB), 10) == SettingsMenu::Result::Cancel);
}

TEST(menu_resets_a_row_or_everything) {
    SettingsMenu m = opened(0);
    m.change(rowOf(kPadX), +1);
    m.change(rowOf(kPadY), +1);
    m.select(rowOf(kPadX));
    CHECK(m.update(padOf(kPadY), 10) == SettingsMenu::Result::None);  // Y: this row back to its default
    CHECK(m.bindings()[rowOf(kPadX)] == defaultBindings()[rowOf(kPadX)]);
    CHECK(m.bindings()[rowOf(kPadY)] != defaultBindings()[rowOf(kPadY)]);
    m.update(padOf(0), 20);
    m.select(m.rowCount() - 1);
    CHECK(m.update(padOf(kPadA), 30) == SettingsMenu::Result::None);  // "reset all" keeps the menu open
    CHECK(m.bindings() == defaultBindings());
    CHECK(m.isOpen());
}

TEST(menu_rows_show_what_is_in_the_slots) {
    SettingsMenu m;
    std::array<std::string, kHotkeySlots> slots{};
    slots[0] = "AC_DOUBLE";
    m.open(defaultBindings(), slots, 0, 0);
    const std::vector<SettingsMenu::Row> rows = m.rows();
    CHECK_EQ(rows.size(), m.rowCount());
    CHECK(rows[rowOf(kPadX)].button == "X");
    CHECK(rows[rowOf(kPadX)].action == "Слот F1 — AC_DOUBLE");
    CHECK(rows[rowOf(kPadY)].action == "Слот F2");  // nothing known about F2
    CHECK(rows[0].selected && !rows[1].selected);
    CHECK(rows.back().button.empty() && !rows.back().action.empty());
}

TEST(menu_diagonal_stick_moves_one_axis) {
    SettingsMenu m = opened(0);
    m.update(padOf(0, 0.8f, -0.7f), 10);  // mostly right, a bit down: change the action only
    CHECK_EQ(m.selected(), 0u);
    CHECK(m.bindings()[0] != defaultBindings()[0]);
    m.update(padOf(0, 0, 0), 20);
    m.update(padOf(0, 0.5f, -0.9f), 30);  // mostly down: next row only
    CHECK_EQ(m.selected(), 1u);
    CHECK(m.bindings()[1] == defaultBindings()[1]);
}

TEST(menu_stick_lets_go_lower_than_it_presses) {
    SettingsMenu m = opened(0);
    m.update(padOf(0, 0, -0.7f), 10);
    CHECK_EQ(m.selected(), 1u);
    m.update(padOf(0, 0, -0.5f), 20);  // eased off a little: still the same push
    m.update(padOf(0, 0, -0.7f), 30);
    CHECK_EQ(m.selected(), 1u);
    m.update(padOf(0, 0, -0.3f), 40);  // let go
    m.update(padOf(0, 0, -0.7f), 50);  // a new push
    CHECK_EQ(m.selected(), 2u);
}
