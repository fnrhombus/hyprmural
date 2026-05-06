#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.h"
#include "egl.h"
#include "hyprland_ipc.h"
#include "image.h"
#include "layer_surface.h"
#include "renderer.h"
#include "wayland.h"

namespace {

std::atomic<bool> g_running{true};
extern "C" void on_signal(int) { g_running.store(false, std::memory_order_relaxed); }

void install_signal_handlers() {
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART — let blocking syscalls return EINTR
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

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

    install_signal_handlers();

    try {
        const hm::Config cfg = hm::load_config(config_path);
        if (cfg.default_image.empty()) {
            throw std::runtime_error("config: 'default' image is required");
        }

        hm::Wayland wl;
        hm::Egl egl(wl.display());
        hm::HyprlandIPC ipc;

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
                    "%zu workspace mapping(s); IPC connected; Ctrl-C to exit\n",
                    config_path.c_str(), img.width(), img.height(),
                    surfaces.size(), cfg.per_workspace.size());
        std::fflush(stdout);

        for (auto& s : surfaces) {
            s->set_renderer(renderer.get());
            s->set_texture(texture.get());
            s->set_fit(cfg.fit);
            s->render();
        }

        const auto on_event = [](const std::string& ev, const std::string& data) {
            std::printf("[ipc] %s = %s\n", ev.c_str(), data.c_str());
            std::fflush(stdout);
        };

        wl.flush();
        while (g_running.load(std::memory_order_relaxed)) {
            while (wl.prepare_read() != 0) wl.dispatch_pending();
            wl.flush();

            pollfd pfds[2] = {
                {wl.fd(), POLLIN, 0},
                {ipc.event_fd(), POLLIN, 0},
            };
            const int rc = ::poll(pfds, 2, -1);
            if (rc < 0) {
                wl.cancel_read();
                if (errno == EINTR) continue;
                throw std::runtime_error(std::string("poll: ") + std::strerror(errno));
            }

            if (pfds[0].revents & POLLIN) {
                wl.read_events();
                wl.dispatch_pending();
            } else {
                wl.cancel_read();
            }

            if (pfds[1].revents & (POLLIN | POLLHUP)) {
                if (!ipc.dispatch(on_event)) {
                    std::fprintf(stderr, "ipc: socket closed; exiting\n");
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
