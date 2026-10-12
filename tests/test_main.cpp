#include <cstring>

#include "test.hpp"

namespace t {
std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}
int failures = 0;
}  // namespace t

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (const t::Case& c : t::registry()) {
        if (*filter && !std::strstr(c.name, filter)) continue;
        const int before = t::failures;
        c.fn();
        ++ran;
        std::printf("%s %s\n", t::failures == before ? "PASS" : "FAIL", c.name);
    }
    std::printf("%d tests, %d failed checks\n", ran, t::failures);
    if (ran == 0) {
        std::printf("no tests match '%s'\n", filter);
        return 2;
    }
    return t::failures ? 1 : 0;
}
