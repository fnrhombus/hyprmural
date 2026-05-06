#include "egl.h"

#include <stdexcept>

namespace hm {

Egl::Egl(wl_display* wl) {
    display_ = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(wl));
    if (display_ == EGL_NO_DISPLAY) {
        throw std::runtime_error("eglGetDisplay failed");
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(display_, &major, &minor)) {
        throw std::runtime_error("eglInitialize failed");
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        throw std::runtime_error("eglBindAPI(GLES) failed");
    }

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };

    EGLint num_configs = 0;
    if (!eglChooseConfig(display_, config_attribs, &config_, 1, &num_configs) || num_configs == 0) {
        throw std::runtime_error("eglChooseConfig found no configs");
    }

    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, context_attribs);
    if (context_ == EGL_NO_CONTEXT) {
        throw std::runtime_error("eglCreateContext failed");
    }
}

Egl::~Egl() {
    if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglTerminate(display_);
    }
}

void Egl::make_current(EGLSurface surface) {
    if (!eglMakeCurrent(display_, surface, surface, context_)) {
        throw std::runtime_error("eglMakeCurrent failed");
    }
}

}  // namespace hm
