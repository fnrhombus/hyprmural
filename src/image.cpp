#include "image.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <webp/decode.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

namespace hm {

namespace {

bool is_webp(const uint8_t* data, size_t size) {
    return size >= 12 &&
           std::memcmp(data, "RIFF", 4) == 0 &&
           std::memcmp(data + 8, "WEBP", 4) == 0;
}

}  // namespace

Image::Image(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("cannot open image '" + path + "'");
    }
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> bytes(size);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("failed to read '" + path + "'");
    }

    if (is_webp(bytes.data(), size)) {
        data_ = WebPDecodeRGBA(bytes.data(), size, &width_, &height_);
        if (!data_) {
            throw std::runtime_error("WebP decode failed for '" + path + "'");
        }
        webp_ = true;
    } else {
        int channels = 0;
        data_ = stbi_load_from_memory(bytes.data(), static_cast<int>(size),
                                       &width_, &height_, &channels, 4);
        if (!data_) {
            throw std::runtime_error("failed to decode '" + path + "': " +
                                     stbi_failure_reason());
        }
    }
}

Image::~Image() {
    if (!data_) return;
    if (webp_) WebPFree(data_);
    else stbi_image_free(data_);
}

Image::Image(Image&& other) noexcept
    : width_(other.width_),
      height_(other.height_),
      data_(other.data_),
      webp_(other.webp_) {
    other.width_ = 0;
    other.height_ = 0;
    other.data_ = nullptr;
    other.webp_ = false;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        if (data_) {
            if (webp_) WebPFree(data_);
            else stbi_image_free(data_);
        }
        width_ = other.width_;
        height_ = other.height_;
        data_ = other.data_;
        webp_ = other.webp_;
        other.width_ = 0;
        other.height_ = 0;
        other.data_ = nullptr;
        other.webp_ = false;
    }
    return *this;
}

}  // namespace hm
