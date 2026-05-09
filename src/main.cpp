#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <glob.h>
#include <memory>
#include <numeric>
#include <poll.h>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config.h"
#include "egl.h"
#include "hooks.h"
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

// Block SIGUSR1 so the only delivery path is via the signalfd we add to
// the main poll loop. Returns the signalfd, or -1 on failure (in which
// case SIGUSR1 stays at default disposition, which would terminate us —
// we set it to SIG_IGN on failure so the daemon survives at least).
int install_reshuffle_signalfd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
        std::signal(SIGUSR1, SIG_IGN);
        return -1;
    }
    int fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (fd < 0) {
        std::signal(SIGUSR1, SIG_IGN);
        return -1;
    }
    return fd;
}

void usage(FILE* out) {
    std::fprintf(out, "usage: hyprmural [--config <path>]\n");
}

// Recognised image suffixes for the random pool. We can't tell from a glob
// whether a match is a real wallpaper, so we filter before opening anything
// that would noisily fail to decode (e.g. a stray .txt in the folder).
bool has_image_suffix(const std::string& path) {
    static constexpr const char* kSuffixes[] = {
        ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tga", ".gif",
    };
    auto endswith_ci = [&](const char* suf) {
        const auto n = std::strlen(suf);
        if (path.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            const char a = path[path.size() - n + i];
            const char b = suf[i];
            const char la = (a >= 'A' && a <= 'Z') ? char(a + 32) : a;
            if (la != b) return false;
        }
        return true;
    };
    for (const char* s : kSuffixes) {
        if (endswith_ci(s)) return true;
    }
    return false;
}

