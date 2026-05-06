#pragma once

#include <EGL/egl.h>
#include <cstdint>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace hm {

class Wayland;
class Egl;
struct Output;

class LayerSurface {
public:
    LayerSurface(Wayland& wl, Egl& egl, Output& output);
    ~LayerSurface();
    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;

    void render();

    static void on_configure(void* data, zwlr_layer_surface_v1* surface,
                             uint32_t serial, uint32_t w, uint32_t h);
    static void on_closed(void* data, zwlr_layer_surface_v1* surface);

private:
    Wayland& wl_;
    Egl& egl_;
    Output& output_;

    wl_surface* surface_{};
    zwlr_layer_surface_v1* layer_surface_{};
    wl_egl_window* egl_window_{};
    EGLSurface egl_surface_{EGL_NO_SURFACE};

    int32_t width_{};
    int32_t height_{};
    bool configured_{false};
    bool closed_{false};
};

}  // namespace hm
