#include "win/pads.hpp"

#include "core/config/bindings.hpp"

namespace uapad {

void Pads::setSony(bool on) {
    if (on == sonyNow_) return;
    sonyNow_ = on;
    useSonyLabels(on);  // the menu names the buttons as they are printed on it
}

}  // namespace uapad
