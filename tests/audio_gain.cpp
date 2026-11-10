#include "../src/audio_gain.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    // A quiet signal is amplified fivefold with no limiting.
    assert(std::fabs(ApplyAudioGain(0.1f,5.f) - 0.5f) < 1e-6f);
    assert(std::fabs(ApplyAudioGain(-0.1f,5.f) + 0.5f) < 1e-6f);
    assert(ApplyAudioGain(0.5f,0.f)==0.f);
    assert(std::fabs(ApplyAudioGain(0.9f,1.f)-0.9f)<1e-6f);
    assert(std::fabs(ApplyAudioGain(0.18f,5.f)-0.9f)<1e-6f);
    float prev=0;
    for(int i=0;i<=100000;i++) {
        float x=i/10000.f, y=ApplyAudioGain(x,5.f);
        assert(std::isfinite(y) && y >= prev && y <= 1.f);
        assert(std::fabs(y + ApplyAudioGain(-x,5.f))<1e-6f);
        prev=y;
    }
    // Loud source is bounded, not five times louder than 100%.
    assert(ApplyAudioGain(0.8f,5.f)<=1.f);
    puts("audio gain, limit and symmetry: OK");
}
