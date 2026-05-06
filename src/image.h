#pragma once

#include <cstdint>
#include <string>

namespace hm {

// CPU-side decoded RGBA8 pixel buffer.
class Image {
public:
    explicit Image(const std::string& path);
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    int width() const { return width_; }
    int height() const { return height_; }
    const uint8_t* data() const { return data_; }

private:
    int width_{};
    int height_{};
    uint8_t* data_{};
    bool webp_{false};  // controls which deallocator to use
};

}  // namespace hm
