#pragma once

#include <GLES2/gl2.h>
#include <cstdint>

namespace hm {

class Image;

enum class FitMode {
    Cover,    // fill, preserve aspect, crop overflow
    Contain,  // fit, preserve aspect, letterbox
    Stretch,  // fill, ignore aspect
    Center,   // 1:1 pixel-perfect, centered
    Tile,     // 1:1 pixel-perfect, repeated
};

class Texture {
public:
    explicit Texture(const Image& img);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    GLuint id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLuint id_{};
    int width_{};
    int height_{};
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void draw(const Texture& tex, int surface_w, int surface_h, FitMode fit);

private:
    GLuint program_{};
    GLuint vbo_{};
    GLint loc_pos_{};
    GLint loc_uv_offset_{};
    GLint loc_uv_scale_{};
    GLint loc_uv_repeat_{};
};

}  // namespace hm
