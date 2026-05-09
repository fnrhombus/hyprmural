#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "renderer.h"

namespace hm {

struct Config {
    std::string default_image;
    FitMode fit = FitMode::Cover;
    std::unordered_map<std::string, std::string> per_workspace;
    std::string hook;  // shell command run via /bin/sh -c on each per-output image change

    // When true, workspaces without an explicit per-workspace mapping pick a
    // random image from `wallpaper_globs` (POSIX glob(3)) at first sight and
    // keep it for the daemon's lifetime. Explicit per-workspace and default
    // entries still win.
    bool randomize = false;
    std::vector<std::string> wallpaper_globs;
};

// Parses the config file at `path`. Throws std::runtime_error on failure.
Config load_config(const std::string& path);

// $XDG_CONFIG_HOME/hyprmural/hyprmural.conf, falling back to
// $HOME/.config/hyprmural/hyprmural.conf.
std::string default_config_path();

}  // namespace hm
