#include "core/logic/controller.hpp"

#include <algorithm>
#include <cmath>

#include "core/logic/slots.hpp"
#include "core/logic/targeting.hpp"
#include "core/proto/c2s.hpp"

namespace uapad {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kTriggerOn = 0.5f;
constexpr float kTriggerOff = 0.3f;
constexpr double kAimOn = 0.6;
constexpr double kAimOff = 0.35;
constexpr double kAimSwitchMargin = 8.0;  // aim score points a new monster must win by
constexpr uint32_t kSlotIntervalMs = 150;
constexpr uint32_t kApproachStepMs = 250;
constexpr uint32_t kApproachTimeoutMs = 5000;
constexpr uint32_t kRingSettleMs = 150;
constexpr double kRingTurn = 10.0 * kPi / 180.0;
constexpr uint32_t kMarkAgainMs = 1000;
constexpr uint32_t kReattackMs = 1000;  // standing still without a hit of ours this long: ask again
constexpr uint32_t kPickupGiveUpMs = 1000;  // still on the ground a second after we asked: not ours to take
constexpr int kPickupRadius = 10;
constexpr int kPickupReach = 1;

bool due(const std::optional<uint32_t>& last, uint32_t now, uint32_t interval) {
    return !last || now - *last >= interval;
}

size_t bitIndex(uint16_t button) {
    size_t i = 0;
    while (i < 15 && !(button & (1u << i))) ++i;
    return i;
}

}  // namespace

const Controller::Binding Controller::kLayout[] = {
    {kPadA, Action::Attack, 0},
    {kPadX, Action::Slot, 0},     // F1
    {kPadY, Action::Slot, 1},     // F2
    {kPadB, Action::Slot, 2},     // F3
    {kPadRB, Action::Slot, 3},    // F4
    {kPadRT, Action::Slot, 4},    // F5
    {kPadLB, Action::Slot, 5},    // F6
    {kPadUp, Action::Slot, 6},    // F7
    {kPadRight, Action::Slot, 7}, // F8
    {kPadDown, Action::Slot, 8},  // F9
    {kPadLT, Action::Pickup, 0},
    {kPadLeft, Action::SitStand, 0},
    {kPadR3, Action::ClearTarget, 0},
};

Controller::Controller(const Config& cfg) : cfg_(cfg), enabled_(cfg.enabled) {}

void Controller::deny(TickOutput& out, const std::string& why) {
    if (out.rumble == Rumble::None) out.rumble = Rumble::Denied;
    out.notes.push_back("denied: " + why);
}

bool Controller::targetAlive(const WorldState& world) const {
    return target_ != 0 && world.actors().count(target_) != 0;
}

uint32_t Controller::nearestTarget(const WorldState& world, uint32_t now) const {
    auto c = targetCandidates(world, now, blacklist_);
    return c.empty() ? 0 : c.front();
}

void Controller::stopAttacking() {
    wantAttack_ = false;
    attackSent_ = false;
    engagedSinceMs_.reset();
}

// Every skill but a combo ends the server's attack (uAthena unit_skilluse_id2 / _pos2): ask for it again.
void Controller::attackAgainSoon(uint32_t now) {
    if (!wantAttack_ || target_ == 0) return;
    attackSent_ = false;
    lastAttackMs_ = now;  // the request waits attack_interval_ms after the skill
}

void Controller::onClientSkill(uint32_t now) { attackAgainSoon(now); }

void Controller::setTarget(uint32_t id, uint32_t now, TickOutput& out) {
    if (id == target_) return;
    target_ = id;
    attackSent_ = false;
    engagedSinceMs_.reset();
    if (approach_ && !approach_->pickup && approach_->id != id) approach_.reset();  // a skill for the old one
    if (id == 0) return;
    if (out.rumble == Rumble::None) out.rumble = Rumble::TargetSwitch;
    out.notes.push_back("target " + std::to_string(id));
    for (auto it = markedMs_.begin(); it != markedMs_.end();) {
        if (now - it->second >= kMarkAgainMs)
            it = markedMs_.erase(it);
        else
            ++it;
    }
    if (markedMs_.count(id) == 0) {  // its "!" from a moment ago would still be showing
        markedMs_[id] = now;
        out.mark = id;
    }
}

