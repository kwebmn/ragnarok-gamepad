#pragma once
#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

// The settings menu's window: drawn with GDI over the game window's client area. It never takes the focus,
// so the game keeps its keyboard and the pad keeps working; the mouse can click it.
namespace uapad::overlay {

struct View {
    std::wstring title;
    std::vector<std::wstring> buttons;  // per row; empty for the "reset everything" row
    std::vector<std::wstring> actions;
    size_t selected = 0;
    std::wstring help;
    bool operator==(const View& o) const {
        return title == o.title && buttons == o.buttons && actions == o.actions && selected == o.selected && help == o.help;
    }
};

enum class Click { Select, Previous, Next, Save, Cancel };
struct MouseEvent {
    Click click;
    size_t row;
};

// Shows the window (created on first use) or refreshes it, centred over `game`'s client area.
void show(HWND game, const View& view);
void hide();
// Clicks on the window since the last call.
std::vector<MouseEvent> takeClicks();

}  // namespace uapad::overlay
