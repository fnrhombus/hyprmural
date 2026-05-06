#pragma once

#include <functional>
#include <string>
#include <unordered_map>

namespace hm {

class HyprlandIPC {
public:
    using EventHandler = std::function<void(const std::string& event, const std::string& data)>;

    HyprlandIPC();
    ~HyprlandIPC();
    HyprlandIPC(const HyprlandIPC&) = delete;
    HyprlandIPC& operator=(const HyprlandIPC&) = delete;

    int event_fd() const { return event_fd_; }

    // Drain available bytes from the event socket, parse complete `event>>data`
    // lines, and invoke `handler` for each. Returns false if the socket has
    // been closed (compositor exited).
    bool dispatch(const EventHandler& handler);

    // Synchronous request to .socket.sock. One command per connection — open,
    // write, read until EOF, close.
    static std::string request(const std::string& cmd);

private:
    int event_fd_{-1};
    std::string buffer_;
};

// Parse the plain-text response of `monitors` into monitor_name ->
// active workspace name. Skips monitors with no parseable active workspace
// line.
std::unordered_map<std::string, std::string>
parse_monitors_active_workspace(const std::string& monitors_response);

}  // namespace hm
