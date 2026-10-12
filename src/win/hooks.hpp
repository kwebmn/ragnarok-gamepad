#pragma once
#include <winsock2.h>
#include <windows.h>

#include <cstdint>

namespace uapad::hooks {

// Patches the client's import table: recv, send, WSASend, closesocket, GetCursorPos, PeekMessageA.
void install();
// Sends our own request through the original send(); finishes a partial send right away.
bool sendAll(SOCKET s, const uint8_t* data, int len);
int lastError();

}  // namespace uapad::hooks
