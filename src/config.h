#pragma once

#include <string>
#include <unordered_map>

#include "renderer.h"

namespace hm {

struct Config {
    std::string default_image;
    FitMode fit = FitMode::Cover;
    std::unordered_map<std::string, std::string> per_workspace;
    std::string hook;  // shell command run via /bin/sh -c on each per-output image change
};

// Parses the config file at `path`. Throws std::runtime_error on failure.
Config load_config(const std::string& path);

// $XDG_CONFIG_HOME/hyprmural/hyprmural.conf, falling back to
// $HOME/.config/hyprmural/hyprmural.conf.
std::string default_config_path();

}  // namespace hm
