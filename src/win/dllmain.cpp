#include <winsock2.h>
#include <windows.h>

#include <dinput.h>

#include <string>

#include "win/di_wrappers.hpp"
#include "win/log.hpp"
#include "win/runtime.hpp"

namespace {

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
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
    return TRUE;
}
