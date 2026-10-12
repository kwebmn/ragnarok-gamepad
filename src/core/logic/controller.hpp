#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/bytes.hpp"
#include "core/config/config.hpp"
#include "core/logic/pad.hpp"
#include "core/world/world.hpp"

namespace uapad {

struct ViewInfo {
    bool focused = false;
    int width = 0;  // client area, px
    int height = 0;
};

struct VirtualMouse {
    bool active = false;  // false: the real mouse passes through untouched
    int x = 0;            // client-area coordinates
    int y = 0;
    bool lmb = false;
};

struct TickOutput {
    VirtualMouse mouse;
    std::vector<Bytes> packets;  // requests for the map server, in order
    Rumble rumble = Rumble::None;
    std::vector<std::string> notes;  // decisions worth a log line
};

class Controller {
public:
    explicit Controller(const Config& cfg);
    TickOutput tick(const PadState& pad, const WorldState& world, const ViewInfo& view, uint32_t nowMs);

    bool enabled() const { return enabled_; }
    bool autoAttack() const { return autoAttack_; }
    uint32_t target() const { return target_; }
    bool approaching() const { return approach_.has_value(); }

private:
    enum class Mod { None, LT, RT, LB, RB };
    struct Approach {
        bool pickup = false;  // floor item pickup, otherwise a skill
        uint32_t id = 0;      // floor item id or actor id
        bool ground = false;
        uint16_t skill = 0;
        uint16_t lv = 0;
        int range = 1;
        uint32_t startMs = 0;
        std::optional<uint32_t> lastStepMs;
    };

    void updateModifier(bool lt, bool rt, uint16_t buttons);
    void walk(const PadState& pad, const WorldState& world, const ViewInfo& view, uint32_t now, TickOutput& out);
    void handleButtons(uint16_t down, const WorldState& world, uint32_t now, TickOutput& out);
    void useSlot(int slot, const WorldState& world, uint32_t now, TickOutput& out);
    void pickup(const WorldState& world, uint32_t now, TickOutput& out);
    void stepApproach(const WorldState& world, uint32_t now, TickOutput& out);
    void maintainTarget(const WorldState& world, uint32_t now, TickOutput& out);
    void setTarget(uint32_t id, TickOutput& out);
    bool targetAlive(const WorldState& world) const;
    void deny(TickOutput& out, const std::string& why);

    Config cfg_;
    bool enabled_ = true;
    bool autoAttack_ = false;
    uint16_t prevButtons_ = 0;
    Mod mod_ = Mod::None;
    bool walking_ = false;
    uint32_t target_ = 0;
    bool wantAttack_ = false;  // we want the server to be attacking target_
    bool attackSent_ = false;  // an attack request for target_ is in effect
    uint32_t attackSinceMs_ = 0;
    std::optional<uint32_t> lastAttackMs_;
    std::optional<uint32_t> lastSlotMs_;
    std::unordered_map<uint32_t, uint32_t> blacklist_;  // actor id -> blacklisted until (ms)
    std::optional<Approach> approach_;
};

}  // namespace uapad
