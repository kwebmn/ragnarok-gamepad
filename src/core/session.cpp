#include "core/session.hpp"

#include <cstdio>

#include "core/proto/c2s.hpp"
#include "core/proto/s2c_parser.hpp"

namespace uapad {
namespace {

constexpr uint32_t kStaleMs = 500;

std::string hex16(uint16_t v) {
    char b[8];
    std::snprintf(b, sizeof b, "0x%04x", unsigned(v));
    return b;
}

}  // namespace

Session::Session(const Config& cfg) : controller_(cfg) {}

void Session::onClientSent(SocketId s, const uint8_t* data, size_t n, uint32_t nowMs) {
    (void)nowMs;
    if (n == 0) return;
    Conn& c = conns_[s];
    if (c.kind == Conn::Kind::Other) return;
    if (c.kind == Conn::Kind::Map) {
        feedUp(c, data, n);
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
    Bytes first;
    first.swap(c.head);
    feedUp(c, first.data(), first.size());
}

void Session::feedUp(Conn& c, const uint8_t* data, size_t n) {
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
        world_.apply(*ev, nowMs);
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

TickOutput Session::tick(const PadState& pad, const ViewInfo& view, uint32_t nowMs) {
    TickOutput out = controller_.tick(pad, world_, view, nowMs);
    if (out.packets.empty()) return out;
    if (mapSocket_ != 0 && downOk_ && injectOk_) {
        for (const Bytes& p : out.packets) queue_.emplace_back(nowMs, p);
    } else {
        log_.push_back("dropped " + std::to_string(out.packets.size()) + " request(s): no healthy map server connection");
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
