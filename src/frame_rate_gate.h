#pragma once
#include <algorithm>
#include <cmath>
// Gate presentation, not decoding/audio. Time is a monotonic wall clock.
class FrameRateGate {
    double next = 0, last = 0;
    int previousLimit = 0;
public:
    bool allow(double now, int fps) {
        if (!std::isfinite(now)) return false;
        if (fps <= 0) { next=0; last=now; previousLimit=0; return true; }
        fps=std::clamp(fps,1,240);
        double period=1.0/fps;
        if (fps != previousLimit || next==0 || now < last || now-next > period*2) next=now;
        previousLimit=fps; last=now;
        if (now + 0.001 < next) return false;
        next += period;
        return true;
    }
};
