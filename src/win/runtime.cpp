#include "win/runtime.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/bytes.hpp"
#include "core/config/config.hpp"
#include "core/session.hpp"
#include "core/version.hpp"
#include "win/hooks.hpp"
#include "win/log.hpp"
#include "win/xinput_pad.hpp"

namespace uapad::runtime {
namespace {

constexpr size_t kMaxPendingButtons = 16;

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
    bool reportedLmb = false;  // last virtual LMB state handed to the client
    std::vector<DIDEVICEOBJECTDATA> pendingButtons;  // for clients that read the mouse buffered
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
    s.mouse = VirtualMouse{};
    s.pendingButtons.clear();
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
    if (lmb != s.reportedLmb) {
        if (s.cfg.buttons == ButtonChannel::DirectInput) {
            DIDEVICEOBJECTDATA ev{};
            ev.dwOfs = DIMOFS_BUTTON0;
            ev.dwData = lmb ? 0x80 : 0x00;
            ev.dwTimeStamp = GetTickCount();
            if (s.pendingButtons.size() >= kMaxPendingButtons) s.pendingButtons.erase(s.pendingButtons.begin());
            s.pendingButtons.push_back(ev);
        } else if (s.hwnd) {
            PostMessageA(s.hwnd, lmb ? WM_LBUTTONDOWN : WM_LBUTTONUP, lmb ? MK_LBUTTON : 0, MAKELPARAM(m.x, m.y));
        }
        s.reportedLmb = lmb;
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
    const TickOutput out = s.session->tick(pad, currentView(), now);
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
    LOG_INFO("game window %p", static_cast<void*>(hwnd));
}

void onMouseState(DWORD cb, LPVOID data) {
    State& s = S();
    if (s.nState++ == 0) LOG_INFO("first mouse GetDeviceState, %lu bytes", (unsigned long)cb);
    guarded("mouse state", [&] {
        if (!onGameThread()) return;
        tick();
        if (!s.mouse.active || !data || cb < sizeof(DIMOUSESTATE)) return;
        auto* st = static_cast<DIMOUSESTATE*>(data);
        st->lX = 0;  // the real mouse must not drag the cursor away from the walking point
        st->lY = 0;
        if (s.cfg.buttons == ButtonChannel::DirectInput) st->rgbButtons[0] = s.mouse.lmb ? 0x80 : 0x00;
    });
}

void onMouseData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD inOut, DWORD capacity, DWORD flags) {
    State& s = S();
    if (s.nData++ == 0)
        LOG_INFO("first mouse GetDeviceData: object %lu bytes, capacity %lu, flags 0x%lx", (unsigned long)cbObjectData,
                 (unsigned long)capacity, (unsigned long)flags);
    guarded("mouse data", [&] {
        if (!onGameThread()) return;
        tick();
        if (!rgdod || !inOut || s.pendingButtons.empty() || cbObjectData < sizeof(DIDEVICEOBJECTDATA)) return;
        DWORD filled = *inOut;
        size_t used = 0;
        while (used < s.pendingButtons.size() && filled < capacity) {
            auto* dst = reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<BYTE*>(rgdod) + size_t(filled) * cbObjectData);
            *dst = s.pendingButtons[used++];
            ++filled;
        }
        *inOut = filled;
        if (!(flags & DIGDD_PEEK)) s.pendingButtons.erase(s.pendingButtons.begin(), s.pendingButtons.begin() + long(used));
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

void onSentBuffers(SOCKET sock, LPWSABUF bufs, DWORD count, DWORD sent) {
    if (!bufs) return;
    std::string joined;
    for (DWORD i = 0; i < count && joined.size() < sent; ++i)
        joined.append(bufs[i].buf, std::min<size_t>(bufs[i].len, sent - joined.size()));
    onSent(sock, joined.data(), int(joined.size()));
}

void onClose(SOCKET sock) {
    State& s = S();
    guarded("close", [&] {
        if (!onGameThread()) return;
        s.session->onClosed(uintptr_t(sock));
        g_mapSocket.store(s.session->mapSocket());
    });
}

void onOverlappedSend(SOCKET sock) {
    guarded("overlapped send", [&] { noteUnseen(sock, true); });
}

}  // namespace uapad::runtime
