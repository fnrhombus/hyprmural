#include "config.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace hm {

namespace {

std::string trim(std::string s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string expand_home(std::string s) {
    if (!s.empty() && s.front() == '~') {
        if (const char* home = std::getenv("HOME")) {
            return std::string(home) + s.substr(1);
        }
    }
    return s;
}

FitMode parse_fit(const std::string& s, int line_no) {
    if (s == "cover") return FitMode::Cover;
    if (s == "contain") return FitMode::Contain;
    if (s == "stretch") return FitMode::Stretch;
    if (s == "center") return FitMode::Center;
    if (s == "tile") return FitMode::Tile;
    throw std::runtime_error("config:" + std::to_string(line_no) +
                             ": unknown fit mode '" + s + "'");
}

}  // namespace

Config load_config(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open config: " + path);

    Config cfg;
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        line = trim(line);
        if (line.empty()) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("config:" + std::to_string(line_no) +
                                     ": expected 'key = value'");
        }
        const auto key = trim(line.substr(0, eq));
        const auto val = trim(line.substr(eq + 1));

        if (key == "fit") {
            cfg.fit = parse_fit(val, line_no);
        } else if (key == "default") {
            cfg.default_image = expand_home(val);
        } else if (key == "hook") {
            cfg.hook = expand_home(val);
        } else if (key == "randomize") {
            if (val == "true" || val == "yes" || val == "1") {
                cfg.randomize = true;
            } else if (val == "false" || val == "no" || val == "0") {
                cfg.randomize = false;
            } else {
                throw std::runtime_error("config:" + std::to_string(line_no) +
                                         ": randomize expects true/false");
            }
        } else if (key == "wallpaper_dir") {
            if (val.empty()) {
                throw std::runtime_error("config:" + std::to_string(line_no) +
                                         ": wallpaper_dir is empty");
            }
            cfg.wallpaper_globs.push_back(expand_home(val));
        } else if (key == "workspace") {
            const auto comma = val.find(',');
            if (comma == std::string::npos) {
                throw std::runtime_error("config:" + std::to_string(line_no) +
                                         ": expected 'workspace = <id>, <path>'");
            }
            const auto ws = trim(val.substr(0, comma));
            const auto p = expand_home(trim(val.substr(comma + 1)));
            if (ws.empty() || p.empty()) {
                throw std::runtime_error("config:" + std::to_string(line_no) +
                                         ": empty workspace id or path");
            }
            cfg.per_workspace[ws] = p;
        } else {
            throw std::runtime_error("config:" + std::to_string(line_no) +
                                     ": unknown key '" + key + "'");
        }
    }
    return cfg;
}

std::string default_config_path() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::string(xdg) + "/hyprmural/hyprmural.conf";
    }
    if (const char* home = std::getenv("HOME"); home) {
        return std::string(home) + "/.config/hyprmural/hyprmural.conf";
    }
    return "hyprmural.conf";
}

}  // namespace hm
