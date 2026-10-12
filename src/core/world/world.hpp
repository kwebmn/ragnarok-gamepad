#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "core/proto/events.hpp"

namespace uapad {

struct Actor {
    uint32_t id = 0;
    uint16_t cls = 0;
    uint16_t hair = 0;
    uint16_t speed = 150;  // ms per cell
    Cell from;
    Cell to;
    uint32_t moveStartMs = 0;
};

struct GroundItem {
    uint32_t id = 0;
    uint16_t item = 0;
    Cell pos;
};

constexpr int kHotkeySlots = 27;

// What the client knows about the map, rebuilt from the map server's packets.
class WorldState {
public:
    void reset(uint32_t selfId);  // new map server connection
    void apply(const Event& ev, uint32_t nowMs);
    void setHotkey(int slot, const Hotkey& key);

    uint32_t selfId() const { return selfId_; }
    bool inGame() const { return inGame_; }
    Cell selfPos(uint32_t nowMs) const;
    bool sitting() const { return sitting_; }
    // Our last step on this map as a vector (destination - start), nothing before the first one.
    std::optional<Cell> lastMove() const { return lastMove_; }
    bool dead() const { return dead_; }

    const std::unordered_map<uint32_t, Actor>& actors() const { return actors_; }
    std::optional<Cell> actorPos(uint32_t id, uint32_t nowMs) const;
    bool isMonster(const Actor& a) const;
    std::optional<uint32_t> lastHitMs(uint32_t target) const;  // last time *we* hit `target`

    const std::unordered_map<uint32_t, GroundItem>& groundItems() const { return items_; }
    std::optional<InvEntry> findUsable(uint16_t itemId) const;
    std::optional<SkillInfo> skill(uint16_t id) const;
    const Hotkey& hotkey(int slot) const;
    // False until the server sends 0x02b9; uAthena only does with HOTKEY_SAVING enabled.
    bool hotkeysKnown() const { return hotkeysKnown_; }

private:
    static Cell interpolate(Cell from, Cell to, uint16_t speed, uint32_t startMs, uint32_t nowMs);
    void clearMap();

    uint32_t selfId_ = 0;
    bool inGame_ = false;
    Cell selfFrom_;
    Cell selfTo_;
    uint32_t selfMoveStartMs_ = 0;
    uint16_t selfSpeed_ = 150;  // ms per cell, from 0x00b0 SP_SPEED
    std::optional<Cell> lastMove_;
    bool sitting_ = false;
    bool dead_ = false;
    uint32_t ownPet_ = 0;
    std::unordered_map<uint32_t, Actor> actors_;
    std::unordered_map<uint32_t, uint32_t> lastHit_;
    std::unordered_map<uint32_t, GroundItem> items_;
    std::unordered_map<uint16_t, InvEntry> inventory_;  // by protocol index
    std::unordered_map<uint16_t, SkillInfo> skills_;
    std::array<Hotkey, kHotkeySlots> hotkeys_{};
    bool hotkeysKnown_ = false;
};

}  // namespace uapad