uint16_t Controller::readButtons(const PadState& pad) {
    if (!pad.connected) {
        ltHeld_ = rtHeld_ = false;
        return 0;
    }
    ltHeld_ = pad.lt > (ltHeld_ ? kTriggerOff : kTriggerOn);
    rtHeld_ = pad.rt > (rtHeld_ ? kTriggerOff : kTriggerOn);
    uint16_t b = uint16_t(pad.buttons & ~(kPadLT | kPadRT));
    if (ltHeld_) b = uint16_t(b | kPadLT);
    if (rtHeld_) b = uint16_t(b | kPadRT);
    return b;
}

TickOutput Controller::tick(const PadState& pad, const WorldState& world, const ViewInfo& view, uint32_t now) {
    TickOutput out;
    const uint16_t buttons = readButtons(pad);
    const uint16_t down = uint16_t(buttons & ~prevButtons_);
    prevButtons_ = buttons;

    if (down & kPadBack) {
        enabled_ = !enabled_;
        out.rumble = enabled_ ? Rumble::PadOn : Rumble::PadOff;
        out.notes.push_back(enabled_ ? "pad on" : "pad off");
        if (!enabled_) stopAttacking();
    }
    if (!enabled_ || !pad.connected || !view.focused || !world.inGame() || world.dead()) {
        walking_ = false;
        ringSteadyFromMs_.reset();
        aiming_ = false;
        armed_ = 0;  // buttons pressed meanwhile do nothing until pressed again
        approach_.reset();
        return out;
    }
    if (view.mouseClicked && (wantAttack_ || approach_)) {  // the player took over with the mouse
        stopAttacking();
        approach_.reset();
        out.notes.push_back("mouse click: the pad stops its attack");
    }
    walk(pad, world, view, now, out);
    aim(pad, world, now, out);
    handleButtons(buttons, down, world, now, out);
    stepApproach(world, now, out);
    maintainTarget((armed_ & kPadA) != 0, world, now, out);
    return out;
}

void Controller::onClientWalk(Cell dest, const WorldState& world, uint32_t now) {
    if (!walking_ || !ringSteadyFromMs_ || int32_t(now - *ringSteadyFromMs_) < 0) return;
    const Cell me = world.selfPos(now);
    camera_.addSample(ringX_, ringY_, dest.x - me.x, dest.y - me.y);
}

void Controller::walk(const PadState& pad, const WorldState& world, const ViewInfo& view, uint32_t now,
                      TickOutput& out) {
    const float mag = std::sqrt(pad.lx * pad.lx + pad.ly * pad.ly);
    if (mag > cfg_.deadzone && view.width > 0 && view.height > 0) {
        const bool starting = !walking_;
        if (starting) {
            walking_ = true;
            approach_.reset();
            stopAttacking();  // walking ends a fight; a held A starts it again afterwards
            ringSteadyFromMs_.reset();
        }
        const float radius = cfg_.walkRadius * float(std::min(view.width, view.height));
        const int cx = view.width / 2;
        const int cy = view.height / 2;
        out.mouse.active = true;
        out.mouse.lmb = true;
        if (starting && cfg_.press == PressMode::Center) {
            out.mouse.x = cx;
            out.mouse.y = cy;
            return;
        }
        ringX_ = int(std::lround(pad.lx / mag * radius));
        ringY_ = -int(std::lround(pad.ly / mag * radius));
        out.mouse.x = cx + ringX_;
        out.mouse.y = cy + ringY_;
        // The client's walk requests show the camera's turn once it has acted on this point: right away
        // for the first press, a moment later after the stick turned.
        const double angle = std::atan2(double(pad.ly), double(pad.lx));
        if (!ringSteadyFromMs_ || std::fabs(std::remainder(angle - ringAngle_, 2.0 * kPi)) > kRingTurn) {
            ringSteadyFromMs_ = (starting ? now : now + kRingSettleMs);
            ringAngle_ = angle;
        }
        return;
    }
    if (walking_) {
        walking_ = false;
        ringSteadyFromMs_.reset();
        const Cell here = world.selfPos(now);
        out.packets.push_back(buildWalk(here));  // stop where we are
        out.notes.push_back("walk stop at " + cellText(here) + "; the server was taking us to " +
                            cellText(world.selfDest()));
    }
}

