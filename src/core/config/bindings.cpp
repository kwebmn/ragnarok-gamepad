#include "core/config/bindings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>
#include <vector>

namespace uapad {
namespace {

constexpr int kSlots = 27;
constexpr int kRing = 2 + kSlots + 3;  // none, attack, 27 slots, pickup, sit, untarget

const char* const kTokens[kBindableCount] = {"A",  "B",  "X",  "Y",     "LB",   "RB",   "LT",
                                             "RT", "UP", "RIGHT", "DOWN", "LEFT", "L3", "R3"};
const char* const kLabels[kBindableCount] = {"A",  "B",       "X",       "Y",       "LB",      "RB", "LT",
                                             "RT", "D-pad ↑", "D-pad →", "D-pad ↓", "D-pad ←", "L3", "R3"};

int ringIndex(Action a) {
    switch (a.kind) {
        case Action::Kind::None: return 0;
        case Action::Kind::Attack: return 1;
        case Action::Kind::Slot: return 2 + std::min<int>(a.slot, kSlots - 1);
        case Action::Kind::Pickup: return 2 + kSlots;
        case Action::Kind::SitStand: return 3 + kSlots;
        case Action::Kind::ClearTarget: return 4 + kSlots;
    }
    return 0;
}

Action fromRing(int i) {
    if (i == 0) return Action{};
    if (i == 1) return Action{Action::Kind::Attack, 0};
    if (i < 2 + kSlots) return Action{Action::Kind::Slot, uint8_t(i - 2)};
    if (i == 2 + kSlots) return Action{Action::Kind::Pickup, 0};
    if (i == 3 + kSlots) return Action{Action::Kind::SitStand, 0};
    return Action{Action::Kind::ClearTarget, 0};
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::optional<size_t> buttonIndex(const std::string& token) {
    for (size_t i = 0; i < kBindableCount; ++i)
        if (lower(token) == lower(kTokens[i])) return i;
    return std::nullopt;
}

struct Section {
    enum class Kind { Default, Character, Other };
    Kind kind = Kind::Other;
    uint32_t charId = 0;
    std::string name;
    std::vector<std::pair<size_t, Action>> keys;
};

Section sectionFor(const std::string& header) {
    Section s;
    const std::string h = lower(trim(header));
    if (h == "default") {
        s.kind = Section::Kind::Default;
    } else if (h.rfind("char ", 0) == 0) {
        const std::string digits = trim(h.substr(5));
        char* end = nullptr;
        const unsigned long id = std::strtoul(digits.c_str(), &end, 10);
        if (!digits.empty() && end && *end == '\0' && id > 0 && id <= 0xFFFFFFFFul) {
            s.kind = Section::Kind::Character;
            s.charId = uint32_t(id);
        }
    }
    return s;
}

void writeSection(std::ostringstream& out, const Bindings& b) {
    for (size_t i = 0; i < kBindableCount; ++i) out << kTokens[i] << " = " << actionToken(b[i]) << "\n";
}

}  // namespace

Bindings defaultBindings() {
    Bindings b{};
    auto set = [&](uint16_t button, Action a) {
        for (size_t i = 0; i < kBindableCount; ++i)
            if (kBindableButtons[i] == button) b[i] = a;
    };
    set(kPadA, Action{Action::Kind::Attack, 0});
    set(kPadX, Action{Action::Kind::Slot, 0});
    set(kPadY, Action{Action::Kind::Slot, 1});
    set(kPadB, Action{Action::Kind::Slot, 2});
    set(kPadRB, Action{Action::Kind::Slot, 3});
    set(kPadRT, Action{Action::Kind::Slot, 4});
    set(kPadLB, Action{Action::Kind::Slot, 5});
    set(kPadUp, Action{Action::Kind::Slot, 6});
    set(kPadRight, Action{Action::Kind::Slot, 7});
    set(kPadDown, Action{Action::Kind::Slot, 8});
    set(kPadLT, Action{Action::Kind::Pickup, 0});
    set(kPadLeft, Action{Action::Kind::SitStand, 0});
    set(kPadR3, Action{Action::Kind::ClearTarget, 0});
    return b;
}

Action nextAction(Action a, int dir) {
    const int i = ringIndex(a) + (dir > 0 ? 1 : -1);
    return fromRing((i % kRing + kRing) % kRing);
}

const char* buttonToken(size_t i) { return i < kBindableCount ? kTokens[i] : ""; }
const char* buttonLabel(size_t i) { return i < kBindableCount ? kLabels[i] : ""; }

std::string actionToken(Action a) {
    switch (a.kind) {
        case Action::Kind::None: return "none";
        case Action::Kind::Attack: return "attack";
        case Action::Kind::Slot: return "slot " + std::to_string(int(a.slot) + 1);
        case Action::Kind::Pickup: return "pickup";
        case Action::Kind::SitStand: return "sit";
        case Action::Kind::ClearTarget: return "untarget";
    }
    return "none";
}

std::optional<Action> parseAction(const std::string& token) {
    const std::string t = lower(trim(token));
    if (t == "none") return Action{};
    if (t == "attack") return Action{Action::Kind::Attack, 0};
    if (t == "pickup") return Action{Action::Kind::Pickup, 0};
    if (t == "sit") return Action{Action::Kind::SitStand, 0};
    if (t == "untarget") return Action{Action::Kind::ClearTarget, 0};
    if (t.rfind("slot ", 0) == 0) {
        const std::string digits = trim(t.substr(5));
        char* end = nullptr;
        const long n = std::strtol(digits.c_str(), &end, 10);
        if (!digits.empty() && end && *end == '\0' && n >= 1 && n <= kSlots) return Action{Action::Kind::Slot, uint8_t(n - 1)};
    }
    return std::nullopt;
}

std::string actionLabel(Action a, Lang lang) {
    switch (a.kind) {
        case Action::Kind::None: return "—";
        case Action::Kind::Attack: return tr(lang, Text::Attack);
        case Action::Kind::Slot: {
            const int row = a.slot / 9 + 1;
            std::string label = std::string(tr(lang, Text::Slot)) + " F" + std::to_string(a.slot % 9 + 1);
            if (row > 1) label += ", " + std::string(tr(lang, Text::Row)) + " " + std::to_string(row);
            return label;
        }
        case Action::Kind::Pickup: return tr(lang, Text::Pickup);
        case Action::Kind::SitStand: return tr(lang, Text::SitStand);
        case Action::Kind::ClearTarget: return tr(lang, Text::ClearTarget);
    }
    return "—";
}

Bindings Profiles::forCharacter(uint32_t charId) const {
    auto it = characters.find(charId);
    return it == characters.end() ? fallback : it->second.bindings;
}

Profiles parseProfiles(const std::string& text) {
    std::vector<Section> sections;
    std::istringstream in(text);
    std::string line;
    Section* current = nullptr;
    while (std::getline(in, line)) {
        if (sections.empty() && line.rfind("\xEF\xBB\xBF", 0) == 0) line = line.substr(3);
        const size_t comment = line.find(';');
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            sections.push_back(sectionFor(line.substr(1, line.size() - 2)));
            current = &sections.back();
            continue;
        }
        const size_t eq = line.find('=');
        if (!current || eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (lower(key) == "name") {
            current->name = value;
        } else if (auto button = buttonIndex(key)) {
            if (auto action = parseAction(value)) current->keys.emplace_back(*button, *action);
        }
    }
    Profiles p;
    for (const Section& s : sections)
        if (s.kind == Section::Kind::Default)
            for (const auto& [button, action] : s.keys) p.fallback[button] = action;
    for (const Section& s : sections) {
        if (s.kind != Section::Kind::Character) continue;
        Profiles::Character& c = p.characters[s.charId];
        c.name = s.name;
        c.bindings = p.fallback;
        for (const auto& [button, action] : s.keys) c.bindings[button] = action;
    }
    return p;
}

Profiles withLayout(Profiles onDisk, uint32_t charId, const std::string& name, const Bindings& bindings) {
    if (charId == 0) {
        onDisk.fallback = bindings;
        return onDisk;
    }
    Profiles::Character& c = onDisk.characters[charId];
    if (!name.empty()) c.name = name;
    c.bindings = bindings;
    return onDisk;
}

std::string formatProfiles(const Profiles& profiles) {
    std::ostringstream out;
    // ASCII only: the character names are in the system code page, and one file should have one encoding.
    out << "; uaRO Pad: button layouts per character. The pad writes this file from its menu (Start in the game).\n";
    out << "; Edit it by hand only while the client is closed. Actions: none, attack, slot 1-27, pickup, sit, untarget.\n\n";
    out << "[default]\n";
    writeSection(out, profiles.fallback);
    for (const auto& [id, c] : profiles.characters) {
        out << "\n[char " << id << "]\n";
        if (!c.name.empty()) out << "name = " << c.name << "\n";
        writeSection(out, c.bindings);
    }
    return out.str();
}

}  // namespace uapad
