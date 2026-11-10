#pragma once
#include <algorithm>
#include <cmath>

struct AudioRange { int first, last; };
// Intersect decoded PCM with [start, end). A negative end means no end limit.
// Tiny tolerance prevents binary timestamp rounding from dropping an exact sample.
inline AudioRange TrimAudioRange(int frames, int rate, double pts, double start, double end) {
    auto index = [&](double t) {
        return (int)std::clamp(std::ceil((t - pts) * rate - 1e-7), 0.0, (double)frames);
    };
    int first = index(start);
    int last = end < 0 ? frames : index(end);
    return {first, std::max(first, last)};
}
