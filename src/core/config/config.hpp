#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace uapad {

enum class LogLevel { Error = 0, Info = 1, Debug = 2 };
enum class PressMode { Ring, Center };              // where the virtual LMB goes down when walking starts
enum class ButtonChannel { DirectInput, Messages };  // how the virtual LMB reaches the client

struct Config {
    bool enabled = true;
    LogLevel logLevel = LogLevel::Info;
    float deadzone = 0.25f;
    float walkRadius = 0.22f;  // fraction of the shorter side of the client area
    PressMode press = PressMode::Ring;
    ButtonChannel buttons = ButtonChannel::DirectInput;
    bool mouseFreedom = true;  // non-exclusive DirectInput mouse; the game cursor follows the Windows cursor
    float giveUpSeconds = 4.0f;
    float blacklistSeconds = 10.0f;
    int attackIntervalMs = 300;
    std::vector<uint16_t> groundSelf = {12, 25, 27, 70};  // Safety Wall, Pneuma, Warp Portal, Sanctuary
    // Hunter traps: the server refuses a trap with any character (us included) within 1 cell of it.
    std::vector<uint16_t> traps = {115, 116, 117, 118, 119, 120, 121, 122, 123, 125};
    int repeatMs = 250;  // a held slot button fires again this often; 0 = once per press
    bool mark = true;    // "!" over a newly chosen target (shown to the client only)
    bool rumble = true;
};

// Parses uaro-pad.ini text. Unknown sections/keys and malformed values keep the defaults.
Config parseConfig(const std::string& text);

}  // namespace uapad
