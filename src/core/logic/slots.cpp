#include "core/logic/slots.hpp"

#include <algorithm>

#include "core/proto/c2s.hpp"

namespace uapad {
namespace {

constexpr uint32_t kInfEnemy = 1;
constexpr uint32_t kInfGround = 2;
constexpr uint32_t kInfSelf = 4;
constexpr uint32_t kInfSupport = 16;

SlotDecision deny(std::string why) {
    SlotDecision d;
    d.reason = std::move(why);
    return d;
}

SlotDecision send(Bytes packet) {
    SlotDecision d;
    d.kind = SlotDecision::Kind::Send;
    d.packet = std::move(packet);
    return d;
}

}  // namespace

SlotDecision decideSlot(const Hotkey& key, const WorldState& world, uint32_t target, uint32_t nearest,
                        const std::vector<uint16_t>& groundSelf, uint32_t nowMs) {
    if (key.id == 0) return deny("empty slot");
    if (key.type == 0) {
        auto item = world.findUsable(uint16_t(key.id));
        if (!item) return deny("item " + std::to_string(key.id) + " is not in the inventory");
        return send(buildUseItem(item->index, world.selfId()));
    }
    auto sk = world.skill(uint16_t(key.id));
    if (!sk || sk->lv == 0) return deny("skill " + std::to_string(key.id) + " is not learned");
    const uint16_t id = sk->id;
    const uint16_t lv = key.lv == 0 ? sk->lv : std::min(key.lv, sk->lv);
    const Cell me = world.selfPos(nowMs);
    const int reach = std::max(1, int(sk->range));

    auto castOn = [&](uint32_t actor, bool ground, uint32_t newTarget) {
        auto pos = world.actorPos(actor, nowMs);
        if (!pos) return deny("target is gone");
        SlotDecision d;
        d.newTarget = newTarget;
        if (cellDistance(me, *pos) <= reach) {
            d.kind = SlotDecision::Kind::Send;
            d.packet = ground ? buildSkillToPos(id, lv, *pos) : buildSkillToId(id, lv, actor);
        } else {
            d.kind = SlotDecision::Kind::Approach;
            d.actor = actor;
            d.ground = ground;
            d.skill = id;
            d.lv = lv;
            d.range = reach;
        }
        return d;
    };

    if (sk->inf & kInfGround) {
        const bool underSelf = std::find(groundSelf.begin(), groundSelf.end(), id) != groundSelf.end();
        if (underSelf || target == 0) return send(buildSkillToPos(id, lv, me));
        return castOn(target, true, 0);
    }
    if (sk->inf & kInfEnemy) {
        if (target != 0) return castOn(target, false, 0);
        if (nearest != 0) return castOn(nearest, false, nearest);
        return deny("no monster for skill " + std::to_string(id));
    }
    if (sk->inf & (kInfSelf | kInfSupport)) return send(buildSkillToId(id, lv, world.selfId()));
    return deny("skill " + std::to_string(id) + " is passive");
}

}  // namespace uapad
