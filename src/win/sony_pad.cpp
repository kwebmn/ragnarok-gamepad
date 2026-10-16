#include "win/sony_pad.hpp"

#include <setupapi.h>
extern "C" {  // MinGW's HID headers declare C functions without saying so
#include <hidsdi.h>
}

#include "win/log.hpp"

namespace uapad {
namespace {

// A HID device's path whose vendor and product are a Sony controller's - the first found - and its input report's
// length; how it is connected from its path (Bluetooth's go through the BTHENUM or a {00001124-...} service).
struct Found {
    std::string path;
    sony::Model model = sony::Model::None;
    USHORT inputLength = 64;
};

Found findController() {
    Found f;
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsA(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return f;
    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof iface;
    for (DWORD i = 0; f.model == sony::Model::None && SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &iface); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailA(set, &iface, nullptr, 0, &size, nullptr);
        if (!size) continue;
        std::vector<uint8_t> buf(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(set, &iface, detail, size, nullptr, nullptr)) continue;
        HANDLE h = CreateFileA(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES a{};
        a.Size = sizeof a;
        if (HidD_GetAttributes(h, &a)) {
            const sony::Model m = sony::modelOf(a.VendorID, a.ProductID);
            PHIDP_PREPARSED_DATA pre = nullptr;
            HIDP_CAPS caps{};
            // A controller shows up as several collections: the gamepad's has input reports of 10 bytes or more.
            if (m != sony::Model::None && HidD_GetPreparsedData(h, &pre)) {
                if (HidP_GetCaps(pre, &caps) == HIDP_STATUS_SUCCESS && caps.InputReportByteLength >= 10) {
                    f.path = detail->DevicePath;
                    f.model = m;
                    f.inputLength = caps.InputReportByteLength;
                }
                HidD_FreePreparsedData(pre);
            }
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    return f;
}

}  // namespace

SonyPad::~SonyPad() { close(); }

const char* SonyPad::name() const {
    if (model_ == sony::Model::DualShock4) return bluetooth_ ? "DualShock 4 (Bluetooth)" : "DualShock 4 (USB)";
    if (model_ == sony::Model::DualSense) return bluetooth_ ? "DualSense (Bluetooth)" : "DualSense (USB)";
    return "none";
}

void SonyPad::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CancelIo(handle_);
        CloseHandle(handle_);
    }
    if (event_) CloseHandle(event_);
    handle_ = INVALID_HANDLE_VALUE;
    event_ = nullptr;
    reading_ = false;
    model_ = sony::Model::None;
    last_ = PadState{};
}

bool SonyPad::find() {
    const Found f = findController();
    if (f.model == sony::Model::None) return false;
    handle_ = CreateFileA(f.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    event_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    model_ = f.model;
    // Bluetooth: its reports are longer than USB's (78 against 64), or its path says so.
    bluetooth_ = f.inputLength > 64 || f.path.find("{00001124-") != std::string::npos ||
                 f.path.find("bthenum") != std::string::npos || f.path.find("BTHENUM") != std::string::npos;
    buffer_.assign(f.inputLength, 0);
    // Over Bluetooth a feature report asks it for its full input reports (the DualShock 4's 0x02, the DualSense's 0x05).
    if (bluetooth_) {
        uint8_t feature[64] = {uint8_t(model_ == sony::Model::DualShock4 ? 0x02 : 0x05)};
        HidD_GetFeature(handle_, feature, model_ == sony::Model::DualShock4 ? 37 : 41);
    }
    LOG_INFO("pad: %s found", name());
    send(0, 0);  // its light bar's colour
    return true;
}

void SonyPad::startRead() {
    if (reading_ || handle_ == INVALID_HANDLE_VALUE) return;
    ResetEvent(event_);
    overlapped_ = OVERLAPPED{};
    overlapped_.hEvent = event_;
    DWORD got = 0;
    if (ReadFile(handle_, buffer_.data(), DWORD(buffer_.size()), &got, &overlapped_) ||
        GetLastError() == ERROR_IO_PENDING) {
        reading_ = true;
        return;
    }
    LOG_INFO("pad: %s gone", name());
    close();
}

PadState SonyPad::poll() {
    if (handle_ == INVALID_HANDLE_VALUE) {
        const DWORD now = GetTickCount();
        if (scanned_ && now - lastScanMs_ < 1000) return PadState{};
        scanned_ = true;
        lastScanMs_ = now;
        if (!find()) return PadState{};
    }
    // Every report that came since the last poll, the newest kept.
    for (int k = 0; k < 16; ++k) {
        startRead();
        if (!reading_) return PadState{};
        DWORD got = 0;
        if (!GetOverlappedResult(handle_, &overlapped_, &got, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE) break;  // none more yet
            LOG_INFO("pad: %s gone", name());
            close();
            return PadState{};
        }
        reading_ = false;
        PadState p;
        if (sony::parse(model_, buffer_.data(), got, touchpad_, p)) last_ = p;
    }
    return last_;
}

void SonyPad::send(uint8_t left, uint8_t right) {
    if (handle_ == INVALID_HANDLE_VALUE) return;
    const std::vector<uint8_t> o = sony::output(model_, bluetooth_, left, right, lightbar_, sequence_++);
    if (o.empty()) return;
    OVERLAPPED w{};
    w.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    DWORD wrote = 0;
    if (!WriteFile(handle_, o.data(), DWORD(o.size()), &wrote, &w) && GetLastError() == ERROR_IO_PENDING)
        WaitForSingleObject(w.hEvent, 20);  // a report is small: done at once, or let go
    CancelIoEx(handle_, &w);
    CloseHandle(w.hEvent);
}

void SonyPad::rumble(Rumble pattern, DWORD nowMs) {
    pattern_.clear();
    switch (pattern) {  // as XInputPad's, the motors 0-255
        case Rumble::TargetSwitch: pattern_ = {{80, 0, 0x60}}; break;
        case Rumble::TargetCleared: pattern_ = {{60, 0x30, 0}}; break;
        case Rumble::PadOn: pattern_ = {{60, 0x50, 0x50}, {60, 0, 0}, {60, 0x50, 0x50}}; break;
        case Rumble::PadOff: pattern_ = {{250, 0x80, 0}}; break;
        case Rumble::Denied: pattern_ = {{40, 0, 0x30}}; break;
        case Rumble::CameraOn: pattern_ = {{40, 0x20, 0x60}, {50, 0, 0}, {40, 0x20, 0x60}}; break;
        case Rumble::CameraOff: pattern_ = {{90, 0x60, 0x20}}; break;
        case Rumble::None: break;
    }
    seg_ = 0;
    segStart_ = nowMs;
    if (!pattern_.empty()) send(pattern_[0].left, pattern_[0].right);
}

void SonyPad::update(DWORD nowMs) {
    if (pattern_.empty()) return;
    while (seg_ < pattern_.size() && nowMs - segStart_ >= pattern_[seg_].ms) {
        segStart_ += pattern_[seg_].ms;
        ++seg_;
        if (seg_ < pattern_.size()) send(pattern_[seg_].left, pattern_[seg_].right);
    }
    if (seg_ >= pattern_.size()) stopRumble();
}

void SonyPad::stopRumble() {
    if (pattern_.empty()) return;
    pattern_.clear();
    send(0, 0);
}

}  // namespace uapad
