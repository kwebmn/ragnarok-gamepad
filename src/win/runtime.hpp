#pragma once
#include <winsock2.h>
#include <windows.h>

#include <dinput.h>

// Glue between the hooks and the portable core. Every entry point is fail-open:
// after an internal error it does nothing and the client keeps working.
namespace uapad::runtime {

void init();
bool healthy();
void setGameWindow(HWND hwnd);
void onMouseState(DWORD cb, LPVOID data);
void onMouseData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD inOut, DWORD capacity, DWORD flags);
void onGetCursorPos(LPPOINT p);
void onMessagePump();
void onRecv(SOCKET sock, const char* buf, int n);
void onSent(SOCKET sock, const char* buf, int n);
void onSentBuffers(SOCKET sock, LPWSABUF bufs, DWORD count, DWORD sent);
void onClose(SOCKET sock);
// The client used an overlapped WSASend: those bytes are never seen, so injecting becomes unsafe.
void onOverlappedSend(SOCKET sock);

}  // namespace uapad::runtime
