#pragma once

#include <EGL/egl.h>
#include <wayland-client.h>

namespace hm {

class Egl {
public:
    explicit Egl(wl_display* display);
    ~Egl();
    Egl(const Egl&) = delete;
    Egl& operator=(const Egl&) = delete;

    EGLDisplay display() const { return display_; }
    EGLConfig config() const { return config_; }
    EGLContext context() const { return context_; }

    void make_current(EGLSurface surface);

private:
    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLConfig config_{};
    EGLContext context_{EGL_NO_CONTEXT};
};

}  // namespace hm
