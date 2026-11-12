#pragma once
#include <cstdint>
#include <cstddef>

// Interval restarts whenever playback is disabled, paused, or being edited.
class RotationClock {
    uint64_t deadline = 0;
public:
    void reset() { deadline = 0; }
    bool tick(uint64_t now, bool enabled, bool blocked, uint64_t interval) {
        if (!enabled || blocked || interval == 0) { deadline=0; return false; }
        if (!deadline) { deadline = now + interval; return false; }
        if (now < deadline) return false;
        deadline = now + interval; return true;
    }
};
template<class Predicate>
int NextPlayable(size_t count, int current, Predicate playable) {
    if (count < 2 || current < 0 || (size_t)current >= count) return -1;
    for (size_t n=1; n<count; ++n) {
        size_t i=((size_t)current+n)%count;
        if (playable(i)) return (int)i;
    }
    return -1;
}