bool is_regular_file(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Atomically write the per-workspace assignment map to
// $XDG_RUNTIME_DIR/hyprmural/assignments.json so consumers (e.g.
// pill-accents.py) can render full state at session start without
// waiting for the user to visit each workspace.
void write_assignments_file(
    const std::unordered_map<std::string, std::string>& explicit_per_workspace,
    const std::unordered_map<std::string, std::string>& randomized) {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (!xdg || !*xdg) return;
    const std::string dir = std::string(xdg) + "/hyprmural";
    ::mkdir(dir.c_str(), 0700);  // benign if it exists
    const std::string final_path = dir + "/assignments.json";
    const std::string tmp_path = final_path + ".tmp";

    std::ofstream f(tmp_path, std::ios::trunc);
    if (!f) return;
    f << "{";
    bool first = true;
    const auto emit = [&](const std::string& ws, const std::string& image) {
        if (!first) f << ",";
        first = false;
        f << "\n  \"" << json_escape(ws) << "\": \"" << json_escape(image) << "\"";
    };
    for (const auto& [ws, img] : explicit_per_workspace) emit(ws, img);
    for (const auto& [ws, img] : randomized) {
        if (explicit_per_workspace.count(ws)) continue;
        emit(ws, img);
    }
    f << "\n}\n";
    f.close();
    ::rename(tmp_path.c_str(), final_path.c_str());
}

std::vector<std::string> expand_pool(const std::vector<std::string>& globs) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> out;
    for (const auto& g : globs) {
        glob_t gl {};
        const int rc = ::glob(g.c_str(), GLOB_TILDE | GLOB_NOSORT, nullptr, &gl);
        if (rc == GLOB_NOMATCH) {
            ::globfree(&gl);
            continue;
        }
        if (rc != 0) {
            ::globfree(&gl);
            throw std::runtime_error("glob failed for '" + g + "'");
        }
        for (size_t i = 0; i < gl.gl_pathc; ++i) {
            std::string p = gl.gl_pathv[i];
            if (!has_image_suffix(p) || !is_regular_file(p)) continue;
            if (seen.insert(p).second) out.push_back(std::move(p));
        }
        ::globfree(&gl);
    }
    std::sort(out.begin(), out.end());  // stable order before shuffle
    return out;
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
    hm::install_hook_reaper();

    try {
        hm::Config cfg = hm::load_config(config_path);

        std::vector<std::string> pool;
        std::mt19937 rng;
        if (cfg.randomize) {
            if (cfg.wallpaper_globs.empty()) {
                throw std::runtime_error(
                    "config: randomize=true requires at least one wallpaper_dir");
            }
            pool = expand_pool(cfg.wallpaper_globs);
            if (pool.empty()) {
                throw std::runtime_error(
                    "config: randomize=true but wallpaper_dir glob(s) matched no images");
            }
            std::random_device rd;
            std::seed_seq seed{rd(), rd(), rd(), rd()};
            rng.seed(seed);
            if (cfg.default_image.empty()) {
                std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
                cfg.default_image = pool[dist(rng)];
            }
        }

        if (cfg.default_image.empty()) {
            throw std::runtime_error(
                "config: 'default' image is required (or set randomize=true with wallpaper_dir)");
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
        for (const auto& p : pool) preload(p);

        // Stable random assignment per workspace for the daemon's lifetime.
        // Cache outside the lambda so the same workspace keeps its pick.
        std::unordered_map<std::string, std::string> randomized;
        const auto pick_path = [&](const std::string& workspace) -> const std::string& {
            const auto it = cfg.per_workspace.find(workspace);
            if (it != cfg.per_workspace.end()) return it->second;
            if (cfg.randomize && !pool.empty()) {
                auto cached = randomized.find(workspace);
                if (cached != randomized.end()) return cached->second;
                std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
                return randomized.emplace(workspace, pool[dist(rng)]).first->second;
            }
            return cfg.default_image;
        };

        for (auto& s : surfaces) {
            s->set_renderer(renderer.get());
            s->set_fit(cfg.fit);
            s->set_texture(textures.at(cfg.default_image).get());
        }

        // Pre-pick numeric workspaces 1..9 from a shuffled pool so each
        // gets a distinct image (when the pool is at least that big), and
        // so consumers can render full per-workspace state at session
        // start without waiting for each to be visited. Lazy picks for
        // any other workspace fall through to uniform-random in
        // pick_path. Explicit per_workspace pins still win.
        const auto reshuffle = [&]() {
            if (!cfg.randomize) return;
            randomized.clear();
            std::vector<size_t> indices(pool.size());
            std::iota(indices.begin(), indices.end(), 0);
            std::shuffle(indices.begin(), indices.end(), rng);
            size_t taken = 0;
            for (int i = 1; i <= 9; ++i) {
                const std::string ws = std::to_string(i);
                if (cfg.per_workspace.count(ws)) continue;
                randomized.emplace(ws, pool[indices[taken % pool.size()]]);
                ++taken;
            }
        };
        reshuffle();
        write_assignments_file(cfg.per_workspace, randomized);

        std::unordered_map<std::string, std::string> last_image;  // monitor -> image path

        const auto sync_workspaces = [&]() {
            const auto map = hm::parse_monitors_active_workspace(
                hm::HyprlandIPC::request("monitors"));
            for (auto& s : surfaces) {
                const auto& mon = s->output_name();
                const auto it = map.find(mon);
                if (it == map.end()) continue;
                const std::string& workspace = it->second;
                const std::string& image = pick_path(workspace);
                if (last_image[mon] == image) continue;
                last_image[mon] = image;
                s->set_texture(textures.at(image).get());
                s->render();
                std::printf("[hyprmural] %s -> ws %s -> %s\n",
                            mon.c_str(), workspace.c_str(), image.c_str());
                std::fflush(stdout);
                // pick_path may have just added a new lazy pick — keep the
                // assignments file in sync before consumers read it.
                write_assignments_file(cfg.per_workspace, randomized);
                hm::fire_hook(cfg.hook, mon, workspace, image);
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

        // SIGUSR1 → re-shuffle in place. Explicit per-workspace pins are
        // untouched (reshuffle() only refills the `randomized` map).
        const int sigfd = install_reshuffle_signalfd();
        const auto on_reshuffle = [&]() {
            reshuffle();
            // Force the next sync_workspaces to repaint visible workspaces
            // by clearing their last_image entries.
            last_image.clear();
            write_assignments_file(cfg.per_workspace, randomized);
            std::printf("[hyprmural] reshuffled\n");
            std::fflush(stdout);
            sync_workspaces();
            wl.flush();
        };

        while (g_running.load(std::memory_order_relaxed)) {
            while (wl.prepare_read() != 0) wl.dispatch_pending();
            wl.flush();

            pollfd pfds[3] = {
                {wl.fd(), POLLIN, 0},
                {ipc.event_fd(), POLLIN, 0},
                {sigfd, static_cast<short>(sigfd >= 0 ? POLLIN : 0), 0},
            };
            const int nfds = sigfd >= 0 ? 3 : 2;
            const int rc = ::poll(pfds, nfds, -1);
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

            if (sigfd >= 0 && (pfds[2].revents & POLLIN)) {
                signalfd_siginfo si;
                while (::read(sigfd, &si, sizeof(si)) == sizeof(si)) {
                    if (si.ssi_signo == SIGUSR1) on_reshuffle();
                }
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
