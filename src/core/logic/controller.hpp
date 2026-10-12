#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/bytes.hpp"
#include "core/config/bindings.hpp"
#include "core/config/config.hpp"
#include "core/logic/camera.hpp"
#include "core/logic/pad.hpp"
#include "core/world/world.hpp"

namespace uapad {

struct ViewInfo {
    bool focused = false;
    int width = 0;  // client area, px
    int height = 0;
    bool mouseClicked = false;  // the player pressed the real left mouse button since the last tick
    bool paused = false;        // the settings menu is open: the pad belongs to it
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
    uint32_t mark = 0;           // a newly chosen monster to flag with "!" for the player; 0: none
    Rumble rumble = Rumble::None;
    std::vector<std::string> notes;  // decisions worth a log line
};

// The left stick walks, the right stick picks a monster, Back switches the pad off; every other button does
// what its Bindings say (by default the 0.2 hunter layout).
class Controller {
public:
    explicit Controller(const Config& cfg);
    TickOutput tick(const PadState& pad, const WorldState& world, const ViewInfo& view, uint32_t nowMs);
    // The client asked to walk to `dest` (0x00a7). While the pad holds the walking point steady this
    // shows which way the camera is turned.
    void onClientWalk(Cell dest, const WorldState& world, uint32_t nowMs);
    // The client asked for a skill itself (0x0072 / 0x0113, e.g. from the keyboard): the server ends our
    // attack for it, so ask for the attack again.
    void onClientSkill(uint32_t nowMs);
    // A map was loaded. The client may have set its camera back, and one walk teaches the turn again.
    void onMapLoaded() { camera_.reset(); }

    void setBindings(const Bindings& bindings);
    const Bindings& bindings() const { return bindings_; }

    bool enabled() const { return enabled_; }
    bool walking() const { return walking_; }  // the left stick holds the virtual mouse
    uint32_t target() const { return target_; }
    bool attacking() const { return wantAttack_; }
    bool approaching() const { return approach_.has_value(); }
    const CameraYaw& camera() const { return camera_; }

private:
    struct Approach {
        bool pickup = false;  // floor item pickup, otherwise a skill
        uint32_t id = 0;      // floor item id or actor id
        bool ground = false;
        uint16_t skill = 0;
        uint16_t lv = 0;
        int range = 1;
        int slot = -1;  // the hotkey slot that asked for the skill
        uint32_t startMs = 0;
        std::optional<uint32_t> lastStepMs;
    };

    uint16_t readButtons(const PadState& pad);
    void walk(const PadState& pad, const WorldState& world, const ViewInfo& view, uint32_t now, TickOutput& out);
    void aim(const PadState& pad, const WorldState& world, uint32_t now, TickOutput& out);
    void handleButtons(uint16_t held, uint16_t down, const WorldState& world, uint32_t now, TickOutput& out);
    void fire(const Action& a, bool repeat, const WorldState& world, uint32_t now, TickOutput& out);
    bool ownsApproach(const Action& a) const;
    bool attackHeld() const;
    void attack(const WorldState& world, uint32_t now, TickOutput& out);
    void useSlot(int slot, bool repeat, const WorldState& world, uint32_t now, TickOutput& out);
    void pickup(bool repeat, const WorldState& world, uint32_t now, TickOutput& out);
    void sendTake(uint32_t item, uint32_t now, TickOutput& out);
    void clearTarget(const WorldState& world, uint32_t now, TickOutput& out);
    void stopAttacking();
    void attackAgainSoon(uint32_t now);
    void stepApproach(const WorldState& world, uint32_t now, TickOutput& out);
    void maintainTarget(bool attackHeld, const WorldState& world, uint32_t now, TickOutput& out);
    uint32_t nearestTarget(const WorldState& world, uint32_t now) const;
    void dropFarTarget(const WorldState& world, uint32_t now, TickOutput& out);
    void setTarget(uint32_t id, uint32_t now, TickOutput& out);
    bool targetAlive(const WorldState& world) const;
    void deny(TickOutput& out, const std::string& why);

    Config cfg_;
    Bindings bindings_ = defaultBindings();
    bool enabled_ = true;
    uint16_t prevButtons_ = 0;
    uint16_t armed_ = 0;  // held buttons whose press we saw while active: only these repeat
    bool ltHeld_ = false;
    bool rtHeld_ = false;
    std::array<uint32_t, 16> repeatAtMs_{};  // by button bit: when a held button fires again
    bool walking_ = false;
    int ringX_ = 0;  // the walking point's offset from the centre, px
    int ringY_ = 0;
    double ringAngle_ = 0;
    std::optional<uint32_t> ringSteadyFromMs_;  // walk requests from then on tell the camera's turn
    std::optional<Cell> stickDest_;  // the client's latest walk request while the stick walks
    bool paused_ = false;  // the settings menu had the pad last tick
    bool aiming_ = false;
    bool aimLatched_ = false;  // R3 dropped the target: no aiming until the stick comes back
    uint32_t target_ = 0;
    bool wantAttack_ = false;  // we want the server to be attacking target_
    bool attackSent_ = false;  // an attack request for target_ is in effect
    std::optional<uint32_t> engagedSinceMs_;  // our first attack request on target_: the give-up clock
    std::optional<uint32_t> lastAttackMs_;
    std::array<std::optional<uint32_t>, kHotkeySlots> lastSlotMs_{};
    std::optional<uint32_t> lastPickupMs_;
    std::unordered_map<uint32_t, uint32_t> pickupTriedMs_;  // floor item id -> our first takeitem for it
    std::unordered_map<uint32_t, uint32_t> blacklist_;  // actor id -> blacklisted until (ms)
    std::unordered_map<uint32_t, uint32_t> markedMs_;   // actor id -> when we last showed its "!"
    std::optional<Approach> approach_;
    CameraYaw camera_;
};

}  // namespace uapad
