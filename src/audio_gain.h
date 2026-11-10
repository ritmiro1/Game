#pragma once
#include <algorithm>
#include <cmath>

// Stereo-linked peak protection for boosted audio. Applied gain rises instantly
// on a peak and releases over time. Quiet material retains the requested gain;
// already loud material cannot be boosted to 5x within a digital PCM ceiling.
// This is a sample-peak limiter, not a lookahead / true-peak mastering limiter.
class StereoGainLimiter {
    float envelope_ = 0.0f;
    float release_ = 0.9997f;
public:
    void reset(int sampleRate) {
        envelope_ = 0.0f;
        release_ = std::exp(-1.0f / (0.08f * std::max(sampleRate, 1)));
    }
    void process(float& left, float& right, float gain) {
        left *= gain; right *= gain;
        if (!std::isfinite(left)) left = 0;
        if (!std::isfinite(right)) right = 0;
        if (gain > 1.0f) {
            float peak = std::max(std::fabs(left), std::fabs(right));
            envelope_ = std::max(peak, envelope_ * release_);
            float reduction = envelope_ > 0.98f ? 0.98f / envelope_ : 1.0f;
            left *= reduction; right *= reduction;
        } else {
            envelope_ = 0.0f;
        }
        left = std::clamp(left, -1.0f, 1.0f);
        right = std::clamp(right, -1.0f, 1.0f);
    }
};
