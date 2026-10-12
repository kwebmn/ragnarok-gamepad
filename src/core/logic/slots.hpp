#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.hpp"
#include "core/world/world.hpp"

namespace uapad {

struct SlotDecision {
    enum class Kind { Deny, Send, Approach };
    Kind kind = Kind::Deny;
    Bytes packet;            // Send: the request to send now
    uint32_t newTarget = 0;  // != 0: the controller should adopt this monster as its target
    uint32_t actor = 0;      // Approach: walk until within `range` of this actor, then cast
    bool ground = false;     // Approach: cast on the actor's cell instead of on the actor
    uint16_t skill = 0;
    uint16_t lv = 0;
    int range = 1;
    std::string reason;      // Deny: why, for the log
};

// What a hotkey slot does right now. `target`: the current live target or 0; `nearest`: the nearest
// candidate or 0 (used only by enemy skills when there is no target).
SlotDecision decideSlot(const Hotkey& key, const WorldState& world, uint32_t target, uint32_t nearest,
                        const std::vector<uint16_t>& groundSelf, uint32_t nowMs);

}  // namespace uapad
