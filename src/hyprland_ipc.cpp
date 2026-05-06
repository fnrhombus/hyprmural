#include "hyprland_ipc.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace hm {

namespace {

std::string socket_dir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg || !*xdg) {
        throw std::runtime_error("XDG_RUNTIME_DIR is not set");
    }
    if (!sig || !*sig) {
        throw std::runtime_error(
            "HYPRLAND_INSTANCE_SIGNATURE is not set — Hyprland must be running");
    }
    return std::string(xdg) + "/hypr/" + sig;
}

std::string event_socket_path() { return socket_dir() + "/.socket2.sock"; }
std::string request_socket_path() { return socket_dir() + "/.socket.sock"; }

void connect_unix(int fd, const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        throw std::runtime_error("ipc socket path too long: " + path);
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("connect " + path + ": " + std::strerror(errno));
    }
}

}  // namespace

HyprlandIPC::HyprlandIPC() {
    event_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (event_fd_ < 0) {
        throw std::runtime_error(std::string("socket(): ") + std::strerror(errno));
    }

    try {
        connect_unix(event_fd_, event_socket_path());
    } catch (...) {
        ::close(event_fd_);
        event_fd_ = -1;
        throw;
    }

    const int flags = ::fcntl(event_fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(event_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        const int e = errno;
        ::close(event_fd_);
        event_fd_ = -1;
        throw std::runtime_error(std::string("fcntl O_NONBLOCK: ") + std::strerror(e));
    }
}

HyprlandIPC::~HyprlandIPC() {
    if (event_fd_ >= 0) ::close(event_fd_);
}

bool HyprlandIPC::dispatch(const EventHandler& handler) {
    char chunk[4096];
    for (;;) {
        const ssize_t n = ::read(event_fd_, chunk, sizeof(chunk));
        if (n > 0) {
            buffer_.append(chunk, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) return false;  // socket closed
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        return false;
    }

    size_t pos = 0;
    while (true) {
        const auto nl = buffer_.find('\n', pos);
        if (nl == std::string::npos) break;
        const std::string line = buffer_.substr(pos, nl - pos);
        pos = nl + 1;

        const auto sep = line.find(">>");
        if (sep == std::string::npos) continue;
        handler(line.substr(0, sep), line.substr(sep + 2));
    }
    if (pos > 0) buffer_.erase(0, pos);
    return true;
}

std::string HyprlandIPC::request(const std::string& cmd) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("socket(): ") + std::strerror(errno));
    }

    try {
        connect_unix(fd, request_socket_path());
    } catch (...) {
        ::close(fd);
        throw;
    }

    size_t written = 0;
    while (written < cmd.size()) {
        const ssize_t n = ::write(fd, cmd.data() + written, cmd.size() - written);
        if (n > 0) {
            written += static_cast<size_t>(n);
        } else if (errno == EINTR) {
            continue;
        } else {
            ::close(fd);
            throw std::runtime_error(std::string("ipc write: ") + std::strerror(errno));
        }
    }

    std::string out;
    char buf[4096];
    while (true) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            ::close(fd);
            throw std::runtime_error(std::string("ipc read: ") + std::strerror(errno));
        }
    }

    ::close(fd);
    return out;
}

std::unordered_map<std::string, std::string>
parse_monitors_active_workspace(const std::string& text) {
    std::unordered_map<std::string, std::string> result;
    std::string current;

    size_t pos = 0;
    while (pos < text.size()) {
        const auto nl = text.find('\n', pos);
        const std::string line =
            text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = (nl == std::string::npos ? text.size() : nl + 1);

        if (line.starts_with("Monitor ")) {
            const auto end = line.find(' ', 8);
            current = (end == std::string::npos) ? line.substr(8) : line.substr(8, end - 8);
        } else if (!current.empty()) {
            const auto p = line.find("active workspace:");
            if (p == std::string::npos) continue;
            const auto open = line.find('(', p);
            const auto close = line.find(')', open == std::string::npos ? 0 : open);
            if (open == std::string::npos || close == std::string::npos) continue;
            result[current] = line.substr(open + 1, close - open - 1);
            current.clear();
        }
    }
    return result;
}

}  // namespace hm
