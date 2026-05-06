#include "image.h"

#include <stdexcept>
#include <utility>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

namespace hm {

Image::Image(const std::string& path) {
    int channels = 0;
    data_ = stbi_load(path.c_str(), &width_, &height_, &channels, 4);
    if (!data_) {
        throw std::runtime_error("failed to load image '" + path + "': " + stbi_failure_reason());
    }
}

Image::~Image() {
    if (data_) stbi_image_free(data_);
}

Image::Image(Image&& other) noexcept
    : width_(other.width_), height_(other.height_), data_(other.data_) {
    other.width_ = 0;
    other.height_ = 0;
    other.data_ = nullptr;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        if (data_) stbi_image_free(data_);
        width_ = other.width_;
        height_ = other.height_;
        data_ = other.data_;
        other.width_ = 0;
        other.height_ = 0;
        other.data_ = nullptr;
    }
    return *this;
}

}  // namespace hm
