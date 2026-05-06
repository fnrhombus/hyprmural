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

std::string event_socket_path() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg || !*xdg) {
        throw std::runtime_error("XDG_RUNTIME_DIR is not set");
    }
    if (!sig || !*sig) {
        throw std::runtime_error(
            "HYPRLAND_INSTANCE_SIGNATURE is not set — Hyprland must be running");
    }
    return std::string(xdg) + "/hypr/" + sig + "/.socket2.sock";
}

}  // namespace

HyprlandIPC::HyprlandIPC() {
    const std::string path = event_socket_path();

    event_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (event_fd_ < 0) {
        throw std::runtime_error(std::string("socket(): ") + std::strerror(errno));
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(event_fd_);
        throw std::runtime_error("ipc socket path too long: " + path);
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    if (::connect(event_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int e = errno;
        ::close(event_fd_);
        event_fd_ = -1;
        throw std::runtime_error("connect " + path + ": " + std::strerror(e));
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

}  // namespace hm