void Controller::aim(const PadState& pad, const WorldState& world, uint32_t now, TickOutput& out) {
    const double mag = std::hypot(double(pad.rx), double(pad.ry));
    if (aimLatched_) {
        if (mag < kAimOff) aimLatched_ = false;
        return;
    }
    if (mag < (aiming_ ? kAimOff : kAimOn)) {
        aiming_ = false;
        return;
    }
    aiming_ = true;
    double dirX = 0;
    double dirY = 0;
    camera_.toWorld(pad.rx, -pad.ry, dirX, dirY);  // the stick's +y is up, the screen's is down
    const uint32_t best = pickInDirection(world, now, dirX, dirY, blacklist_);
    if (best == 0 || best == target_) return;
    if (targetAlive(world)) {
        const auto current = aimScore(world, now, target_, dirX, dirY);
        const auto challenger = aimScore(world, now, best, dirX, dirY);
        if (current && challenger && *challenger + kAimSwitchMargin > *current) return;
    }
    setTarget(best, now, out);  // a running attack follows: setTarget asks for a new attack request
}

void Controller::handleButtons(uint16_t held, uint16_t down, const WorldState& world, uint32_t now,
                               TickOutput& out) {
    armed_ = uint16_t((armed_ & held) | down);
    for (const Binding& b : kLayout) {
        if (down & b.button) {
            approach_.reset();  // a new press overrides walking up to something
            break;
        }
    }
    for (const Binding& b : kLayout) {
        const size_t bit = bitIndex(b.button);
        if (down & b.button) {
            fire(b, false, world, now, out);
            repeatAtMs_[bit] = now + uint32_t(cfg_.repeatMs);
            continue;
        }
        const bool repeats = b.action == Action::Slot || b.action == Action::Pickup;
        if (repeats && cfg_.repeatMs > 0 && (armed_ & b.button) && !ownsApproach(b) &&
            int32_t(now - repeatAtMs_[bit]) >= 0) {
            fire(b, true, world, now, out);
            repeatAtMs_[bit] = now + uint32_t(cfg_.repeatMs);
        }
    }
}

// The button whose press is walking us up to something waits for that walk; the others keep repeating.
bool Controller::ownsApproach(const Binding& b) const {
    if (!approach_) return false;
    if (b.action == Action::Slot) return !approach_->pickup && approach_->slot == b.slot;
    return b.action == Action::Pickup && approach_->pickup;
}

void Controller::fire(const Binding& b, bool repeat, const WorldState& world, uint32_t now, TickOutput& out) {
    switch (b.action) {
        case Action::Attack:
            attack(world, now, out);
            break;
        case Action::Slot:
            useSlot(b.slot, repeat, world, now, out);
            break;
        case Action::Pickup:
            pickup(repeat, world, now, out);
            break;
        case Action::SitStand:
            out.packets.push_back(buildAction(0, world.sitting() ? kActionStand : kActionSit));
            stopAttacking();
            break;
        case Action::ClearTarget:
            clearTarget(world, now, out);
            break;
    }
}

void Controller::attack(const WorldState& world, uint32_t now, TickOutput& out) {
    if (!targetAlive(world)) {
        const uint32_t id = nearestTarget(world, now);
        if (id == 0) {
            deny(out, "no monsters around");
            return;
        }
        setTarget(id, now, out);
    }
    wantAttack_ = true;
    attackSent_ = false;
}

