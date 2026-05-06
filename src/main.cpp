#include <atomic>
#include <csignal>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

#include "egl.h"
#include "image.h"
#include "layer_surface.h"
#include "renderer.h"
#include "wayland.h"

namespace {
std::atomic<bool> g_running{true};
extern "C" void on_signal(int) { g_running.store(false, std::memory_order_relaxed); }
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: hyprmural <image-path>\n");
        return 2;
    }
    const std::string image_path = argv[1];

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        hm::Wayland wl;
        hm::Egl egl(wl.display());

        std::vector<std::unique_ptr<hm::LayerSurface>> surfaces;
        for (auto& output : wl.outputs()) {
            surfaces.push_back(std::make_unique<hm::LayerSurface>(wl, egl, output));
        }

        if (surfaces.empty()) {
            std::fprintf(stderr, "no outputs to render to\n");
            return 1;
        }

        wl.roundtrip();  // collect first configure event per surface

        egl.make_current(surfaces.front()->egl_surface());
        hm::Image img(image_path);
        auto renderer = std::make_unique<hm::Renderer>();
        auto texture = std::make_unique<hm::Texture>(img);

        std::printf("hyprmural — %dx%d image on %zu surface(s); Ctrl-C to exit\n",
                    img.width(), img.height(), surfaces.size());
        std::fflush(stdout);

        for (auto& s : surfaces) {
            s->set_renderer(renderer.get());
            s->set_texture(texture.get());
            s->set_fit(hm::FitMode::Cover);
            s->render();
        }

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
