#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

// Streaming band-limited resampling. Filter history and fractional phase are
// kept across decoder packets. Pitch follows playback speed (not time stretch).
class SpeedResampler {
    static constexpr int radius = 16, taps = 2 * radius;
    static constexpr int phases = 256, cutoffs = 16;
    struct Tables {
        std::array<float, (cutoffs + 1) * (phases + 1) * taps> data{};
        Tables() {
            constexpr double pi = 3.14159265358979323846;
            for (int ci = 0; ci <= cutoffs; ++ci) {
                // Cutoff is indexed by speed from 1x to 2x.
                double cutoff = 0.94 / (1.0 + (double)ci / cutoffs);
                for (int ph = 0; ph <= phases; ++ph) {
                    double fraction = (double)ph / phases, weights[taps], sum = 0;
                    for (int j = 0; j < taps; ++j) {
                        double x = fraction - (j - radius + 1);
                        double window = 0.5 + 0.5 * std::cos(pi * x / radius);
                        double t = pi * x * cutoff;
                        weights[j] = cutoff * (std::abs(t) < 1e-12 ? 1.0 : std::sin(t) / t) * window;
                        sum += weights[j];
                    }
                    for (int j = 0; j < taps; ++j)
                        data[((ci * (phases + 1) + ph) * taps) + j] = (float)(weights[j] / sum);
                }
            }
        }
        const float* at(int ci, int ph) const { return &data[(ci * (phases + 1) + ph) * taps]; }
    };
    static const Tables& tables() { static const Tables table; return table; }
    int channels;
    std::vector<float> samples;
    double position = radius, rate = 1.0, consumed = 0.0;
    bool initialized = false;
public:
    explicit SpeedResampler(int ch) : channels(ch) {}
    void reset(double speed = 1.0) {
        samples.clear(); position = radius; rate = std::clamp(speed, 0.15, 2.0);
        initialized = false; consumed = 0.0;
    }
    void warm() { (void)tables(); }
    double consumedFrames() const { return consumed; }
    void process(const float* input, size_t frames, double target, std::vector<float>& output) {
        output.clear(); consumed = 0.0;
        if (!frames) return;
        const Tables& table = tables(); // expensive trig is done once, never per output sample
        if (!initialized) {
            for (int i = 0; i < radius; ++i)
                for (int c = 0; c < channels; ++c) samples.push_back(input[c]);
            initialized = true;
        }
        samples.insert(samples.end(), input, input + frames * channels);
        target = std::clamp(target, 0.15, 2.0);
        size_t count = samples.size() / channels;
        output.reserve((size_t)((frames + 2) / std::min(rate, target) + 64) * channels);
        while (position + radius < (double)count) {
            rate += (target - rate) * 0.001;
            int center = (int)position;
            double phase = (position - center) * phases;
            int ph = std::min(phases - 1, (int)phase);
            float pf = (float)(phase - ph);
            double cutoffIndex = (std::max(1.0, rate) - 1.0) * cutoffs;
            int ci = std::min(cutoffs - 1, (int)cutoffIndex);
            float cf = (float)(cutoffIndex - ci);
            const float *a = table.at(ci, ph), *b = table.at(ci, ph+1),
                        *c = table.at(ci+1, ph), *d = table.at(ci+1, ph+1);
            float sums[2] = {};
            const float* src = &samples[(size_t)(center - radius + 1) * channels];
            for (int j = 0; j < taps; ++j) {
                float w0 = a[j] + (b[j] - a[j]) * pf;
                float w1 = c[j] + (d[j] - c[j]) * pf;
                float w = w0 + (w1 - w0) * cf;
                for (int ch = 0; ch < channels; ++ch) sums[ch] += src[j * channels + ch] * w;
            }
            for (int ch = 0; ch < channels; ++ch) output.push_back(sums[ch]);
            position += rate; consumed += rate;
        }
        size_t discard = (size_t)std::max(0, (int)position - radius);
        if (discard) {
            samples.erase(samples.begin(), samples.begin() + discard * channels);
            position -= discard;
        }
    }
};
