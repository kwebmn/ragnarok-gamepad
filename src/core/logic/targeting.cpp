#include "core/logic/targeting.hpp"

#include <algorithm>
#include <utility>

namespace uapad {

std::vector<uint32_t> targetCandidates(const WorldState& world, uint32_t nowMs,
                                       const std::unordered_map<uint32_t, uint32_t>& blacklistUntil) {
    const Cell me = world.selfPos(nowMs);
    std::vector<std::pair<int, uint32_t>> scored;
    for (const auto& entry : world.actors()) {
        const uint32_t id = entry.first;
        if (!world.isMonster(entry.second)) continue;
        auto bl = blacklistUntil.find(id);
        if (bl != blacklistUntil.end() && int32_t(nowMs - bl->second) < 0) continue;
        if (auto pos = world.actorPos(id, nowMs)) scored.emplace_back(cellDistance(me, *pos), id);
    }
    std::sort(scored.begin(), scored.end());
    std::vector<uint32_t> ids;
    ids.reserve(scored.size());
    for (const auto& s : scored) ids.push_back(s.second);
    return ids;
}

uint32_t cycleTarget(const std::vector<uint32_t>& candidates, uint32_t current, int dir) {
    if (candidates.empty()) return 0;
    auto it = std::find(candidates.begin(), candidates.end(), current);
    if (it == candidates.end()) return candidates.front();
    const long n = long(candidates.size());
    long i = long(it - candidates.begin()) + (dir > 0 ? 1 : -1);
    i = ((i % n) + n) % n;
    return candidates[size_t(i)];
}

bool shouldGiveUp(const WorldState& world, uint32_t target, uint32_t attackSinceMs, uint32_t nowMs, uint32_t giveUpMs) {
    uint32_t ref = attackSinceMs;
    if (auto hit = world.lastHitMs(target); hit && int32_t(*hit - ref) > 0) ref = *hit;
    return nowMs - ref >= giveUpMs;
}

}  // namespace uapad
