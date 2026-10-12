#include "core/session.hpp"

#include <algorithm>
#include <cstdio>

#include "core/proto/c2s.hpp"
#include "core/proto/s2c_build.hpp"
#include "core/proto/s2c_parser.hpp"

namespace uapad {
namespace {

constexpr uint32_t kStaleMs = 500;
constexpr uint32_t kMarkStaleMs = 1000;  // a late "!" could land on the wrong moment
constexpr uint32_t kAfterStopLogMs = 1500;

std::string hotkeyText(const Hotkey& k) {
    if (k.id == 0) return "empty";
    if (k.type == 0) return "item " + std::to_string(k.id);
    return "skill " + std::to_string(k.id) + " lv " + std::to_string(k.lv);
}

std::string hex16(uint16_t v) {
    char b[8];
    std::snprintf(b, sizeof b, "0x%04x", unsigned(v));
    return b;
}

}  // namespace

Session::Session(const Config& cfg) : controller_(cfg), mark_(cfg.mark) {}

void Session::onClientSent(SocketId s, const uint8_t* data, size_t n, uint32_t nowMs) {
    if (n == 0) return;
    Conn& c = conns_[s];
    if (c.kind == Conn::Kind::Other) return;
    if (c.kind == Conn::Kind::Map) {
        feedUp(c, data, n, nowMs);
        return;
    }
    c.head.insert(c.head.end(), data, data + n);
    if (c.head.size() < 2) return;
    if (rd16(c.head.data(), 0) != 0x009b) {
        c.kind = Conn::Kind::Other;  // login or char server: never parsed, never logged
        Bytes().swap(c.head);
        return;
    }
    if (mapSocket_ != 0 && mapSocket_ != s) conns_.erase(mapSocket_);
    c.kind = Conn::Kind::Map;
    c.up = std::make_unique<StreamFramer>(Direction::ClientToServer);
    c.down = std::make_unique<StreamFramer>(Direction::ServerToClient, 4);  // raw account id first
    mapSocket_ = s;
    downOk_ = true;
    injectOk_ = true;
    queue_.clear();
    toClient_.clear();
    Bytes first;
    first.swap(c.head);
    feedUp(c, first.data(), first.size(), nowMs);
}

void Session::feedUp(Conn& c, const uint8_t* data, size_t n, uint32_t nowMs) {
    std::vector<Bytes> packets;
    if (!c.up->feed(data, n, packets) && injectOk_) {
        injectOk_ = false;
        log_.push_back("client stream out of sync at " + hex16(c.up->badId()) + "; pad requests are off");
    }
    for (const Bytes& p : packets) {
        if (auto account = parseWantToConnection(p)) {
            world_.reset(*account);
            log_.push_back("map server connection, account " + std::to_string(*account));
        } else if (auto change = parseHotkeyChange(p)) {
            world_.setHotkey(change->index, change->key);
            const int row = change->index / 9 + 1;
            const std::string slot = "F" + std::to_string(change->index % 9 + 1);
            log_.push_back("hotkey bar: " + (row == 1 ? slot : "row " + std::to_string(row) + " " + slot) + " = " +
                           hotkeyText(change->key) + (row == 1 ? "" : " (the pad uses row 1 only)"));
        } else if (auto dest = parseWalkRequest(p)) {
            if (justStoppedWalking(nowMs))
                log_.push_back("after the stick: the client asks to walk to " + cellText(*dest) + "; we are at " +
                               cellText(world_.selfPos(nowMs)));
            controller_.onClientWalk(*dest, world_, nowMs);
        } else if (p.size() >= 2 && (rd16(p.data(), 0) == 0x0072 || rd16(p.data(), 0) == 0x0113)) {
            controller_.onClientSkill(nowMs);  // useskilltoid / useskilltopos
        }
    }
}

void Session::onServerData(SocketId s, const uint8_t* data, size_t n, uint32_t nowMs) {
    if (n == 0 || s != mapSocket_ || !downOk_) return;
    auto it = conns_.find(s);
    if (it == conns_.end() || !it->second.down) return;
    std::vector<Bytes> packets;
    if (!it->second.down->feed(data, n, packets)) {
        downOk_ = false;
        log_.push_back("server stream out of sync at " + hex16(it->second.down->badId()) + "; pad requests are off");
    }
    for (const Bytes& p : packets) {
        auto ev = parseS2C(p);
        if (!ev) continue;
        if (const auto* walk = std::get_if<EvSelfWalk>(&*ev); walk && justStoppedWalking(nowMs))
            log_.push_back("after the stick: the server walks us " + cellText(walk->from) + " -> " + cellText(walk->to));
        world_.apply(*ev, nowMs);
        if (std::holds_alternative<EvAuthOk>(*ev) || std::holds_alternative<EvMapChange>(*ev)) controller_.onMapLoaded();
        if (const auto* bar = std::get_if<EvHotkeys>(&*ev)) {
            int filled = 0;
            for (const Hotkey& k : bar->keys) filled += k.id != 0;
            log_.push_back("hotkey bar from the server: " + std::to_string(filled) + " filled slot(s)");
        }
    }
}

void Session::onClosed(SocketId s) {
    conns_.erase(s);
    if (s != mapSocket_) return;
    mapSocket_ = 0;
    downOk_ = false;
    injectOk_ = false;
    queue_.clear();
    toClient_.clear();
    log_.push_back("map server connection closed");
}

void Session::onUnobserved(SocketId s, Direction dir) {
    if (s == 0 || s != mapSocket_) return;
    if (dir == Direction::ClientToServer) {
        if (injectOk_) log_.push_back("the client sent data the pad could not see; pad requests are off until reconnect");
        injectOk_ = false;
        queue_.clear();
    } else {
        if (downOk_) log_.push_back("the client received data the pad could not see; pad requests are off until reconnect");
        downOk_ = false;
    }
}

bool Session::justStoppedWalking(uint32_t nowMs) const {
    return walkStopMs_ && nowMs - *walkStopMs_ <= kAfterStopLogMs;
}

TickOutput Session::tick(const PadState& pad, const ViewInfo& view, uint32_t nowMs) {
    TickOutput out = controller_.tick(pad, world_, view, nowMs);
    if (padWalking_ && !controller_.walking()) walkStopMs_ = nowMs;
    padWalking_ = controller_.walking();
    std::vector<Bytes> requests = out.packets;
    if (out.mark != 0 && mark_ && mapSocket_ != 0 && downOk_) {
        toClient_.emplace_back(nowMs, buildEmotion(out.mark, kEmotionGasp));
        requests.push_back(buildNameRequest(out.mark));  // the server answers at once and the "!" rides along
    }
    if (requests.empty()) return out;
    if (mapSocket_ != 0 && downOk_ && injectOk_) {
        for (Bytes& p : requests) queue_.emplace_back(nowMs, std::move(p));
    } else {
        log_.push_back("dropped " + std::to_string(requests.size()) + " request(s): no healthy map server connection");
    }
    return out;
}

bool Session::canInject() const {
    if (mapSocket_ == 0 || !downOk_ || !injectOk_) return false;
    auto it = conns_.find(mapSocket_);
    return it != conns_.end() && it->second.up && it->second.up->atBoundary();
}

std::vector<Bytes> Session::takeInjectable(uint32_t nowMs) {
    std::vector<Bytes> ready;
    while (!queue_.empty() && nowMs - queue_.front().first > kStaleMs) {
        log_.push_back("dropped a request that waited too long");
        queue_.pop_front();
    }
    if (!canInject()) return ready;
    while (!queue_.empty()) {
        ready.push_back(std::move(queue_.front().second));
        queue_.pop_front();
    }
    return ready;
}

size_t Session::appendForClient(SocketId s, uint8_t* buf, size_t n, size_t cap, uint32_t nowMs) {
    while (!toClient_.empty() && nowMs - toClient_.front().first > kMarkStaleMs) toClient_.pop_front();
    if (toClient_.empty() || s == 0 || s != mapSocket_ || !downOk_ || n > cap) return 0;
    auto it = conns_.find(s);
    if (it == conns_.end() || !it->second.down || !it->second.down->atBoundary()) return 0;
    size_t added = 0;
    while (!toClient_.empty() && toClient_.front().second.size() <= cap - n - added) {
        const Bytes& p = toClient_.front().second;
        std::copy(p.begin(), p.end(), buf + n + added);
        added += p.size();
        toClient_.pop_front();
    }
    return added;
}

void Session::markInjectBroken() {
    injectOk_ = false;
    queue_.clear();
    log_.push_back("could not send a request; pad requests are off until the next map server connection");
}

std::vector<std::string> Session::takeLog() {
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

}  // namespace uapad
