#include <cmath>

#include "core/proto/c2s.hpp"
#include "core/proto/packet_tables.hpp"
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

// The map server's answer to a name request for `id` (0x0095).
Bytes nameReply(uint32_t id) {
    Bytes b(size_t(s2cLength(0x0095)), 0);
    wr16(b, 0, 0x0095);
    wr32(b, 2, id);
    return b;
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
    CHECK_EQ(ready.size(), 2u);  // the attack and the name request behind the new target's "!"
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
    CHECK_EQ(s.takeInjectable(130).size(), 2u);  // the attack and the name request
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

TEST(session_unobserved_client_send_stops_injection) {
    Session s{Config{}};
    connect(s);
    CHECK(s.canInject());
    s.onUnobserved(kLogin, Direction::ClientToServer);  // some other socket: no effect
    CHECK(s.canInject());
    s.onUnobserved(kMap, Direction::ClientToServer);  // bytes left that we never saw: no boundary is known
    CHECK(!s.canInject());
    CHECK(!s.takeLog().empty());
}

TEST(session_unobserved_server_data_stops_parsing) {
    Session s{Config{}};
    connect(s);
    s.onUnobserved(kMap, Direction::ServerToClient);
    CHECK(!s.canInject());
    Bytes mob(54, 0);
    wr16(mob, 0, 0x0078);
    wr32(mob, 2, 110000002);
    wr16(mob, 14, 1002);
    encodePos3(mob, 46, Cell{103, 100}, 0);
    s.onServerData(kMap, mob.data(), mob.size(), 20);
    CHECK_EQ(s.world().actors().size(), 1u);  // the stream is no longer trusted: nothing new is parsed
}

TEST(session_mark_rides_along_with_the_server_reply) {
    Session s{Config{}};
    connect(s);
    TickOutput o = s.tick(pressA(), kView, 100);  // the Poring becomes the target
    CHECK_EQ(o.mark, 110000001u);
    std::vector<Bytes> ready = s.takeInjectable(100);
    CHECK_EQ(ready.size(), 2u);  // the attack, then a name request: the server answers right away
    CHECK(ready.size() == 2 && rd16(ready[1].data(), 0) == 0x008c && rd32(ready[1].data(), 7) == 110000001u);
    uint8_t buf[64] = {0};
    const Bytes reply = nameReply(110000001);
    std::copy(reply.begin(), reply.end(), buf);
    s.onServerData(kMap, buf, reply.size(), 120);
    CHECK_EQ(s.appendForClient(kMap, buf, reply.size(), sizeof buf, 120), 7u);
    CHECK_EQ(rd16(buf, reply.size()), 0x00c0);  // "!" over the Poring, after the whole reply
    CHECK_EQ(rd32(buf, reply.size() + 2), 110000001u);
    CHECK_EQ(buf[reply.size() + 6], 0);
    CHECK_EQ(s.appendForClient(kMap, buf, reply.size() + 7, sizeof buf, 130), 0u);  // shown once
}

TEST(session_mark_waits_for_a_packet_boundary_and_room) {
    Session s{Config{}};
    connect(s);
    s.tick(pressA(), kView, 100);
    const Bytes reply = nameReply(110000001);
    uint8_t buf[64] = {0};
    s.onServerData(kMap, reply.data(), 10, 110);  // the client gets the first 10 bytes of the reply
    CHECK_EQ(s.appendForClient(kMap, buf, 10, sizeof buf, 110), 0u);  // never inside a packet
    s.onServerData(kMap, reply.data() + 10, reply.size() - 10, 120);
    const size_t n = reply.size() - 10;
    CHECK_EQ(s.appendForClient(kMap, buf, n, n + 6, 120), 0u);  // a boundary, but no room for 7 bytes
    CHECK_EQ(s.appendForClient(kMap, buf, n, n + 7, 120), 7u);  // exactly enough room
}

TEST(session_mark_only_on_the_map_socket_and_only_while_fresh) {
    Session s{Config{}};
    connect(s);
    s.tick(pressA(), kView, 100);
    uint8_t buf[64] = {0};
    CHECK_EQ(s.appendForClient(kLogin, buf, 0, sizeof buf, 150), 0u);  // another socket
    CHECK_EQ(s.appendForClient(kMap, buf, 0, sizeof buf, 1200), 0u);   // over a second old: dropped
    CHECK_EQ(s.appendForClient(kMap, buf, 0, sizeof buf, 1210), 0u);
}

TEST(session_mark_off_in_config) {
    Config cfg;
    cfg.mark = false;
    Session s{cfg};
    connect(s);
    TickOutput o = s.tick(pressA(), kView, 100);
    CHECK_EQ(o.mark, 110000001u);  // the controller still reports the new target...
    CHECK_EQ(s.takeInjectable(100).size(), 1u);  // ...but no name request goes out
    uint8_t buf[64] = {0};
    CHECK_EQ(s.appendForClient(kMap, buf, 0, sizeof buf, 110), 0u);  // and no "!"
}

TEST(session_mark_dropped_with_the_connection_or_a_desync) {
    Session s{Config{}};
    connect(s);
    s.tick(pressA(), kView, 100);
    Bytes junk = {0x01, 0x00, 0xaa, 0xbb};  // 0x0001 is not a server packet
    s.onServerData(kMap, junk.data(), junk.size(), 110);
    uint8_t buf[64] = {0};
    CHECK_EQ(s.appendForClient(kMap, buf, junk.size(), sizeof buf, 110), 0u);  // the stream is lost
    Session s2{Config{}};
    connect(s2);
    s2.tick(pressA(), kView, 100);
    s2.onClosed(kMap);
    connect(s2);  // a new map server connection
    CHECK_EQ(s2.appendForClient(kMap, buf, 0, sizeof buf, 150), 0u);
}

TEST(session_client_walk_requests_teach_the_camera) {
    Session s{Config{}};
    connect(s);
    PadState walk;
    walk.connected = true;
    walk.lx = 1.0f;
    s.tick(walk, kView, 100);  // the pad holds the walking point right of the centre
    const Bytes req = buildWalk(Cell{100, 107});
    s.onClientSent(kMap, req.data(), req.size(), 110);  // and the client walks north
    CHECK_EQ(s.controller().camera().samples(), 1u);
    CHECK(std::fabs(s.controller().camera().radians() - 3.14159265358979323846 / 2) < 1e-6);
}

TEST(session_client_skill_resumes_the_pad_attack) {
    Session s{Config{}};
    connect(s);
    s.tick(pressA(), kView, 100);
    s.takeInjectable(100);
    PadState none;
    none.connected = true;
    s.tick(none, kView, 150);
    const Bytes skill = buildSkillToId(46, 10, 110000001);  // Double Strafe from the keyboard
    s.onClientSent(kMap, skill.data(), skill.size(), 200);
    s.tick(none, kView, 500);
    const std::vector<Bytes> ready = s.takeInjectable(500);
    CHECK_EQ(ready.size(), 1u);
    CHECK(!ready.empty() && rd16(ready[0].data(), 0) == 0x0190);
}
