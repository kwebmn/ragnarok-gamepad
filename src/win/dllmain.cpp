#include <winsock2.h>
#include <windows.h>

#include <dinput.h>

#include <string>

#include "win/di_wrappers.hpp"
#include "win/game_window.hpp"
#include "win/log.hpp"
#include "win/runtime.hpp"

namespace {

// Where dinput8.dll came from before and after we loaded the system dinput.dll at process start.
// ROExt ships as dinput8.dll and is pulled in by the system dinput.dll from the client folder.
char g_dinput8Before[MAX_PATH] = "not loaded";
char g_dinput8After[MAX_PATH] = "not loaded";

void modulePath(const char* name, char* out) {
    if (HMODULE m = GetModuleHandleA(name)) GetModuleFileNameA(m, out, MAX_PATH);
}

using DirectInputCreateA_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);

// The real dinput.dll from the system folder (a 32-bit process on 64-bit Windows gets SysWOW64).
DirectInputCreateA_t systemDirectInputCreateA() {
    static DirectInputCreateA_t fn = nullptr;
    if (fn) return fn;
    char dir[MAX_PATH];
    const UINT n = GetSystemDirectoryA(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return nullptr;
    const std::string path = std::string(dir) + "\\dinput.dll";
    if (HMODULE m = LoadLibraryA(path.c_str()))
        fn = reinterpret_cast<DirectInputCreateA_t>(reinterpret_cast<void*>(GetProcAddress(m, "DirectInputCreateA")));
    return fn;
}

}  // namespace

// Exported as DirectInputCreateA (see dinput.def): the only DINPUT.dll import of uaRO-1.1.exe.
extern "C" HRESULT WINAPI Proxy_DirectInputCreateA(HINSTANCE inst, DWORD version, LPDIRECTINPUTA* out, LPUNKNOWN outer) {
    uapad::runtime::init();
    LOG_INFO("dinput8.dll at process start: %s; after the system dinput.dll: %s", g_dinput8Before, g_dinput8After);
    const DirectInputCreateA_t create = systemDirectInputCreateA();
    if (!create) {
        LOG_ERROR("system dinput.dll could not be loaded");
        return E_FAIL;
    }
    const HRESULT hr = create(inst, version, out, outer);
    LOG_INFO("DirectInputCreateA(version 0x%04lx) = 0x%08lx", (unsigned long)version, (unsigned long)hr);
    if (SUCCEEDED(hr) && out && *out && !outer && uapad::runtime::healthy()) *out = uapad::wrapDirectInput(*out);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // Load the system dinput.dll now, as the loader would without us. It pulls in dinput8.dll
        // (ROExt in the uaRO client), whose hooks must be in place before the game window exists.
        modulePath("dinput8.dll", g_dinput8Before);
        systemDirectInputCreateA();
        modulePath("dinput8.dll", g_dinput8After);
    } else if (reason == DLL_PROCESS_DETACH) {
        uapad::gamewindow::releaseCursor();  // the cursor must not stay confined to a window that is gone
    }
    return TRUE;
}