void Controller::useSlot(int slot, bool repeat, const WorldState& world, uint32_t now, TickOutput& out) {
    const Hotkey& key = world.hotkey(slot);
    if (key.id == 0) {
        if (!repeat)
            deny(out, "slot F" + std::to_string(slot + 1) + " is empty" +
                          (world.hotkeysKnown() ? std::string()
                                                : std::string(" (the server sent no hotkey bar: drag the skill onto "
                                                              "the bar again so the pad can see it)")));
        return;
    }
    std::optional<uint32_t>& last = lastSlotMs_[size_t(slot)];
    if (!due(last, now, kSlotIntervalMs)) return;
    last = now;
    const uint32_t target = targetAlive(world) ? target_ : 0;
    const uint32_t nearest = target != 0 ? 0 : nearestTarget(world, now);
    SlotDecision d = decideSlot(key, world, target, nearest, cfg_, now);
    if (d.kind == SlotDecision::Kind::Approach && walking_) {
        // The stick has the legs: no walking up against it. A held button casts once it is in range.
        if (!repeat) deny(out, "skill " + std::to_string(d.skill) + ": out of range while walking");
        return;
    }
    if (d.kind == SlotDecision::Kind::Approach && repeat && approach_) return;  // a press started that walk
    if (d.newTarget != 0) setTarget(d.newTarget, now, out);
    switch (d.kind) {
        case SlotDecision::Kind::Deny:
            if (!repeat) deny(out, d.reason);
            break;
        case SlotDecision::Kind::Send:
            out.packets.push_back(std::move(d.packet));
            if (!repeat)
                out.notes.push_back("F" + std::to_string(slot + 1) + ": " +
                                    (key.type == 0 ? "item " + std::to_string(key.id)
                                                   : "skill " + std::to_string(d.skill) + " lv " + std::to_string(d.lv)));
            attackAgainSoon(now);
            break;
        case SlotDecision::Kind::Approach: {
            Approach ap;
            ap.id = d.actor;
            ap.ground = d.ground;
            ap.skill = d.skill;
            ap.lv = d.lv;
            ap.range = d.range;
            ap.slot = slot;
            ap.startMs = now;
            approach_ = ap;
            out.notes.push_back("approaching for skill " + std::to_string(d.skill));
            break;
        }
    }
}

void Controller::sendTake(uint32_t item, uint32_t now, TickOutput& out) {
    out.packets.push_back(buildTakeItem(item));
    lastPickupMs_ = now;
    pickupTriedMs_.emplace(item, now);
}

void Controller::pickup(bool repeat, const WorldState& world, uint32_t now, TickOutput& out) {
    if (repeat && !due(lastPickupMs_, now, kSlotIntervalMs)) return;  // e.g. right behind a walk-up's take
    if (!repeat) pickupTriedMs_.clear();  // a new press gives every item another try
    for (auto it = pickupTriedMs_.begin(); it != pickupTriedMs_.end();) {
        if (world.groundItems().count(it->first) == 0)
            it = pickupTriedMs_.erase(it);
        else
            ++it;
    }
    const Cell me = world.selfPos(now);
    const GroundItem* best = nullptr;
    int bestDist = 0;
    for (const auto& entry : world.groundItems()) {
        const GroundItem& item = entry.second;
        const int d = cellDistance(me, item.pos);
        if (d > kPickupRadius) continue;
        auto tried = pickupTriedMs_.find(item.id);  // someone else's loot or too heavy: move on
        if (tried != pickupTriedMs_.end() && now - tried->second >= kPickupGiveUpMs) continue;
        if (!best || d < bestDist || (d == bestDist && item.id < best->id)) {
            best = &item;
            bestDist = d;
        }
    }
    if (!best) {
        if (!repeat) deny(out, "nothing to pick up");
        return;
    }
    if (bestDist <= kPickupReach) {
        sendTake(best->id, now, out);
        return;
    }
    if (walking_) {  // no walking up against the stick; a held LT picks things up as we pass them
        if (!repeat) deny(out, "item " + std::to_string(best->id) + " is out of reach while walking");
        return;
    }
    if (repeat && approach_) return;  // a press started that walk
    Approach ap;
    ap.pickup = true;
    ap.id = best->id;
    ap.range = kPickupReach;
    ap.startMs = now;
    approach_ = ap;
    out.notes.push_back("walking to item " + std::to_string(best->id));
}

