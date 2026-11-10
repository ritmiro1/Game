#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// Rectangle edges in ten-thousandths of the visible video, right/bottom exclusive.
struct VideoEdit {
    int left = 0, top = 0, right = 10000, bottom = 10000;
    int brightness = 0, contrast = 100, saturation = 100;
    bool flipX = false, flipY = false;
};
inline void NormalizeEdit(VideoEdit& e) {
    e.left = std::clamp(e.left, 0, 9900);
    e.top = std::clamp(e.top, 0, 9900);
    e.right = std::clamp(e.right, e.left + 100, 10000);
    e.bottom = std::clamp(e.bottom, e.top + 100, 10000);
    e.brightness = std::clamp(e.brightness, -100, 100);
    e.contrast = std::clamp(e.contrast, 0, 200);
    e.saturation = std::clamp(e.saturation, 0, 200);
}
struct VideoRect { int x, y, width, height; };
inline VideoRect CropRect(int x, int y, int w, int h, VideoEdit e) {
    NormalizeEdit(e);
    int left = (int)((int64_t)w * e.left / 10000);
    int top = (int)((int64_t)h * e.top / 10000);
    int right = std::max(left + 1, (int)((int64_t)w * e.right / 10000));
    int bottom = std::max(top + 1, (int)((int64_t)h * e.bottom / 10000));
    return {x + left, y + top, std::min(w, right) - left, std::min(h, bottom) - top};
}
// Top-down RGB32 (B,G,R,X); input may be modified in place.
inline void AdjustColors(uint8_t* pixels, size_t count, VideoEdit e) {
    NormalizeEdit(e);
    if (e.brightness == 0 && e.contrast == 100 && e.saturation == 100) return;
    int lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = std::clamp((i - 128) * e.contrast / 100 + 128 + e.brightness * 255 / 100, 0, 255);
    for (size_t i = 0; i < count; ++i) {
        uint8_t* p = pixels + i * 4;
        int b = p[0], g = p[1], r = p[2];
        if (e.saturation != 100) {
            int gray = (r * 77 + g * 150 + b * 29 + 128) >> 8;
            b = std::clamp(gray + (b - gray) * e.saturation / 100, 0, 255);
            g = std::clamp(gray + (g - gray) * e.saturation / 100, 0, 255);
            r = std::clamp(gray + (r - gray) * e.saturation / 100, 0, 255);
        }
        p[0] = (uint8_t)lut[b]; p[1] = (uint8_t)lut[g]; p[2] = (uint8_t)lut[r];
    }
}

// Mirror RGB32 in place, keeping the selected crop in the mirrored coordinate space.
inline void FlipImage(uint8_t* pixels, int width, int height, bool flipX, bool flipY) {
    if (flipX) for (int y = 0; y < height; ++y) for (int x = 0; x < width / 2; ++x) {
        uint8_t* a = pixels + ((size_t)y * width + x) * 4;
        uint8_t* b = pixels + ((size_t)y * width + (width - 1 - x)) * 4;
        for (int c = 0; c < 4; ++c) std::swap(a[c], b[c]);
    }
    if (flipY) for (int y = 0; y < height / 2; ++y) {
        uint8_t* a = pixels + (size_t)y * width * 4;
        uint8_t* b = pixels + (size_t)(height - 1 - y) * width * 4;
        for (size_t x = 0; x < (size_t)width * 4; ++x) std::swap(a[x], b[x]);
    }
}
