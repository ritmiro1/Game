#pragma once
#include <algorithm>
#include <cstdint>
inline int SliderPixel(int value, int lo, int hi, int width) {
    if (hi <= lo || width <= 0) return 0;
    value = std::clamp(value, lo, hi);
    return (int)(((int64_t)(value - lo) * width + (hi - lo) / 2) / (hi - lo));
}
inline int SliderValue(int pixel, int lo, int hi, int width) {
    if (hi <= lo || width <= 0) return lo;
    pixel = std::clamp(pixel, 0, width);
    return lo + (int)(((int64_t)pixel * (hi - lo) + width / 2) / width);
}
