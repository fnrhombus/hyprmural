#pragma once

#include <functional>
#include <string>

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

private:
    int event_fd_{-1};
    std::string buffer_;
};

}  // namespace hm
