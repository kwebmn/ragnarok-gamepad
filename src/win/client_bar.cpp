#include "win/client_bar.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace uapad::clientbar {
namespace {

// uaRO.exe / uaRO-1.1.exe: the CSession object and the parts of it we read.
constexpr uintptr_t kSession = 0x771148;
constexpr uintptr_t kSlotNames = kSession + 0x314;   // std::string[27], 16 bytes each
constexpr uintptr_t kSlotLevels = kSession + 0x4c4;  // int[27]
constexpr uintptr_t kInventory = kSession + 0xf1c;   // std::list<ITEM_INFO>: its sentinel node
// A list node: next at +0, ITEM_INFO at +8 (m_itemIndex +0x08, m_itemName +0x2c, m_isIdentified +0x3c).
constexpr uintptr_t kNodeIndex = 8 + 0x08;
constexpr uintptr_t kNodeName = 8 + 0x2c;
constexpr uintptr_t kNodeIdentified = 8 + 0x3c;
constexpr size_t kMaxName = 64;
constexpr int kMaxItems = 512;

// The functions that use those offsets, byte for byte (CSession::GetShortcutName, GetItemInfo,
// SetShortcutLevel): another build fails this check instead of being misread.
const uint8_t kSigName[] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0xc1, 0xe0, 0x04, 0x8d, 0x84, 0x08, 0x14, 0x03, 0x00, 0x00};
const uint8_t kSigItems[] = {0x55, 0x8b, 0xec, 0x51, 0x8b, 0x81, 0x20, 0x0f, 0x00, 0x00};
const uint8_t kSigLevel[] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x0c, 0x8b, 0x55, 0x08, 0x89, 0x94, 0x81, 0xc4, 0x04, 0x00, 0x00};
// The shortcut window asking for a slot name: mov ecx, 0x771148; call GetShortcutName — the session's address.
const uint8_t kSigSession[] = {0xb9, 0x48, 0x11, 0x77, 0x00, 0xe8, 0x1e, 0x94, 0x1b, 0x00};

bool readable(uintptr_t p, size_t n) {
    if (p == 0 || n == 0 || p + n < p) return false;
    const DWORD kRead = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                        PAGE_EXECUTE_WRITECOPY;
    for (uintptr_t at = p; at < p + n;) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(at), &mbi, sizeof mbi) == 0) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & kRead))
            return false;
        at = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

template <class T>
bool load(uintptr_t p, T& out) {
    if (!readable(p, sizeof(T))) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(p), sizeof(T));
    return true;
}

// An MSVC 6 std::string: its c_str pointer at +4, its length at +8.
bool loadString(uintptr_t obj, std::string& out) {
    uint32_t ptr = 0;
    uint32_t len = 0;
    if (!load(obj + 4, ptr) || !load(obj + 8, len)) return false;
    out.clear();
    if (ptr == 0 || len == 0) return true;
    if (len > kMaxName || !readable(ptr, len)) return false;
    out.assign(reinterpret_cast<const char*>(uintptr_t(ptr)), len);
    return true;
}

bool matches(const uint8_t* sig, size_t n, uintptr_t at) {
    return readable(at, n) && std::memcmp(reinterpret_cast<const void*>(at), sig, n) == 0;
}

// The inventory index of the first identified item called `name`, as the client resolves a bar item; 0: none.
uint16_t findItem(const std::string& name) {
    uint32_t head = 0;
    uint32_t node = 0;
    if (!load(kInventory, head) || head == 0 || !load(head, node)) return 0;
    for (int i = 0; i < kMaxItems && node != 0 && node != head; ++i) {
        int32_t index = 0;
        uint8_t identified = 0;
        std::string itemName;
        if (!load(node + kNodeIndex, index) || !load(node + kNodeIdentified, identified) ||
            !loadString(node + kNodeName, itemName))
            return 0;
        if (identified && itemName == name && index > 0 && index < 0x10000) return uint16_t(index);
        if (!load(node, node)) return 0;
    }
    return 0;
}

}  // namespace

bool supported() {
    return matches(kSigName, sizeof kSigName, 0x627bb0) && matches(kSigItems, sizeof kSigItems, 0x629140) &&
           matches(kSigLevel, sizeof kSigLevel, 0x6425f0) && matches(kSigSession, sizeof kSigSession, 0x46e788);
}

bool read(std::array<ClientBarSlot, kClientBarSlots>& out) {
    for (size_t j = 0; j < out.size(); ++j) {
        std::string name;
        int32_t level = 0;
        if (!loadString(kSlotNames + 16 * j, name) || !load(kSlotLevels + 4 * j, level)) return false;
        ClientBarSlot slot;
        if (name.size() > 2 && name[0] == '#' && name[1] == '#') {  // "##AC_DOUBLE"
            slot.kind = ClientBarSlot::Kind::Skill;
            slot.name = name.substr(2);
            slot.level = level > 0 && level < 100 ? uint16_t(level) : 0;
        } else if (!name.empty() && name != "nothing") {
            slot.kind = ClientBarSlot::Kind::Item;
            slot.name = name;
            slot.itemIndex = findItem(name);
        }
        out[j] = slot;
    }
    return true;
}

}  // namespace uapad::clientbar
