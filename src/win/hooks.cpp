#include "win/hooks.hpp"

#include "win/iat.hpp"
#include "win/log.hpp"
#include "win/runtime.hpp"

namespace uapad::hooks {
namespace {

using recv_t = int(WSAAPI*)(SOCKET, char*, int, int);
using send_t = int(WSAAPI*)(SOCKET, const char*, int, int);
using wsasend_t = int(WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using closesocket_t = int(WSAAPI*)(SOCKET);
using getcursorpos_t = BOOL(WINAPI*)(LPPOINT);
using peekmessage_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using wsagetlasterror_t = int(WSAAPI*)();

recv_t o_recv = nullptr;
send_t o_send = nullptr;
wsasend_t o_wsasend = nullptr;
closesocket_t o_closesocket = nullptr;
getcursorpos_t o_getcursorpos = nullptr;
peekmessage_t o_peekmessage = nullptr;
wsagetlasterror_t o_wsagetlasterror = nullptr;

int WSAAPI h_recv(SOCKET s, char* buf, int len, int flags) {
    const int n = o_recv(s, buf, len, flags);
    if (n > 0 && !(flags & MSG_PEEK)) runtime::onRecv(s, buf, n);
    return n;
}

int WSAAPI h_send(SOCKET s, const char* buf, int len, int flags) {
    const int n = o_send(s, buf, len, flags);
    if (n > 0) runtime::onSent(s, buf, n);
    return n;
}

int WSAAPI h_wsasend(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD sent, DWORD flags, LPWSAOVERLAPPED ov,
                     LPWSAOVERLAPPED_COMPLETION_ROUTINE done) {
    const int r = o_wsasend(s, bufs, count, sent, flags, ov, done);
    if (ov)
        runtime::onOverlappedSend(s);
    else if (r == 0 && sent)
        runtime::onSentBuffers(s, bufs, count, *sent);
    return r;
}

int WSAAPI h_closesocket(SOCKET s) {
    runtime::onClose(s);
    return o_closesocket(s);
}

BOOL WINAPI h_getcursorpos(LPPOINT p) {
    const BOOL ok = o_getcursorpos(p);
    if (ok) runtime::onGetCursorPos(p);
    return ok;
}

BOOL WINAPI h_peekmessage(LPMSG msg, HWND hwnd, UINT first, UINT last, UINT remove) {
    runtime::onMessagePump();
    return o_peekmessage(msg, hwnd, first, last, remove);
}

template <class T>
T proc(HMODULE dll, const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(dll, name)));
}

template <class T>
void hook(HMODULE dll, const char* name, T replacement, T& original) {
    T target = proc<T>(dll, name);
    original = target;
    if (!target) {
        LOG_ERROR("hook %s: export not found", name);
        return;
    }
    if (iat::patch(GetModuleHandleA(nullptr), reinterpret_cast<void*>(target), reinterpret_cast<void*>(replacement)))
        LOG_INFO("hooked %s", name);
    else
        LOG_ERROR("hook %s: not in the client's import table", name);
}

}  // namespace

void install() {
    HMODULE ws2 = GetModuleHandleA("ws2_32.dll");
    if (!ws2) ws2 = LoadLibraryA("ws2_32.dll");
    HMODULE user32 = GetModuleHandleA("user32.dll");
    o_wsagetlasterror = proc<wsagetlasterror_t>(ws2, "WSAGetLastError");
    hook(ws2, "recv", &h_recv, o_recv);
    hook(ws2, "send", &h_send, o_send);
    hook(ws2, "WSASend", &h_wsasend, o_wsasend);
    hook(ws2, "closesocket", &h_closesocket, o_closesocket);
    hook(user32, "GetCursorPos", &h_getcursorpos, o_getcursorpos);
    hook(user32, "PeekMessageA", &h_peekmessage, o_peekmessage);
}

int lastError() { return o_wsagetlasterror ? o_wsagetlasterror() : 0; }

bool sendAll(SOCKET s, const uint8_t* data, int len) {
    if (!o_send) return false;
    int done = 0;
    int retries = 0;
    while (done < len) {
        const int n = o_send(s, reinterpret_cast<const char*>(data) + done, len - done, 0);
        if (n > 0) {
            done += n;
            continue;
        }
        if (n == SOCKET_ERROR && lastError() == WSAEWOULDBLOCK && ++retries <= 50) {
            Sleep(1);
            continue;
        }
        return false;
    }
    return true;
}

}  // namespace uapad::hooks
