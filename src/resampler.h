#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Streaming, windowed-sinc sample-rate conversion. The fractional source
// position and filter history survive calls, so packet boundaries are inaudible.
// This changes pitch along with speed, just like a tape transport.
class SpeedResampler {
    static constexpr int radius = 16;
    int channels;
    std::vector<float> samples;
    double position = radius, rate = 1.0;
    bool initialized = false;
    double consumed = 0.0;
public:
    explicit SpeedResampler(int ch) : channels(ch) {}
    void reset(double speed = 1.0) {
        samples.clear(); position = radius; rate = speed; initialized = false; consumed = 0.0;
    }
    double consumedFrames() const { return consumed; }
    void process(const float* input, size_t frames, double target, std::vector<float>& output) {
        output.clear();
        consumed = 0.0;
        if (!frames) return;
        if (!initialized) {
            for (int i = 0; i < radius; ++i)
                for (int c = 0; c < channels; ++c) samples.push_back(input[c]);
            initialized = true;
        }
        samples.insert(samples.end(), input, input + frames * channels);
        target = std::clamp(target, 0.5, 2.0);
        size_t count = samples.size() / channels;
        const double pi = 3.14159265358979323846;
        while (position + radius < (double)count) {
            // A gradual transition avoids a discontinuity when the control changes.
            rate += (target - rate) * 0.001;
            double cutoff = std::min(1.0, 1.0 / rate) * 0.94;
            int center = (int)position;
            double sums[2] = {}, weight = 0;
            for (int k = -radius + 1; k <= radius; ++k) {
                double x = position - (center + k);
                double window = 0.5 + 0.5 * std::cos(pi * x / radius);
                double t = pi * x * cutoff;
                double coeff = cutoff * (std::abs(t) < 1e-12 ? 1.0 : std::sin(t) / t) * window;
                weight += coeff;
                for (int c = 0; c < channels; ++c)
                    sums[c] += samples[(size_t)(center + k) * channels + c] * coeff;
            }
            for (int c = 0; c < channels; ++c) output.push_back((float)(sums[c] / weight));
            position += rate;
            consumed += rate;
        }
        size_t discard = (size_t)std::max(0, (int)position - radius);
        if (discard) {
            samples.erase(samples.begin(), samples.begin() + discard * channels);
            position -= discard;
        }
    }
};
