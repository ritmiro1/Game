#pragma once
#include <algorithm>
#include <cmath>

// Preserve true gain for almost all in-range samples. Only the last 2% of
// headroom is a soft knee; excessive gain can still cause audible distortion.
inline float LimitBoostedSample(float sample) {
    const float a = std::fabs(sample);
    if (a <= 0.98f) return sample;
    const float y = 0.98f + 0.02f * std::tanh((a - 0.98f) / 0.02f);
    return std::copysign(y, sample);
}
inline float ApplyAudioGain(float sample, float gain) {
    const float v = sample * gain;
    return gain > 1.0f ? LimitBoostedSample(v) : std::clamp(v, -1.0f, 1.0f);
}
