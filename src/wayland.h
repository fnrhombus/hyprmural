#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <list>
#include <string>

struct zwlr_layer_shell_v1;

namespace hm {

struct Output {
    wl_output* proxy{};
    uint32_t id{};
    std::string name;
    std::string description;
    int32_t width{};
    int32_t height{};
    int32_t scale = 1;
};

class Wayland {
public:
    Wayland();
    ~Wayland();
    Wayland(const Wayland&) = delete;
    Wayland& operator=(const Wayland&) = delete;

    void roundtrip();
    int dispatch();
    void flush();
    wl_display* display() const { return display_; }
    wl_compositor* compositor() const { return compositor_; }
    ::zwlr_layer_shell_v1* layer_shell() const { return layer_shell_; }
    const std::list<Output>& outputs() const { return outputs_; }
    std::list<Output>& outputs() { return outputs_; }

    static void on_global(void* data, wl_registry* reg, uint32_t id, const char* iface, uint32_t version);
    static void on_global_remove(void* data, wl_registry*, uint32_t id);
    static void on_output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t);
    static void on_output_mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t);
    static void on_output_done(void*, wl_output*);
    static void on_output_scale(void*, wl_output*, int32_t);
    static void on_output_name(void*, wl_output*, const char*);
    static void on_output_description(void*, wl_output*, const char*);

private:
    wl_display* display_{};
    wl_registry* registry_{};
    wl_compositor* compositor_{};
    ::zwlr_layer_shell_v1* layer_shell_{};
    std::list<Output> outputs_;
};

}  // namespace hm
