#pragma once

#include <EGL/egl.h>
#include <cstdint>
#include <string>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace hm {

class Wayland;
class Egl;
class Renderer;
class Texture;
struct Output;
enum class FitMode;

class LayerSurface {
public:
    LayerSurface(Wayland& wl, Egl& egl, Output& output);
    ~LayerSurface();
    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;

    void set_renderer(Renderer* r) { renderer_ = r; }
    void set_texture(const Texture* t) { texture_ = t; }
    void set_fit(FitMode f);

    void render();
    EGLSurface egl_surface() const { return egl_surface_; }
    bool configured() const { return configured_; }
    const std::string& output_name() const;

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

    Renderer* renderer_{};
    const Texture* texture_{};
    int fit_{};  // FitMode value, kept as int to avoid pulling renderer.h here

    int32_t width_{};
    int32_t height_{};
    bool configured_{false};
    bool closed_{false};
};

}  // namespace hm
