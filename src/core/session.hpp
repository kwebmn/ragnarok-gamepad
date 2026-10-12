#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/bytes.hpp"
#include "core/config/config.hpp"
#include "core/logic/controller.hpp"
#include "core/proto/framer.hpp"
#include "core/world/world.hpp"

namespace uapad {

// Protocol state of the client's sockets plus the controller. Called from the game thread only.
class Session {
public:
    using SocketId = uintptr_t;

    explicit Session(const Config& cfg);

    void onClientSent(SocketId s, const uint8_t* data, size_t n, uint32_t nowMs);  // bytes really sent
    void onServerData(SocketId s, const uint8_t* data, size_t n, uint32_t nowMs);  // bytes really received
    void onClosed(SocketId s);
    // Traffic on `s` that the hooks could not see (overlapped WSASend, a call from another thread).
    // On the map socket it ends trust in that direction until the next connection.
    void onUnobserved(SocketId s, Direction dir);

    // Runs the controller. Its requests are queued for injection (and still listed in the output).
    TickOutput tick(const PadState& pad, const ViewInfo& view, uint32_t nowMs);
    // Queued requests that may go out now; requests that waited longer than 500 ms are dropped.
    std::vector<Bytes> takeInjectable(uint32_t nowMs);
    bool canInject() const;
    void markInjectBroken();
    // Appends the queued client-only packets (the "!" over a new target) to the `n` bytes the client is
    // about to receive on `s` in a buffer of `cap` bytes: only on the map socket, only while the server
    // stream is understood and stands at a packet boundary, only whole packets. Returns the bytes added.
    size_t appendForClient(SocketId s, uint8_t* buf, size_t n, size_t cap, uint32_t nowMs);

    SocketId mapSocket() const { return mapSocket_; }
    const WorldState& world() const { return world_; }
    const Controller& controller() const { return controller_; }
    std::vector<std::string> takeLog();

private:
    struct Conn {
        enum class Kind { Unknown, Map, Other };
        Kind kind = Kind::Unknown;
        Bytes head;  // first client bytes, until the connection can be classified
        std::unique_ptr<StreamFramer> up;    // client -> server
        std::unique_ptr<StreamFramer> down;  // server -> client
    };

    void feedUp(Conn& c, const uint8_t* data, size_t n, uint32_t nowMs);
    bool justStoppedWalking(uint32_t nowMs) const;

    WorldState world_;
    Controller controller_;
    bool mark_ = true;  // [aim] mark
    bool padWalking_ = false;
    std::optional<uint32_t> walkStopMs_;  // the stick walk ended: what follows is logged for a moment
    std::unordered_map<SocketId, Conn> conns_;
    SocketId mapSocket_ = 0;
    bool downOk_ = false;
    bool injectOk_ = false;
    std::deque<std::pair<uint32_t, Bytes>> queue_;
    std::deque<std::pair<uint32_t, Bytes>> toClient_;  // packets made up for the client, with the time queued
    std::vector<std::string> log_;
};

}  // namespace uapad
