#include "layer_surface.h"

#include "egl.h"
#include "renderer.h"
#include "wayland.h"

#include <GLES2/gl2.h>
#include <stdexcept>
#include <string>

namespace hm {

namespace {

const zwlr_layer_surface_v1_listener kLayerSurfaceListener = {
    .configure = LayerSurface::on_configure,
    .closed = LayerSurface::on_closed,
};

constexpr uint32_t kAnchorAll =
    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

}  // namespace

LayerSurface::LayerSurface(Wayland& wl, Egl& egl, Output& output)
    : wl_(wl), egl_(egl), output_(output) {
    surface_ = wl_compositor_create_surface(wl_.compositor());
    if (!surface_) throw std::runtime_error("wl_compositor_create_surface failed");

    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        wl_.layer_shell(), surface_, output_.proxy,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "hyprmural");
    if (!layer_surface_) throw std::runtime_error("zwlr_layer_shell_v1_get_layer_surface failed");

    zwlr_layer_surface_v1_add_listener(layer_surface_, &kLayerSurfaceListener, this);
    zwlr_layer_surface_v1_set_anchor(layer_surface_, kAnchorAll);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface_, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_size(layer_surface_, 0, 0);

    wl_surface_commit(surface_);
}

LayerSurface::~LayerSurface() {
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_) wl_egl_window_destroy(egl_window_);
    if (layer_surface_) zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_) wl_surface_destroy(surface_);
}

void LayerSurface::set_fit(FitMode f) {
    fit_ = static_cast<int>(f);
}

const std::string& LayerSurface::output_name() const {
    return output_.name;
}

void LayerSurface::render() {
    if (!configured_ || closed_) return;
    if (!renderer_ || !texture_) return;
    if (egl_surface_ == EGL_NO_SURFACE) return;

    egl_.make_current(egl_surface_);

    const int bw = width_ * output_.scale;
    const int bh = height_ * output_.scale;
    glViewport(0, 0, bw, bh);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    renderer_->draw(*texture_, bw, bh, static_cast<FitMode>(fit_));
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void LayerSurface::on_configure(void* data, zwlr_layer_surface_v1* surface,
                                uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<LayerSurface*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    self->width_ = static_cast<int32_t>(w);
    self->height_ = static_cast<int32_t>(h);

    const int buf_w = self->width_ * self->output_.scale;
    const int buf_h = self->height_ * self->output_.scale;

    if (!self->egl_window_) {
        wl_surface_set_buffer_scale(self->surface_, self->output_.scale);
        self->egl_window_ = wl_egl_window_create(self->surface_, buf_w, buf_h);
        if (!self->egl_window_) {
            throw std::runtime_error("wl_egl_window_create failed");
        }
        self->egl_surface_ = eglCreateWindowSurface(
            self->egl_.display(), self->egl_.config(),
            reinterpret_cast<EGLNativeWindowType>(self->egl_window_), nullptr);
        if (self->egl_surface_ == EGL_NO_SURFACE) {
            throw std::runtime_error("eglCreateWindowSurface failed");
        }
    } else {
        wl_egl_window_resize(self->egl_window_, buf_w, buf_h, 0, 0);
    }

    self->configured_ = true;
    self->render();
}

void LayerSurface::on_closed(void* data, zwlr_layer_surface_v1*) {
    static_cast<LayerSurface*>(data)->closed_ = true;
}

}  // namespace hm
