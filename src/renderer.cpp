#include "renderer.h"

#include "image.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace hm {

namespace {

constexpr const char* kVertexShader = R"(
attribute vec2 a_pos;
varying vec2 v_uv;
uniform vec2 u_uv_offset;
uniform vec2 u_uv_scale;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    vec2 uv = (a_pos * 0.5 + 0.5);
    uv.y = 1.0 - uv.y;
    v_uv = u_uv_offset + uv * u_uv_scale;
}
)";

constexpr const char* kFragmentShader = R"(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_uv_repeat;
void main() {
    vec2 uv = mix(v_uv, fract(v_uv), u_uv_repeat);
    gl_FragColor = texture2D(u_tex, uv);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        glDeleteShader(sh);
        throw std::runtime_error(std::string("shader compile failed: ") + log.data());
    }
    return sh;
}

GLuint link_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        glDeleteProgram(p);
        glDeleteShader(vs);
        glDeleteShader(fs);
        throw std::runtime_error(std::string("program link failed: ") + log.data());
    }
    glDetachShader(p, vs);
    glDetachShader(p, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

}  // namespace

Texture::Texture(const Image& img) : width_(img.width()), height_(img.height()) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

Texture::~Texture() {
    if (id_) glDeleteTextures(1, &id_);
}

Renderer::Renderer() {
    program_ = link_program(kVertexShader, kFragmentShader);
    loc_pos_ = glGetAttribLocation(program_, "a_pos");
    loc_uv_offset_ = glGetUniformLocation(program_, "u_uv_offset");
    loc_uv_scale_ = glGetUniformLocation(program_, "u_uv_scale");
    loc_uv_repeat_ = glGetUniformLocation(program_, "u_uv_repeat");

    const float verts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
}

Renderer::~Renderer() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
}

void Renderer::draw(const Texture& tex, int sw, int sh, FitMode fit) {
    const float ai = static_cast<float>(tex.width()) / tex.height();
    const float as = static_cast<float>(sw) / sh;

    float u_off_x = 0.0f, u_off_y = 0.0f;
    float u_scale_x = 1.0f, u_scale_y = 1.0f;
    float repeat = 0.0f;

    switch (fit) {
        case FitMode::Stretch:
            break;
        case FitMode::Cover:
            if (ai > as) {
                u_scale_x = as / ai;
                u_off_x = (1.0f - u_scale_x) * 0.5f;
            } else {
                u_scale_y = ai / as;
                u_off_y = (1.0f - u_scale_y) * 0.5f;
            }
            break;
        case FitMode::Contain:
            // For contain we need to letterbox — solid background outside the
            // image area. Implemented by sampling outside [0,1] and relying on
            // the shader's clamp via fract+repeat? No — contain wants borders.
            // Punt: caller should glClear first; here we use UVs that exceed
            // [0,1] so wrap mode shows a tile. Cleaner contain comes with the
            // border-color path in a follow-up.
            if (ai > as) {
                u_scale_y = as / ai;
                u_off_y = (1.0f - u_scale_y) * 0.5f;
            } else {
                u_scale_x = ai / as;
                u_off_x = (1.0f - u_scale_x) * 0.5f;
            }
            break;
        case FitMode::Center:
            u_scale_x = static_cast<float>(sw) / tex.width();
            u_scale_y = static_cast<float>(sh) / tex.height();
            u_off_x = (1.0f - u_scale_x) * 0.5f;
            u_off_y = (1.0f - u_scale_y) * 0.5f;
            break;
        case FitMode::Tile:
            u_scale_x = static_cast<float>(sw) / tex.width();
            u_scale_y = static_cast<float>(sh) / tex.height();
            repeat = 1.0f;
            break;
    }

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex.id());
    glUniform2f(loc_uv_offset_, u_off_x, u_off_y);
    glUniform2f(loc_uv_scale_, u_scale_x, u_scale_y);
    glUniform1f(loc_uv_repeat_, repeat);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(loc_pos_);
    glVertexAttribPointer(loc_pos_, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(loc_pos_);
}

}  // namespace hm
