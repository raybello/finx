#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "io/png_export.h"
#include <vector>
#include <cstring>

bool png_export_pixels(const std::string& path, int w, int h, const unsigned char* rgba) {
    // OpenGL origin is bottom-left; PNG expects top-left — flip rows.
    std::vector<unsigned char> flipped(static_cast<size_t>(w * h * 4));
    const int stride = w * 4;
    for (int y = 0; y < h; ++y) {
        std::memcpy(flipped.data() + y * stride,
                    rgba + (h - 1 - y) * stride,
                    stride);
    }
    return stbi_write_png(path.c_str(), w, h, 4, flipped.data(), stride) != 0;
}