void Controller::clearTarget(const WorldState& world, uint32_t now, TickOutput& out) {
    aiming_ = false;
    aimLatched_ = true;  // clicking the stick may push it too: no new pick until it comes back
    if (target_ == 0 && !wantAttack_) return;
    out.packets.push_back(buildStopAttack());
    // 0x0118 does not stop a walk up to the target (ours, or the client's after "too far"): stop it too.
    if (!walking_ && world.selfMoving(now)) out.packets.push_back(buildWalk(world.selfPos(now)));
    target_ = 0;
    stopAttacking();
    approach_.reset();
    out.rumble = Rumble::TargetCleared;
    out.notes.push_back("target cleared");
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
            sendTake(ap.id, now, out);
        else if (ap.ground)
            out.packets.push_back(buildSkillToPos(ap.skill, ap.lv, *goal));
        else
            out.packets.push_back(buildSkillToId(ap.skill, ap.lv, ap.id));
        if (ap.slot >= 0) lastSlotMs_[size_t(ap.slot)] = now;  // a held button waits its turn again
        const bool skill = !ap.pickup;
        approach_.reset();
        if (skill) attackAgainSoon(now);
        return;
    }
    if (due(ap.lastStepMs, now, kApproachStepMs)) {
        out.packets.push_back(buildWalk(*goal));
        ap.lastStepMs = now;
    }
}

void Controller::maintainTarget(bool attackHeld, const WorldState& world, uint32_t now, TickOutput& out) {
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
        engagedSinceMs_.reset();
    }
    if (target_ != 0 && engagedSinceMs_ &&
        shouldGiveUp(world, target_, *engagedSinceMs_, now, uint32_t(cfg_.giveUpSeconds * 1000.0f))) {
        blacklist_[target_] = now + uint32_t(cfg_.blacklistSeconds * 1000.0f);
        out.notes.push_back("giving up on " + std::to_string(target_));
        target_ = 0;
        attackSent_ = false;
        engagedSinceMs_.reset();
    }
    // A press fights the target until it is gone; holding A goes on to the next one.
    if (attackHeld)
        wantAttack_ = true;
    else if (target_ == 0)
        stopAttacking();
    const bool idle = !walking_ && !approach_ && !world.sitting();
    if (wantAttack_ && target_ == 0 && idle) {
        if (const uint32_t id = nearestTarget(world, now)) setTarget(id, now, out);
    }
    // The server drops our attack for many reasons (a skill, a cast, a click elsewhere). Standing still with
    // no hit of ours for a while means it is not attacking: ask again. Not while moving: every attack
    // request stops the walk first (clif_parse_ActionRequest_sub).
    if (target_ != 0 && wantAttack_ && attackSent_ && idle && !world.selfMoving(now)) {
        uint32_t ref = lastAttackMs_ ? *lastAttackMs_ : now;
        if (auto hit = world.lastHitMs(target_); hit && int32_t(*hit - ref) > 0) ref = *hit;
        if (now - ref >= kReattackMs) attackSent_ = false;
    }
    if (target_ != 0 && wantAttack_ && !attackSent_ && idle &&
        due(lastAttackMs_, now, uint32_t(cfg_.attackIntervalMs))) {
        out.packets.push_back(buildAction(target_, kActionAttack));
        lastAttackMs_ = now;
        attackSent_ = true;
        if (!engagedSinceMs_) engagedSinceMs_ = now;
    }
}

}  // namespace uapad
