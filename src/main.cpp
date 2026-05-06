#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.h"
#include "egl.h"
#include "image.h"
#include "layer_surface.h"
#include "renderer.h"
#include "wayland.h"

namespace {

std::atomic<bool> g_running{true};
extern "C" void on_signal(int) { g_running.store(false, std::memory_order_relaxed); }

void usage(FILE* out) {
    std::fprintf(out, "usage: hyprmural [--config <path>]\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path = hm::default_config_path();
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            usage(stderr);
            return 2;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        const hm::Config cfg = hm::load_config(config_path);
        if (cfg.default_image.empty()) {
            throw std::runtime_error("config: 'default' image is required");
        }

        hm::Wayland wl;
        hm::Egl egl(wl.display());

        std::vector<std::unique_ptr<hm::LayerSurface>> surfaces;
        for (auto& output : wl.outputs()) {
            surfaces.push_back(std::make_unique<hm::LayerSurface>(wl, egl, output));
        }
        if (surfaces.empty()) {
            throw std::runtime_error("no outputs to render to");
        }

        wl.roundtrip();

        egl.make_current(surfaces.front()->egl_surface());
        hm::Image img(cfg.default_image);
        auto renderer = std::make_unique<hm::Renderer>();
        auto texture = std::make_unique<hm::Texture>(img);

        std::printf("hyprmural — config %s; default %dx%d on %zu surface(s); "
                    "%zu workspace mapping(s); Ctrl-C to exit\n",
                    config_path.c_str(), img.width(), img.height(),
                    surfaces.size(), cfg.per_workspace.size());
        std::fflush(stdout);

        for (auto& s : surfaces) {
            s->set_renderer(renderer.get());
            s->set_texture(texture.get());
            s->set_fit(cfg.fit);
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
