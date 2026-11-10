#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>

// Rectangle edges in ten-thousandths of the visible video, right/bottom exclusive.
struct VideoEdit {
    int left = 0, top = 0, right = 10000, bottom = 10000;
    int brightness = 0, contrast = 100, saturation = 100;
    int gamma = 100, warmth = 0, hue = 0;
    bool flipX = false, flipY = false, invert = false;
};
inline void NormalizeEdit(VideoEdit& e) {
    e.left = std::clamp(e.left, 0, 9900);
    e.top = std::clamp(e.top, 0, 9900);
    e.right = std::clamp(e.right, e.left + 100, 10000);
    e.bottom = std::clamp(e.bottom, e.top + 100, 10000);
    e.brightness = std::clamp(e.brightness, -100, 100);
    e.contrast = std::clamp(e.contrast, 0, 200);
    e.saturation = std::clamp(e.saturation, 0, 200);
    e.gamma = std::clamp(e.gamma, 25, 300);
    e.warmth = std::clamp(e.warmth, -100, 100);
    e.hue = std::clamp(e.hue, -180, 180);
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
inline bool HasColorAdjustments(const VideoEdit& e) {
    return e.brightness != 0 || e.contrast != 100 || e.saturation != 100 ||
           e.gamma != 100 || e.warmth != 0 || e.hue != 0 || e.invert;
}
inline bool SameColorAdjustments(const VideoEdit& a, const VideoEdit& b) {
    return a.brightness == b.brightness && a.contrast == b.contrast && a.saturation == b.saturation &&
           a.gamma == b.gamma && a.warmth == b.warmth && a.hue == b.hue && a.invert == b.invert &&
           a.flipX == b.flipX && a.flipY == b.flipY;
}
inline void AdjustColors(uint8_t* pixels, size_t count, VideoEdit e) {
    NormalizeEdit(e);
    if (!HasColorAdjustments(e)) return;
    int lut[256];
    for (int i = 0; i < 256; ++i) {
        int v = std::clamp((i - 128) * e.contrast / 100 + 128 + e.brightness * 255 / 100, 0, 255);
        if (e.gamma != 100) v = (int)std::lround(255.0 * std::pow(v / 255.0, 100.0 / e.gamma));
        lut[i] = e.invert ? 255 - v : v;
    }
    const int warm = e.warmth * 80 / 100;
    // Most edits do not change hue or saturation. Precompute separate channel
    // lookups so the common case avoids per-pixel arithmetic and clamping.
    if (e.hue == 0 && e.saturation == 100) {
        uint8_t red[256], green[256], blue[256];
        for (int i = 0; i < 256; ++i) {
            red[i] = (uint8_t)lut[std::clamp(i + warm, 0, 255)];
            green[i] = (uint8_t)lut[i];
            blue[i] = (uint8_t)lut[std::clamp(i - warm, 0, 255)];
        }
        for (size_t i = 0; i < count; ++i) {
            uint8_t* p = pixels + i * 4;
            p[0] = blue[p[0]]; p[1] = green[p[1]]; p[2] = red[p[2]];
        }
        return;
    }
    // Hue rotation in the RGB colour cube; coefficients are calculated once per frame.
    const double angle = e.hue * 3.14159265358979323846 / 180.0;
    const double c = std::cos(angle), sn = std::sin(angle);
    const int rr = (int)std::lround((.213 + .787*c - .213*sn)*8192);
    const int rg = (int)std::lround((.715 - .715*c - .715*sn)*8192);
    const int rb = (int)std::lround((.072 - .072*c + .928*sn)*8192);
    const int gr = (int)std::lround((.213 - .213*c + .143*sn)*8192);
    const int gg = (int)std::lround((.715 + .285*c + .140*sn)*8192);
    const int gb = (int)std::lround((.072 - .072*c - .283*sn)*8192);
    const int br = (int)std::lround((.213 - .213*c - .787*sn)*8192);
    const int bg = (int)std::lround((.715 - .715*c + .715*sn)*8192);
    const int bb = (int)std::lround((.072 + .928*c + .072*sn)*8192);
    for (size_t i = 0; i < count; ++i) {
        uint8_t* p = pixels + i * 4;
        int b = p[0], g = p[1], r = p[2];
        if (e.hue != 0) {
            int nr = (rr*r + rg*g + rb*b) / 8192;
            int ng = (gr*r + gg*g + gb*b) / 8192;
            int nb = (br*r + bg*g + bb*b) / 8192;
            r = std::clamp(nr,0,255); g = std::clamp(ng,0,255); b = std::clamp(nb,0,255);
        }
        if (e.saturation != 100) {
            int gray = (r * 77 + g * 150 + b * 29 + 128) >> 8;
            b = std::clamp(gray + (b - gray) * e.saturation / 100, 0, 255);
            g = std::clamp(gray + (g - gray) * e.saturation / 100, 0, 255);
            r = std::clamp(gray + (r - gray) * e.saturation / 100, 0, 255);
        }
        r = std::clamp(r + warm, 0, 255); b = std::clamp(b - warm, 0, 255);
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
