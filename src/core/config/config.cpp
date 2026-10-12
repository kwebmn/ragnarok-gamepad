#include "core/config/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace uapad {
namespace {

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void setFloat(const std::string& v, float lo, float hi, float& out) {
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    if (end != v.c_str() && *end == '\0' && f >= lo && f <= hi) out = f;
}

bool toInt(const std::string& v, int lo, int hi, int& out) {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0' || n < lo || n > hi) return false;
    out = int(n);
    return true;
}

void setBool(const std::string& v, bool& out) {
    if (v == "1" || v == "true" || v == "on") out = true;
    if (v == "0" || v == "false" || v == "off") out = false;
}

// "X,Y" with optional spaces; monitors left of or above the main one have negative coordinates.
bool parsePoint(const std::string& v, int& x, int& y) {
    const size_t comma = v.find(',');
    if (comma == std::string::npos) return false;
    return toInt(trim(v.substr(0, comma)), -32768, 32767, x) && toInt(trim(v.substr(comma + 1)), -32768, 32767, y);
}

void setIdList(const std::string& v, std::vector<uint16_t>& out) {
    std::vector<uint16_t> ids;
    std::istringstream list(v);
    std::string item;
    while (std::getline(list, item, ',')) {
        int id = 0;
        if (!toInt(trim(item), 1, 65535, id)) return;
        ids.push_back(uint16_t(id));
    }
    out = ids;
}

}  // namespace

std::optional<int> parseFKey(const std::string& token) {
    std::string t = trim(token);
    for (char& ch : t) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (t == "none") return 0;
    if (t.size() == 2 && t[0] == 'f' && t[1] >= '1' && t[1] <= '9') return t[1] - '0';
    return std::nullopt;
}

std::string fKeyToken(int key) { return key >= 1 && key <= 9 ? "F" + std::to_string(key) : "none"; }

Config parseConfig(const std::string& text) {
    Config c;
    std::istringstream in(text);
    std::string line;
    std::string section;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) line = line.substr(3);  // saved by Notepad with a BOM
        first = false;
        const size_t comment = line.find(';');
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = section + "." + trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "general.enabled") {
            setBool(val, c.enabled);
        } else if (key == "general.log_level") {
            if (val == "error") c.logLevel = LogLevel::Error;
            if (val == "info") c.logLevel = LogLevel::Info;
            if (val == "debug") c.logLevel = LogLevel::Debug;
        } else if (key == "stick.deadzone") {
            setFloat(val, 0.05f, 0.9f, c.deadzone);
        } else if (key == "stick.walk_radius") {
            setFloat(val, 0.05f, 0.45f, c.walkRadius);
        } else if (key == "stick.press") {
            if (val == "ring") c.press = PressMode::Ring;
            if (val == "center") c.press = PressMode::Center;
        } else if (key == "mouse.buttons") {
            if (val == "dinput") c.buttons = ButtonChannel::DirectInput;
            if (val == "messages") c.buttons = ButtonChannel::Messages;
        } else if (key == "mouse.freedom") {
            setBool(val, c.mouseFreedom);
        } else if (key == "target.give_up_seconds") {
            setFloat(val, 1.0f, 60.0f, c.giveUpSeconds);
        } else if (key == "target.blacklist_seconds") {
            setFloat(val, 0.0f, 600.0f, c.blacklistSeconds);
        } else if (key == "target.attack_interval_ms") {
            toInt(val, 100, 5000, c.attackIntervalMs);
        } else if (key == "skills.ground_self") {
            setIdList(val, c.groundSelf);
        } else if (key == "skills.traps") {
            setIdList(val, c.traps);
        } else if (key == "skills.repeat_ms") {
            int ms = 0;
            if (toInt(val, 0, 2000, ms)) c.repeatMs = ms == 0 ? 0 : std::max(ms, 150);
        } else if (key == "aim.mark") {
            setBool(val, c.mark);
        } else if (key == "rumble.enabled") {
            setBool(val, c.rumble);
        } else if (key == "general.language") {
            if (auto l = parseLang(val)) c.lang = *l;
        } else if (key == "mouse.lock") {
            setBool(val, c.mouseLock);
        } else if (key == "mouse.focus_click") {
            setBool(val, c.focusClick);
        } else if (key == "mouse.middle" || key == "mouse.button4" || key == "mouse.button5") {
            const size_t i = key == "mouse.middle" ? 0 : key == "mouse.button4" ? 1 : 2;
            if (auto k = parseFKey(val)) c.mouseKeys[i] = *k;
        } else if (key == "window.borderless") {
            setBool(val, c.borderless);
        } else if (key == "window.position") {
            int x = 0;
            int y = 0;
            if (val == "center") {
                c.position = WindowPlacement{};
            } else if (val == "keep") {
                c.position = WindowPlacement{WindowPlacement::Kind::Keep, 0, 0};
            } else if (parsePoint(val, x, y)) {
                c.position = WindowPlacement{WindowPlacement::Kind::At, x, y};
            }
        } else if (key == "window.background_sleep_ms") {
            toInt(val, 0, 100, c.backgroundSleepMs);
        }
    }
    return c;
}

}  // namespace uapad
