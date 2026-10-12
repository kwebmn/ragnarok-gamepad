#include "core/session.hpp"
#include "test.hpp"

using namespace uapad;

namespace {

constexpr Session::SocketId kLogin = 5;
constexpr Session::SocketId kMap = 7;
constexpr uint32_t kSelf = 2000001;
const ViewInfo kView{true, 800, 600};

Bytes wantToConnect() {
    Bytes b(26, 0);
    wr16(b, 0, 0x009b);
    wr32(b, 4, kSelf);
    return b;
}

// What the map server sends on a fresh connection: the raw account id, auth ok at (100, 100),
// and one Poring standing at (102, 100).
Bytes mapLoginStream() {
    Bytes s = {0x81, 0x84, 0x1e, 0x00};  // 2000001, little endian, no packet header
    Bytes auth(11, 0);
    wr16(auth, 0, 0x0073);
    encodePos3(auth, 6, Cell{100, 100}, 0);
    Bytes mob(54, 0);
    wr16(mob, 0, 0x0078);
    wr32(mob, 2, 110000001);
    wr16(mob, 14, 1002);
    encodePos3(mob, 46, Cell{102, 100}, 0);
    s.insert(s.end(), auth.begin(), auth.end());
    s.insert(s.end(), mob.begin(), mob.end());
    return s;
}

void connect(Session& s) {
    Bytes w = wantToConnect();
    s.onClientSent(kMap, w.data(), w.size(), 0);
    Bytes down = mapLoginStream();
    s.onServerData(kMap, down.data(), 2, 0);  // the raw account id arrives in two pieces
    s.onServerData(kMap, down.data() + 2, down.size() - 2, 0);
}

PadState pressA() {
    PadState p;
    p.connected = true;
    p.buttons = kPadA;
    return p;
}

}  // namespace

TEST(session_ignores_login_traffic_and_finds_map_socket) {
    Session s{Config{}};
    Bytes login(55, 0);
    wr16(login, 0, 0x0064);
    s.onClientSent(kLogin, login.data(), login.size(), 0);
    Bytes reply = {0x69, 0x00, 0x99, 0x99};
    s.onServerData(kLogin, reply.data(), reply.size(), 0);
    CHECK_EQ(s.mapSocket(), Session::SocketId(0));
    connect(s);
    CHECK_EQ(s.mapSocket(), kMap);
    CHECK_EQ(s.world().selfId(), kSelf);
    CHECK(s.world().inGame());
    CHECK_EQ(s.world().actors().size(), 1u);
    CHECK(s.canInject());
}

TEST(session_tick_queues_requests_for_injection) {
    Session s{Config{}};
    connect(s);
    TickOutput o = s.tick(pressA(), kView, 100);
    CHECK_EQ(o.packets.size(), 1u);
    std::vector<Bytes> ready = s.takeInjectable(100);
    CHECK_EQ(ready.size(), 1u);
    CHECK(!ready.empty() && rd16(ready[0].data(), 0) == 0x0190);
    CHECK(s.takeInjectable(110).empty());
}

TEST(session_injection_waits_for_client_packet_boundary) {
    Session s{Config{}};
    connect(s);
    Bytes half = {0x90, 0x01, 0, 0, 0};  // the client is in the middle of sending a 19-byte 0x0190
    s.onClientSent(kMap, half.data(), half.size(), 50);
    CHECK(!s.canInject());
    s.tick(pressA(), kView, 100);
    CHECK(s.takeInjectable(100).empty());
    Bytes rest(14, 0);
    s.onClientSent(kMap, rest.data(), rest.size(), 120);
    CHECK(s.canInject());
    CHECK_EQ(s.takeInjectable(130).size(), 1u);
}

TEST(session_drops_requests_that_waited_too_long) {
    Session s{Config{}};
    connect(s);
    Bytes half = {0x90, 0x01};
    s.onClientSent(kMap, half.data(), half.size(), 50);
    s.tick(pressA(), kView, 100);
    Bytes rest(17, 0);
    s.onClientSent(kMap, rest.data(), rest.size(), 700);
    CHECK(s.takeInjectable(700).empty());  // queued at 100: 600 ms is too old
    CHECK(!s.takeLog().empty());
}

TEST(session_hotkey_changes_from_the_client_update_the_bar) {
    Session s{Config{}};
    connect(s);
    Bytes change(11, 0);
    wr16(change, 0, 0x02ba);
    wr16(change, 2, 3);
    change[4] = 1;
    wr32(change, 5, 28);
    wr16(change, 9, 5);
    s.onClientSent(kMap, change.data(), change.size(), 10);
    CHECK(s.world().hotkey(3) == (Hotkey{1, 28, 5}));
}

TEST(session_reused_socket_handle_after_close) {
    Session s{Config{}};
    Bytes login(55, 0);
    wr16(login, 0, 0x0064);
    s.onClientSent(kLogin, login.data(), login.size(), 0);
    s.onClosed(kLogin);
    Bytes w = wantToConnect();
    s.onClientSent(kLogin, w.data(), w.size(), 10);  // Windows reused the handle for the map server
    CHECK_EQ(s.mapSocket(), kLogin);
}

TEST(session_server_desync_stops_requests_but_not_walking) {
    Session s{Config{}};
    connect(s);
    Bytes junk = {0x01, 0x00, 0xaa, 0xbb};  // 0x0001 is not a server packet
    s.onServerData(kMap, junk.data(), junk.size(), 10);
    CHECK(!s.canInject());
    PadState walk;
    walk.connected = true;
    walk.lx = 1.0f;
    TickOutput o = s.tick(walk, kView, 20);
    CHECK(o.mouse.active);
    s.tick(pressA(), kView, 40);  // stick released + A: a stop and an attack, both dropped
    CHECK(s.takeInjectable(40).empty());
    CHECK(!s.takeLog().empty());
}

TEST(session_map_close_resets_connection) {
    Session s{Config{}};
    connect(s);
    s.onClosed(kMap);
    CHECK_EQ(s.mapSocket(), Session::SocketId(0));
    CHECK(!s.canInject());
}

TEST(session_logs_hotkey_bar_arrival) {
    Session s{Config{}};
    connect(s);
    s.takeLog();
    Bytes hk(191, 0);
    wr16(hk, 0, 0x02b9);
    hk[2] = 1;  // slot 0: Bash lv 10
    wr32(hk, 3, 5);
    wr16(hk, 7, 10);
    s.onServerData(kMap, hk.data(), hk.size(), 10);
    bool found = false;
    for (const std::string& line : s.takeLog())
        if (line.find("hotkey bar") != std::string::npos && line.find("1 filled") != std::string::npos) found = true;
    CHECK(found);
}
