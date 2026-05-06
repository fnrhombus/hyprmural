#include <cstdio>
#include <stdexcept>

#include "wayland.h"

int main() {
    try {
        hm::Wayland wl;
        std::printf("hyprmural — %zu output(s)\n", wl.outputs().size());
        for (const auto& o : wl.outputs()) {
            std::printf("  %s  %dx%d  scale=%d  (%s)\n",
                        o.name.c_str(), o.width, o.height, o.scale,
                        o.description.c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
