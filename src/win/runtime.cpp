#include "win/runtime.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/bytes.hpp"
#include "core/config/config.hpp"
#include "core/logic/cursor_bridge.hpp"
#include "core/logic/vbutton.hpp"
#include "core/session.hpp"
#include "core/version.hpp"
#include "win/hooks.hpp"
#include "win/log.hpp"
#include "win/xinput_pad.hpp"

namespace uapad::runtime {
namespace {

struct State {
    bool initDone = false;
    bool failed = false;
    DWORD gameThread = 0;
    bool warnedThread = false;
    HWND hwnd = nullptr;
    Config cfg;
    std::unique_ptr<Session> session;
    XInputPad pad;
    VirtualMouse mouse;        // what the client currently gets from us
    VirtualButton button;      // virtual LMB as the client has been told about it
    bool loggedNullRead = false;
    CursorBridge bridge;     // drives the client's own cursor (it only reads DirectInput deltas)
    bool lastFocused = false;
    int loggedSteps = 0;
    int pointersHooked = 0;
    DWORD lastPointerCheckMs = 0;
    bool checkedPointers = false;
    bool loggedBufferRead = false;
    bool loggedMark = false;
    bool realLmb = false;      // the real left button as DirectInput last reported it
    bool realClicked = false;  // it went down since the last tick
    bool loggedConnected = false;
    uint16_t loggedButtons = 0;
    bool ticked = false;
    DWORD lastTickMs = 0;
    unsigned long nState = 0, nData = 0, nCursor = 0, nPump = 0, nRecv = 0, nSend = 0;
    DWORD lastDiagMs = 0;
};

State& S() {
    static State s;
    return s;
}

// Hooks running off the game thread only record that the map socket carried traffic we could not
// parse; the game thread turns that into Session::onUnobserved on its next tick.
std::atomic<uintptr_t> g_mapSocket{0};
std::atomic<bool> g_unseenUp{false};
std::atomic<bool> g_unseenDown{false};

void noteUnseen(SOCKET sock, bool up) {
    if (uintptr_t(sock) != g_mapSocket.load()) return;
    (up ? g_unseenUp : g_unseenDown).store(true);
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

void fail(const char* where) {
    State& s = S();
    if (!s.failed) LOG_ERROR("internal error in %s; pass-through until the client restarts", where);
    s.failed = true;
    // Never leave the client with a held virtual button: queue a release (handed out by onMouseData
    // even after the failure) or, for the message channel, post it right away.
    if (s.button.release(GetTickCount()) && s.cfg.buttons == ButtonChannel::Messages && s.hwnd)
        PostMessageA(s.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(s.mouse.x, s.mouse.y));
    s.mouse = VirtualMouse{};
    s.pad.stopRumble();
}

template <class F>
void guarded(const char* where, F&& body) {
    State& s = S();
    if (!s.initDone || s.failed) return;
    try {
        body();
    } catch (const std::exception& e) {
        LOG_ERROR("%s: %s", where, e.what());
        fail(where);
    } catch (...) {
        fail(where);
    }
}

ViewInfo currentView() {
    State& s = S();
    ViewInfo v;
    if (!s.hwnd) return v;
    v.focused = GetForegroundWindow() == s.hwnd;
    v.mouseClicked = s.realClicked;
    s.realClicked = false;
    RECT r;
    if (GetClientRect(s.hwnd, &r)) {
        v.width = r.right - r.left;
        v.height = r.bottom - r.top;
    }
    return v;
}

void applyMouse(const VirtualMouse& m) {
    State& s = S();
    const bool lmb = m.active && m.lmb;
    if (s.button.set(lmb, GetTickCount())) {
        if (s.cfg.buttons == ButtonChannel::Messages && s.hwnd)
            PostMessageA(s.hwnd, lmb ? WM_LBUTTONDOWN : WM_LBUTTONUP, lmb ? MK_LBUTTON : 0, MAKELPARAM(m.x, m.y));
    } else if (lmb && s.cfg.buttons == ButtonChannel::Messages && s.hwnd && (m.x != s.mouse.x || m.y != s.mouse.y)) {
        PostMessageA(s.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(m.x, m.y));
    }
    s.mouse = m;
}

void flushPackets() {
    State& s = S();
    std::vector<Bytes> ready = s.session->takeInjectable(GetTickCount());
    if (ready.empty()) return;
    const SOCKET sock = SOCKET(s.session->mapSocket());
    for (const Bytes& p : ready) {
        if (!hooks::sendAll(sock, p.data(), int(p.size()))) {
            LOG_ERROR("sending a pad request failed (WSA error %d)", hooks::lastError());
            s.session->markInjectBroken();
            return;
        }
        LOG_DEBUG("sent 0x%04x (%u bytes)", unsigned(rd16(p.data(), 0)), unsigned(p.size()));
    }
}

void logDiagnostics(DWORD now) {
    State& s = S();
    if (s.lastDiagMs != 0 && now - s.lastDiagMs < 10000) return;
    s.lastDiagMs = now;
    LOG_INFO("diag: mouse GetDeviceState=%lu GetDeviceData=%lu | GetCursorPos=%lu | PeekMessageA=%lu | recv=%lu send=%lu | map=%s self=%lu",
             s.nState, s.nData, s.nCursor, s.nPump, s.nRecv, s.nSend, s.session->mapSocket() ? "yes" : "no",
             (unsigned long)s.session->world().selfId());
}

void logPad(const PadState& p) {
    State& s = S();
    if (p.connected != s.loggedConnected) {
        s.loggedConnected = p.connected;
        LOG_INFO("gamepad %s", p.connected ? "connected" : "disconnected");
    }
    if (p.buttons != s.loggedButtons) {
        s.loggedButtons = p.buttons;
        LOG_DEBUG("buttons 0x%04x", unsigned(p.buttons));
    }
}

void tick() {
    State& s = S();
    const DWORD now = GetTickCount();
    if (s.ticked && now - s.lastTickMs < 5) return;  // the client may poll several times per frame
    s.ticked = true;
    s.lastTickMs = now;
    if (g_unseenUp.exchange(false)) s.session->onUnobserved(s.session->mapSocket(), Direction::ClientToServer);
    if (g_unseenDown.exchange(false)) s.session->onUnobserved(s.session->mapSocket(), Direction::ServerToClient);
    const PadState pad = s.pad.poll();
    logPad(pad);
    // The client fills its send/recv pointers itself (and may refill them): keep them going through us.
    if (!s.checkedPointers || now - s.lastPointerCheckMs >= (s.pointersHooked >= 2 ? 2000u : 250u)) {
        s.checkedPointers = true;
        s.lastPointerCheckMs = now;
        s.pointersHooked = hooks::patchClientPointers();
    }
    const ViewInfo view = currentView();
    if (view.focused != s.lastFocused) {  // the real mouse may have moved the client's cursor meanwhile
        s.lastFocused = view.focused;
        s.bridge.reset();
    }
    const TickOutput out = s.session->tick(pad, view, now);
    for (const std::string& note : out.notes) LOG_INFO("%s", note.c_str());
    for (const std::string& line : s.session->takeLog()) LOG_INFO("%s", line.c_str());
    if (s.cfg.rumble && out.rumble != Rumble::None) s.pad.rumble(out.rumble, now);
    s.pad.update(now);
    applyMouse(out.mouse);
    flushPackets();
    g_mapSocket.store(s.session->mapSocket());
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
        LOG_INFO("uaRO Pad %s", version());
        s.gameThread = GetCurrentThreadId();
        s.session = std::make_unique<Session>(s.cfg);
        if (!s.cfg.enabled) LOG_INFO("starts switched off ([general] enabled = 0); press Back to switch on");
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
    RECT w{}, c{};
    GetWindowRect(hwnd, &w);
    GetClientRect(hwnd, &c);
    LOG_INFO("game window %p: style 0x%08lx, window %ldx%ld at (%ld,%ld), client %ldx%ld", static_cast<void*>(hwnd),
             (unsigned long)GetWindowLongA(hwnd, GWL_STYLE), w.right - w.left, w.bottom - w.top, w.left, w.top,
             c.right - c.left, c.bottom - c.top);
}

void onMouseState(DWORD cb, LPVOID data) {
    State& s = S();
    if (s.nState++ == 0) LOG_INFO("first mouse GetDeviceState, %lu bytes", (unsigned long)cb);
    guarded("mouse state", [&] {
        if (!onGameThread()) return;
        tick();
        if (!data || cb < sizeof(DIMOUSESTATE) || !s.hwnd) return;
        auto* st = static_cast<DIMOUSESTATE*>(data);
        // The device's own button, before we put the virtual one in: a click means the player took over.
        const bool realLmb = (st->rgbButtons[0] & 0x80) != 0;
        if (realLmb && !s.realLmb) s.realClicked = true;
        s.realLmb = realLmb;
        if (!s.cfg.mouseFreedom && !s.mouse.active) {
            s.bridge.reset();  // the real mouse moves the client's cursor now; resync before driving it again
            return;
        }
        // Target: the walking point while the stick is used, otherwise the Windows cursor (mouse freedom).
        POINT target{s.mouse.x, s.mouse.y};
        if (!s.mouse.active) {
            GetCursorPos(&target);
            ScreenToClient(s.hwnd, &target);
        }
        RECT rc{};
        GetClientRect(s.hwnd, &rc);
        const CursorBridge::Step step = s.bridge.step(target.x, target.y, rc.right - rc.left, rc.bottom - rc.top);
        if (s.loggedSteps < 12 || step.settling) {
            ++s.loggedSteps;
            LOG_INFO("cursor: target (%ld,%ld) in %ldx%ld -> dx=%ld dy=%ld%s (raw dx=%ld dy=%ld)", target.x, target.y,
                     rc.right - rc.left, rc.bottom - rc.top, step.dx, step.dy, step.settling ? " settling" : "", st->lX,
                     st->lY);
        }
        st->lX = step.dx;
        st->lY = step.dy;
        if (step.settling) {  // the cursor sits in the corner this frame: no clicks there
            st->rgbButtons[0] = 0;
            st->rgbButtons[1] = 0;
            return;
        }
        if (s.mouse.active && s.cfg.buttons == ButtonChannel::DirectInput) st->rgbButtons[0] = s.mouse.lmb ? 0x80 : 0x00;
    });
}

namespace {

// Adds our queued virtual button edges to a buffered DirectInput read.
void deliverButtons(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD inOut, DWORD capacity, DWORD flags) {
    State& s = S();
    if (s.cfg.buttons != ButtonChannel::DirectInput || !inOut || cbObjectData < sizeof(DIDEVICEOBJECTDATA)) return;
    if (!rgdod) {  // count query (DIGDD_PEEK) or flush: our events count too
        *inOut += DWORD(s.button.pending());
        if (!(flags & DIGDD_PEEK)) s.button.flush();
        return;
    }
    const DWORD filled = *inOut;
    if (filled >= capacity) return;
    const std::vector<VirtualButton::Event> events = s.button.take(capacity - filled, (flags & DIGDD_PEEK) != 0);
    for (size_t i = 0; i < events.size(); ++i) {
        DIDEVICEOBJECTDATA ev{};
        ev.dwOfs = DIMOFS_BUTTON0;
        ev.dwData = events[i].down ? 0x80 : 0x00;
        ev.dwTimeStamp = events[i].timeMs;
        std::memcpy(reinterpret_cast<BYTE*>(rgdod) + (size_t(filled) + i) * cbObjectData, &ev, sizeof ev);
    }
    *inOut = filled + DWORD(events.size());
}

}  // namespace

void onMouseData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD inOut, DWORD capacity, DWORD flags) {
    State& s = S();
    ++s.nData;
    bool& logged = rgdod ? s.loggedBufferRead : s.loggedNullRead;
    if (!logged) {
        logged = true;
        LOG_INFO("mouse GetDeviceData with %s buffer: object %lu bytes, capacity %lu, flags 0x%lx",
                 rgdod ? "a" : "a NULL (count/flush)", (unsigned long)cbObjectData, (unsigned long)capacity,
                 (unsigned long)flags);
    }
    if (!s.initDone) return;
    if (s.failed) {  // still hand out the release queued by fail(), nothing else
        try {
            if (GetCurrentThreadId() == s.gameThread) deliverButtons(cbObjectData, rgdod, inOut, capacity, flags);
        } catch (...) {
        }
        return;
    }
    guarded("mouse data", [&] {
        if (!onGameThread()) return;
        tick();
        deliverButtons(cbObjectData, rgdod, inOut, capacity, flags);
    });
}

void onGetCursorPos(LPPOINT p) {
    State& s = S();
    ++s.nCursor;
    guarded("cursor", [&] {
        if (!p || !s.mouse.active || !s.hwnd || !onGameThread()) return;
        POINT pt{s.mouse.x, s.mouse.y};
        if (ClientToScreen(s.hwnd, &pt)) *p = pt;
    });
}

void onMessagePump() {
    State& s = S();
    ++s.nPump;
    guarded("message pump", [&] {
        if (onGameThread() && (!s.ticked || GetTickCount() - s.lastTickMs >= 50)) tick();
    });
}

void onRecv(SOCKET sock, const char* buf, int n) {
    State& s = S();
    ++s.nRecv;
    guarded("recv", [&] {
        if (n <= 0) return;
        if (!onGameThread()) {
            noteUnseen(sock, false);
            return;
        }
        s.session->onServerData(uintptr_t(sock), reinterpret_cast<const uint8_t*>(buf), size_t(n), GetTickCount());
    });
}

int extendRecv(SOCKET sock, char* buf, int n, int cap) {
    State& s = S();
    int added = 0;
    guarded("recv extend", [&] {
        if (!buf || n <= 0 || cap <= n || !onGameThread()) return;
        added = int(s.session->appendForClient(uintptr_t(sock), reinterpret_cast<uint8_t*>(buf), size_t(n),
                                               size_t(cap), GetTickCount()));
        if (added == 0) return;
        if (!s.loggedMark) {
            s.loggedMark = true;
            LOG_INFO("target marks reach the client (%d bytes after %d received)", added, n);
        } else {
            LOG_DEBUG("target mark: %d bytes after %d received", added, n);
        }
    });
    return added;
}

void onSent(SOCKET sock, const char* buf, int n) {
    State& s = S();
    ++s.nSend;
    guarded("send", [&] {
        if (n <= 0) return;
        if (!onGameThread()) {
            noteUnseen(sock, true);
            return;
        }
        s.session->onClientSent(uintptr_t(sock), reinterpret_cast<const uint8_t*>(buf), size_t(n), GetTickCount());
        g_mapSocket.store(s.session->mapSocket());
        flushPackets();
    });
}

void onClose(SOCKET sock) {
    State& s = S();
    guarded("close", [&] {
        if (!onGameThread()) return;
        s.session->onClosed(uintptr_t(sock));
        g_mapSocket.store(s.session->mapSocket());
    });
}

DWORD mouseCooperativeLevel(DWORD flags) {
    State& s = S();
    if (!healthy() || !s.cfg.mouseFreedom || !(flags & DISCL_EXCLUSIVE)) return flags;
    const DWORD freed = (flags & ~DWORD(DISCL_EXCLUSIVE)) | DISCL_NONEXCLUSIVE;
    LOG_INFO("mouse freedom: cooperative level 0x%lx -> 0x%lx", (unsigned long)flags, (unsigned long)freed);
    return freed;
}

}  // namespace uapad::runtime
