#include "win/runtime.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "core/config/config.hpp"
#include "core/version.hpp"
#include "win/hooks.hpp"
#include "win/log.hpp"
#include "win/xinput_pad.hpp"

// M0 diagnostics build: everything passes through untouched. The log records how the client reads
// the mouse and which packet opens each connection, so task 13 can rely on facts.
namespace uapad::runtime {
namespace {

struct State {
    bool initDone = false;
    bool failed = false;
    DWORD gameThread = 0;
    bool warnedThread = false;
    HWND hwnd = nullptr;
    Config cfg;
    XInputPad pad;
    bool loggedConnected = false;
    uint16_t loggedButtons = 0;
    bool ticked = false;
    DWORD lastTickMs = 0;
    std::unordered_map<SOCKET, bool> seenSockets;
    unsigned long nState = 0, nData = 0, nCursor = 0, nPump = 0, nRecv = 0, nSend = 0;
    DWORD lastDiagMs = 0;
};

State& S() {
    static State s;
    return s;
}

std::string exeDir() {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const std::string p(path);
    const size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool onGameThread() {
    State& s = S();
    if (GetCurrentThreadId() == s.gameThread) return true;
    if (!s.warnedThread) {
        s.warnedThread = true;
        LOG_INFO("a hook was called from a non-game thread (%lu); such calls are ignored",
                 (unsigned long)GetCurrentThreadId());
    }
    return false;
}

template <class F>
void guarded(const char* where, F&& body) {
    State& s = S();
    if (!s.initDone || s.failed) return;
    try {
        body();
    } catch (...) {
        LOG_ERROR("internal error in %s; pass-through until the client restarts", where);
        s.failed = true;
    }
}

void logDiagnostics(DWORD now) {
    State& s = S();
    if (s.lastDiagMs != 0 && now - s.lastDiagMs < 10000) return;
    s.lastDiagMs = now;
    LOG_INFO("diag: mouse GetDeviceState=%lu GetDeviceData=%lu | GetCursorPos=%lu | PeekMessageA=%lu | recv=%lu send=%lu",
             s.nState, s.nData, s.nCursor, s.nPump, s.nRecv, s.nSend);
}

void tick() {
    State& s = S();
    const DWORD now = GetTickCount();
    if (s.ticked && now - s.lastTickMs < 5) return;
    s.ticked = true;
    s.lastTickMs = now;
    const PadState p = s.pad.poll();
    if (p.connected != s.loggedConnected) {
        s.loggedConnected = p.connected;
        LOG_INFO("gamepad %s", p.connected ? "connected" : "disconnected");
    }
    if (p.buttons != s.loggedButtons) {
        if ((p.buttons & kPadA) && !(s.loggedButtons & kPadA)) s.pad.rumble(Rumble::TargetSwitch, now);
        s.loggedButtons = p.buttons;
        LOG_INFO("buttons 0x%04x lx=%.2f ly=%.2f lt=%.2f rt=%.2f", unsigned(p.buttons), p.lx, p.ly, p.lt, p.rt);
    }
    s.pad.update(now);
    logDiagnostics(now);
}

}  // namespace

bool healthy() { return S().initDone && !S().failed; }

void init() {
    State& s = S();
    if (s.initDone || s.failed) return;
    try {
        const std::string dir = exeDir();
        s.cfg = parseConfig(readFile(dir + "uaro-pad.ini"));
        wlog::open(dir + "uaro-pad.log", s.cfg.logLevel);
        LOG_INFO("uaRO Pad %s, M0 diagnostics build", version());
        s.gameThread = GetCurrentThreadId();
        hooks::install();
        if (s.pad.load())
            LOG_INFO("XInput: %s", s.pad.dllName());
        else
            LOG_INFO("XInput not found: gamepad unavailable");
        s.initDone = true;
    } catch (...) {
        s.failed = true;
        LOG_ERROR("init failed; pass-through only");
    }
}

void setGameWindow(HWND hwnd) {
    State& s = S();
    if (s.hwnd == hwnd) return;
    s.hwnd = hwnd;
    LOG_INFO("game window %p", static_cast<void*>(hwnd));
}

void onMouseState(DWORD cb, LPVOID data) {
    (void)data;
    State& s = S();
    if (s.nState++ == 0) LOG_INFO("first mouse GetDeviceState, %lu bytes", (unsigned long)cb);
    guarded("mouse state", [&] {
        if (onGameThread()) tick();
    });
}

void onMouseData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD inOut, DWORD capacity, DWORD flags) {
    (void)rgdod;
    State& s = S();
    if (s.nData++ == 0)
        LOG_INFO("first mouse GetDeviceData: object %lu bytes, capacity %lu, got %lu, flags 0x%lx",
                 (unsigned long)cbObjectData, (unsigned long)capacity, (unsigned long)(inOut ? *inOut : 0),
                 (unsigned long)flags);
    guarded("mouse data", [&] {
        if (onGameThread()) tick();
    });
}

void onGetCursorPos(LPPOINT p) {
    (void)p;
    ++S().nCursor;
}

void onMessagePump() {
    State& s = S();
    ++s.nPump;
    guarded("message pump", [&] {
        if (onGameThread() && (!s.ticked || GetTickCount() - s.lastTickMs >= 50)) tick();
    });
}

void onRecv(SOCKET sock, const char* buf, int n) {
    (void)sock;
    (void)buf;
    (void)n;
    ++S().nRecv;
}

void onSent(SOCKET sock, const char* buf, int n) {
    State& s = S();
    ++s.nSend;
    guarded("send", [&] {
        if (n >= 2 && s.seenSockets.emplace(sock, true).second)
            LOG_INFO("socket %lu: first client packet 0x%04x (%d bytes)", (unsigned long)sock,
                     unsigned(uint8_t(buf[0]) | (uint8_t(buf[1]) << 8)), n);
    });
}

void onSentBuffers(SOCKET sock, LPWSABUF bufs, DWORD count, DWORD sent) {
    if (!bufs || count == 0 || sent == 0) return;
    onSent(sock, bufs[0].buf, int(bufs[0].len < sent ? bufs[0].len : sent));
}

void onClose(SOCKET sock) {
    State& s = S();
    guarded("close", [&] {
        s.seenSockets.erase(sock);
        LOG_INFO("socket %lu closed", (unsigned long)sock);
    });
}

}  // namespace uapad::runtime
