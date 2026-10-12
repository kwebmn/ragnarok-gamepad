#include "core/logic/controller.hpp"

#include <algorithm>
#include <cmath>

#include "core/logic/slots.hpp"
#include "core/logic/targeting.hpp"
#include "core/proto/c2s.hpp"

namespace uapad {
namespace {

constexpr float kTriggerHeld = 0.5f;
constexpr uint32_t kSlotIntervalMs = 150;
constexpr uint32_t kApproachStepMs = 250;
constexpr uint32_t kApproachTimeoutMs = 5000;
constexpr int kPickupRadius = 10;
constexpr int kPickupReach = 1;
constexpr uint16_t kFaceButtons[4] = {kPadA, kPadB, kPadX, kPadY};
constexpr uint16_t kActionButtons = kPadA | kPadB | kPadX | kPadY | kPadLeft | kPadRight;
constexpr int kSlotBase[] = {0, 0, 4, 9, 13};  // by Mod: None, LT, RT, LB, RB

bool due(const std::optional<uint32_t>& last, uint32_t now, uint32_t interval) {
    return !last || now - *last >= interval;
}

}  // namespace

Controller::Controller(const Config& cfg) : cfg_(cfg), enabled_(cfg.enabled) {}

void Controller::deny(TickOutput& out, const std::string& why) {
    if (out.rumble == Rumble::None) out.rumble = Rumble::Denied;
    out.notes.push_back("denied: " + why);
}

bool Controller::targetAlive(const WorldState& world) const {
    return target_ != 0 && world.actors().count(target_) != 0;
}

void Controller::setTarget(uint32_t id, TickOutput& out) {
    if (id == target_) return;
    target_ = id;
    attackSent_ = false;
    if (id == 0) return;
    if (out.rumble == Rumble::None) out.rumble = Rumble::TargetSwitch;
    out.notes.push_back("target " + std::to_string(id));
}

void Controller::updateModifier(bool lt, bool rt, uint16_t buttons) {
    auto held = [&](Mod m) {
        switch (m) {
            case Mod::LT: return lt;
            case Mod::RT: return rt;
            case Mod::LB: return (buttons & kPadLB) != 0;
            case Mod::RB: return (buttons & kPadRB) != 0;
            case Mod::None: break;
        }
        return false;
    };
    if (mod_ != Mod::None && held(mod_)) return;  // the first pressed modifier keeps priority
    mod_ = Mod::None;
    for (Mod m : {Mod::LT, Mod::RT, Mod::LB, Mod::RB}) {
        if (held(m)) {
            mod_ = m;
            break;
        }
    }
}

TickOutput Controller::tick(const PadState& pad, const WorldState& world, const ViewInfo& view, uint32_t now) {
    TickOutput out;
    const uint16_t buttons = pad.connected ? pad.buttons : 0;
    const uint16_t down = uint16_t(buttons & ~prevButtons_);
    prevButtons_ = buttons;
    updateModifier(pad.connected && pad.lt > kTriggerHeld, pad.connected && pad.rt > kTriggerHeld, buttons);

    if (down & kPadBack) {
        enabled_ = !enabled_;
        out.rumble = enabled_ ? Rumble::AutoOn : Rumble::AutoOff;
        out.notes.push_back(enabled_ ? "pad on" : "pad off");
        if (!enabled_) {
            autoAttack_ = false;
            wantAttack_ = false;
        }
    }
    if (!enabled_ || !pad.connected || !view.focused || !world.inGame() || world.dead()) {
        walking_ = false;
        approach_.reset();
        return out;
    }
    walk(pad, world, view, now, out);
    handleButtons(down, world, now, out);
    stepApproach(world, now, out);
    maintainTarget(world, now, out);
    return out;
}

void Controller::walk(const PadState& pad, const WorldState& world, const ViewInfo& view, uint32_t now,
                      TickOutput& out) {
    const float mag = std::sqrt(pad.lx * pad.lx + pad.ly * pad.ly);
    if (mag > cfg_.deadzone && view.width > 0 && view.height > 0) {
        const bool starting = !walking_;
        if (starting) {
            walking_ = true;
            approach_.reset();
            attackSent_ = false;
            if (!autoAttack_) wantAttack_ = false;
        }
        const float radius = cfg_.walkRadius * float(std::min(view.width, view.height));
        const int cx = view.width / 2;
        const int cy = view.height / 2;
        out.mouse.active = true;
        out.mouse.lmb = true;
        if (starting && cfg_.press == PressMode::Center) {
            out.mouse.x = cx;
            out.mouse.y = cy;
        } else {
            out.mouse.x = cx + int(std::lround(pad.lx / mag * radius));
            out.mouse.y = cy - int(std::lround(pad.ly / mag * radius));
        }
        return;
    }
    if (walking_) {
        walking_ = false;
        out.packets.push_back(buildWalk(world.selfPos(now)));  // stop where we are
        out.notes.push_back("walk stop");
    }
}

void Controller::handleButtons(uint16_t down, const WorldState& world, uint32_t now, TickOutput& out) {
    if (down & kActionButtons) approach_.reset();
    if (mod_ != Mod::None) {
        for (int i = 0; i < 4; ++i)
            if (down & kFaceButtons[i]) useSlot(kSlotBase[int(mod_)] + i, world, now, out);
    } else {
        if (down & kPadA) {
            if (!targetAlive(world)) {
                auto c = targetCandidates(world, now, blacklist_);
                if (c.empty())
                    deny(out, "no monsters around");
                else
                    setTarget(c.front(), out);
            }
            if (targetAlive(world)) {
                wantAttack_ = true;
                attackSent_ = false;
            }
        }
        if (down & kPadB) pickup(world, now, out);
        if (down & kPadX) {
            autoAttack_ = !autoAttack_;
            wantAttack_ = autoAttack_;
            out.rumble = autoAttack_ ? Rumble::AutoOn : Rumble::AutoOff;
            out.notes.push_back(autoAttack_ ? "auto-attack on" : "auto-attack off");
        }
        if (down & kPadY) {
            out.packets.push_back(buildAction(0, world.sitting() ? kActionStand : kActionSit));
            attackSent_ = false;
        }
    }
    if (down & (kPadLeft | kPadRight)) {
        const uint32_t next = cycleTarget(targetCandidates(world, now, blacklist_), target_, (down & kPadRight) ? 1 : -1);
        if (next == 0) {
            deny(out, "no monsters around");
        } else {
            setTarget(next, out);
            wantAttack_ = true;
            attackSent_ = false;
        }
    }
}

void Controller::useSlot(int slot, const WorldState& world, uint32_t now, TickOutput& out) {
    const Hotkey& key = world.hotkey(slot);
    if (key.id == 0) {
        deny(out, "slot " + std::to_string(slot) + " is empty" +
                      (world.hotkeysKnown() ? std::string()
                                            : std::string(" (the server sent no hotkey bar: drag the skill onto the bar "
                                                          "again so the pad can see it)")));
        return;
    }
    if (!due(lastSlotMs_, now, kSlotIntervalMs)) return;
    lastSlotMs_ = now;
    const uint32_t target = targetAlive(world) ? target_ : 0;
    uint32_t nearest = 0;
    if (target == 0) {
        auto c = targetCandidates(world, now, blacklist_);
        if (!c.empty()) nearest = c.front();
    }
    SlotDecision d = decideSlot(key, world, target, nearest, cfg_, now);
    if (d.newTarget != 0) setTarget(d.newTarget, out);
    switch (d.kind) {
        case SlotDecision::Kind::Deny:
            deny(out, d.reason);
            break;
        case SlotDecision::Kind::Send:
            out.packets.push_back(std::move(d.packet));
            break;
        case SlotDecision::Kind::Approach: {
            Approach ap;
            ap.id = d.actor;
            ap.ground = d.ground;
            ap.skill = d.skill;
            ap.lv = d.lv;
            ap.range = d.range;
            ap.startMs = now;
            approach_ = ap;
            out.notes.push_back("approaching for skill " + std::to_string(d.skill));
            break;
        }
    }
}

void Controller::pickup(const WorldState& world, uint32_t now, TickOutput& out) {
    const Cell me = world.selfPos(now);
    const GroundItem* best = nullptr;
    int bestDist = 0;
    for (const auto& entry : world.groundItems()) {
        const GroundItem& item = entry.second;
        const int d = cellDistance(me, item.pos);
        if (d > kPickupRadius) continue;
        if (!best || d < bestDist || (d == bestDist && item.id < best->id)) {
            best = &item;
            bestDist = d;
        }
    }
    if (!best) {
        deny(out, "nothing to pick up");
        return;
    }
    if (bestDist <= kPickupReach) {
        out.packets.push_back(buildTakeItem(best->id));
        return;
    }
    Approach ap;
    ap.pickup = true;
    ap.id = best->id;
    ap.range = kPickupReach;
    ap.startMs = now;
    approach_ = ap;
    out.notes.push_back("walking to item " + std::to_string(best->id));
}

void Controller::stepApproach(const WorldState& world, uint32_t now, TickOutput& out) {
    if (!approach_) return;
    Approach& ap = *approach_;
    if (now - ap.startMs >= kApproachTimeoutMs) {
        out.notes.push_back("approach timed out");
        approach_.reset();
        return;
    }
    std::optional<Cell> goal;
    if (ap.pickup) {
        auto it = world.groundItems().find(ap.id);
        if (it != world.groundItems().end()) goal = it->second.pos;
    } else {
        goal = world.actorPos(ap.id, now);
    }
    if (!goal) {
        out.notes.push_back("approach target is gone");
        approach_.reset();
        return;
    }
    if (cellDistance(world.selfPos(now), *goal) <= ap.range) {
        if (ap.pickup)
            out.packets.push_back(buildTakeItem(ap.id));
        else if (ap.ground)
            out.packets.push_back(buildSkillToPos(ap.skill, ap.lv, *goal));
        else
            out.packets.push_back(buildSkillToId(ap.skill, ap.lv, ap.id));
        approach_.reset();
        return;
    }
    if (due(ap.lastStepMs, now, kApproachStepMs)) {
        out.packets.push_back(buildWalk(*goal));
        ap.lastStepMs = now;
    }
}

void Controller::maintainTarget(const WorldState& world, uint32_t now, TickOutput& out) {
    for (auto it = blacklist_.begin(); it != blacklist_.end();) {
        if (int32_t(now - it->second) >= 0)
            it = blacklist_.erase(it);
        else
            ++it;
    }
    if (target_ != 0 && !targetAlive(world)) {
        out.notes.push_back("target " + std::to_string(target_) + " is gone");
        target_ = 0;
        attackSent_ = false;
        if (!autoAttack_) wantAttack_ = false;
    }
    if (target_ != 0 && attackSent_ &&
        shouldGiveUp(world, target_, attackSinceMs_, now, uint32_t(cfg_.giveUpSeconds * 1000.0f))) {
        blacklist_[target_] = now + uint32_t(cfg_.blacklistSeconds * 1000.0f);
        out.notes.push_back("giving up on " + std::to_string(target_));
        target_ = 0;
        attackSent_ = false;
        if (!autoAttack_) wantAttack_ = false;
    }
    const bool idle = !walking_ && !approach_ && !world.sitting();
    if (autoAttack_ && target_ == 0 && idle) {
        auto c = targetCandidates(world, now, blacklist_);
        if (!c.empty()) {
            setTarget(c.front(), out);
            wantAttack_ = true;
        }
    }
    if (target_ != 0 && wantAttack_ && !attackSent_ && idle &&
        due(lastAttackMs_, now, uint32_t(cfg_.attackIntervalMs))) {
        out.packets.push_back(buildAction(target_, kActionAttack));
        lastAttackMs_ = now;
        attackSent_ = true;
        attackSinceMs_ = now;
    }
}

}  // namespace uapad
