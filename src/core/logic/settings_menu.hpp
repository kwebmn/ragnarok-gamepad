#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/config/bindings.hpp"
#include "core/logic/pad.hpp"
#include "core/world/world.hpp"

namespace uapad {

// The settings menu behind Start, without any drawing: which row is chosen, what each button does.
// Rows: the bindable buttons in kBindableButtons order, then "reset everything".
class SettingsMenu {
public:
    enum class Result { None, Save, Cancel };
    struct Row {
        std::string button;  // UTF-8; empty on the "reset everything" row
        std::string action;  // UTF-8
        bool selected = false;
    };

    // `slotNames`: what the client's bar holds in each slot (UTF-8, empty when unknown), shown next to "Slot".
    // Buttons and stick directions held right now (the Start that opened the menu) count as already pressed.
    void open(const Bindings& current, const std::array<std::string, kHotkeySlots>& slotNames, uint16_t heldButtons,
              uint32_t nowMs, float lx = 0, float ly = 0);
    void close() { open_ = false; }
    void setLang(Lang lang) { lang_ = lang; }  // of the labels in rows()
    bool isOpen() const { return open_; }

    // One frame of the pad: up/down picks a row, left/right changes its action (held: repeats), A or Start
    // saves, B cancels, Y resets the row. On Save, bindings() is the new layout.
    Result update(const PadState& pad, uint32_t nowMs);
    // The mouse: pick a row; turn a button row's action one step.
    void select(size_t row);
    void change(size_t row, int dir);
    void resetAll() { bindings_ = defaultBindings(); }

    size_t selected() const { return selected_; }
    size_t rowCount() const { return kBindableCount + 1; }
    const Bindings& bindings() const { return bindings_; }
    std::vector<Row> rows() const;

private:
    // A direction held on the D-pad or the left stick: acts on the press, then again after a pause.
    struct Repeat {
        int dir = 0;
        bool live = true;  // false: held since before the menu opened, ignored until let go
        uint32_t nextMs = 0;
        void reset(int held);
        bool step(int d, uint32_t now);
    };

    bool open_ = false;
    Lang lang_ = Lang::En;
    Bindings bindings_{};
    std::array<std::string, kHotkeySlots> slotNames_{};
    size_t selected_ = 0;
    uint16_t prevButtons_ = 0;
    Repeat vertical_;
    Repeat horizontal_;
};

}  // namespace uapad
