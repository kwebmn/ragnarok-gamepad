#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace uapad {

// The language of everything the player reads: the Start menu (the log stays English).
enum class Lang : uint8_t { En, Uk, Ru };
constexpr int kLangCount = 3;

std::optional<Lang> parseLang(const std::string& token);  // "en", "uk", "ru", any case
const char* langToken(Lang l);                            // in uaro-pad.ini
const char* langName(Lang l);                             // in its own language: "English", "Українська", "Русский"
Lang nextLang(Lang l, int dir);                           // the menu's ring: en, uk, ru

enum class Text : uint16_t {
    TabButtons,
    TabSettings,
    DefaultLayout,  // the menu title before the character is known
    Character,      // "character" + its id when the name is unknown
    ResetButtons,
    ResetSettings,
    Save,
    Cancel,
    Help,  // two lines
    Attack,
    Slot,
    Row,
    Pickup,
    SitStand,
    ClearTarget,
    Language,
    Lock,
    FocusClick,
    MiddleButton,
    MouseButton4,
    MouseButton5,
    Borderless,
    SaveCpu,
    Rumble,
    Mark,
    On,
    Off,
    FocusStaysOut,
    FocusGoesIn,
    MarkGasp,
    MarkSight,
    MarkRuwach,
    MarkPoison,
    MarkDeadlyPoison,
    MarkCurse,
    MarkStun,
    MarkSleep,
    MarkSilence,
    Count
};

const char* tr(Lang l, Text t);  // UTF-8

}  // namespace uapad
