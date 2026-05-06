#include <atomic>
#include <csignal>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

#include "egl.h"
#include "layer_surface.h"
#include "wayland.h"

namespace {
std::atomic<bool> g_running{true};
extern "C" void on_signal(int) { g_running.store(false, std::memory_order_relaxed); }
}  // namespace

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        hm::Wayland wl;
        hm::Egl egl(wl.display());

        std::vector<std::unique_ptr<hm::LayerSurface>> surfaces;
        for (auto& output : wl.outputs()) {
            surfaces.push_back(std::make_unique<hm::LayerSurface>(wl, egl, output));
        }

        std::printf("hyprmural — %zu surface(s) running; Ctrl-C to exit\n",
                    surfaces.size());
        std::fflush(stdout);

        wl.flush();
        while (g_running.load(std::memory_order_relaxed)) {
            if (wl.dispatch() < 0) break;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
