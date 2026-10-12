#include "core/logic/settings_menu.hpp"

#include <cmath>

namespace uapad {
namespace {

constexpr uint32_t kFirstRepeatMs = 350;
constexpr uint32_t kRepeatMs = 120;
constexpr float kStickPress = 0.6f;
constexpr float kStickKeep = 0.4f;  // a push already under way lasts down to here

// The D-pad, else the stick on its dominant axis only: a diagonal push never moves both ways at once.
// `heldV` / `heldH`: that axis is pushed already, so it lets go at kStickKeep rather than kStickPress.
int verticalOf(uint16_t buttons, float lx, float ly, bool heldV = false) {
    if (buttons & kPadUp) return -1;  // up the list
    if (buttons & kPadDown) return 1;
    if (std::fabs(ly) < std::fabs(lx)) return 0;
    const float t = heldV ? kStickKeep : kStickPress;
    return ly > t ? -1 : ly < -t ? 1 : 0;
}

int horizontalOf(uint16_t buttons, float lx, float ly, bool heldH = false) {
    if (buttons & kPadLeft) return -1;
    if (buttons & kPadRight) return 1;
    if (std::fabs(lx) <= std::fabs(ly)) return 0;
    const float t = heldH ? kStickKeep : kStickPress;
    return lx < -t ? -1 : lx > t ? 1 : 0;
}

}  // namespace

void SettingsMenu::Repeat::reset(int held) {
    dir = held;
    live = held == 0;
}

bool SettingsMenu::Repeat::step(int d, uint32_t now) {
    if (d != dir) {
        dir = d;
        live = true;
        if (d == 0) return false;
        nextMs = now + kFirstRepeatMs;
        return true;
    }
    if (d == 0 || !live || int32_t(now - nextMs) < 0) return false;
    nextMs = now + kRepeatMs;
    return true;
}

void SettingsMenu::open(const Bindings& current, const std::array<std::string, kHotkeySlots>& slotNames,
                        uint16_t heldButtons, uint32_t nowMs, float lx, float ly) {
    (void)nowMs;
    open_ = true;
    bindings_ = current;
    slotNames_ = slotNames;
    selected_ = 0;
    prevButtons_ = heldButtons;
    vertical_.reset(verticalOf(heldButtons, lx, ly));
    horizontal_.reset(horizontalOf(heldButtons, lx, ly));
}

void SettingsMenu::select(size_t row) { selected_ = row < rowCount() ? row : rowCount() - 1; }

void SettingsMenu::change(size_t row, int dir) {
    select(row);
    if (selected_ < kBindableCount) bindings_[selected_] = nextAction(bindings_[selected_], dir);
}

SettingsMenu::Result SettingsMenu::update(const PadState& pad, uint32_t nowMs) {
    if (!open_) return Result::None;
    const uint16_t buttons = pad.connected ? pad.buttons : 0;
    const uint16_t down = uint16_t(buttons & ~prevButtons_);
    prevButtons_ = buttons;
    if (down & kPadB) return Result::Cancel;
    if (down & kPadStart) return Result::Save;
    if (down & kPadA) {
        if (selected_ < kBindableCount) return Result::Save;
        resetAll();  // the last row: "reset everything"
        return Result::None;
    }
    if ((down & kPadY) && selected_ < kBindableCount) bindings_[selected_] = defaultBindings()[selected_];
    const float lx = pad.connected ? pad.lx : 0;
    const float ly = pad.connected ? pad.ly : 0;
    const int v = verticalOf(buttons, lx, ly, vertical_.dir != 0);
    if (vertical_.step(v, nowMs)) {
        const int n = int(rowCount());
        selected_ = size_t((int(selected_) + v + n) % n);
    }
    const int h = horizontalOf(buttons, lx, ly, horizontal_.dir != 0);
    if (horizontal_.step(h, nowMs) && selected_ < kBindableCount)
        bindings_[selected_] = nextAction(bindings_[selected_], h);
    return Result::None;
}

std::vector<SettingsMenu::Row> SettingsMenu::rows() const {
    std::vector<Row> rows;
    rows.reserve(rowCount());
    for (size_t i = 0; i < kBindableCount; ++i) {
        const Action& a = bindings_[i];
        std::string action = actionLabel(a);
        if (a.kind == Action::Kind::Slot && a.slot < slotNames_.size() && !slotNames_[a.slot].empty())
            action += " — " + slotNames_[a.slot];
        rows.push_back(Row{buttonLabel(i), action, selected_ == i});
    }
    rows.push_back(Row{"", "Вернуть всё по умолчанию", selected_ == kBindableCount});
    return rows;
}

}  // namespace uapad
