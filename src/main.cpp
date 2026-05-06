#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void usage(FILE* out) {
    std::fprintf(out, "usage: hyprmural [--config <path>]\n");
}

const std::unordered_set<std::string> kWorkspaceEvents = {
    "workspace",         "workspacev2",
    "focusedmon",        "focusedmonv2",
    "moveworkspace",     "moveworkspacev2",
    "activespecial",     "activespecialv2",
    "createworkspace",   "createworkspacev2",
    "destroyworkspace",  "destroyworkspacev2",
};

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
        auto renderer = std::make_unique<hm::Renderer>();

        // Preload every unique image referenced by the config.
        std::unordered_map<std::string, std::unique_ptr<hm::Image>> images;
        std::unordered_map<std::string, std::unique_ptr<hm::Texture>> textures;
        const auto preload = [&](const std::string& path) {
            if (textures.count(path)) return;
            auto img = std::make_unique<hm::Image>(path);
            auto tex = std::make_unique<hm::Texture>(*img);
            images.emplace(path, std::move(img));
            textures.emplace(path, std::move(tex));
        };
        preload(cfg.default_image);
        for (const auto& [_, p] : cfg.per_workspace) preload(p);

        const auto resolve = [&](const std::string& workspace) -> hm::Texture* {
            const auto it = cfg.per_workspace.find(workspace);
            const std::string& path =
                (it != cfg.per_workspace.end()) ? it->second : cfg.default_image;
            return textures.at(path).get();
        };

        for (auto& s : surfaces) {
            s->set_renderer(renderer.get());
            s->set_fit(cfg.fit);
            s->set_texture(textures.at(cfg.default_image).get());
        }

        std::unordered_map<std::string, std::string> last_workspace;  // monitor -> ws name

        const auto sync_workspaces = [&]() {
            const auto map = hm::parse_monitors_active_workspace(
                hm::HyprlandIPC::request("monitors"));
            for (auto& s : surfaces) {
                const auto& mon = s->output_name();
                const auto it = map.find(mon);
                if (it == map.end()) continue;
                if (last_workspace[mon] == it->second) continue;
                last_workspace[mon] = it->second;
                s->set_texture(resolve(it->second));
                s->render();
                std::printf("[hyprmural] %s -> workspace %s\n",
                            mon.c_str(), it->second.c_str());
                std::fflush(stdout);
            }
        };

        std::printf("hyprmural — config %s; %zu image(s) preloaded; %zu surface(s); "
                    "Ctrl-C to exit\n",
                    config_path.c_str(), textures.size(), surfaces.size());
        std::fflush(stdout);

        sync_workspaces();
        wl.flush();

        const auto on_event = [&](const std::string& ev, const std::string&) {
            if (kWorkspaceEvents.count(ev)) sync_workspaces();
        };

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
