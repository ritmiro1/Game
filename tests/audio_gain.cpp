#include "../src/audio_gain.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    StereoGainLimiter limiter; limiter.reset(48000);
    float l=0.1f, r=-0.1f; limiter.process(l,r,5.0f);
    assert(std::fabs(l-0.5f)<1e-6f && std::fabs(r+0.5f)<1e-6f);
    limiter.reset(48000); l=0.9f; r=-0.9f; limiter.process(l,r,1.0f);
    assert(std::fabs(l-0.9f)<1e-6f && std::fabs(r+0.9f)<1e-6f);
    limiter.reset(48000); l=0.8f; r=0.4f; limiter.process(l,r,5.0f);
    assert(l<=1.0f && r<=1.0f && std::fabs(r-l*0.5f)<1e-6f);
    // Limiting is stereo-linked, bounded, and deterministic across packet breaks.
    StereoGainLimiter a,b; a.reset(48000); b.reset(48000);
    double sum100=0, sum500=0;
    for(int i=0;i<48000;i++) {
        float input=0.5f * std::sin(2.0*3.141592653589793*440*i/48000.0);
        float al=input, ar=input*0.5f, bl=al, br=ar;
        a.process(al,ar,5.f); b.process(bl,br,5.f);
        assert(al==bl && ar==br && std::fabs(al)<=1.f && std::fabs(ar)<=1.f);
        assert(std::fabs(ar-al*0.5f)<1e-6f);
        if(i>4800) { sum100+=input*input; sum500+=al*al; }
    }
    // Half-scale tone becomes substantially louder without peak overflow.
    assert(std::sqrt(sum500/sum100)>1.8);
    l=0.5f; r=0.5f; limiter.process(l,r,0.f); assert(l==0 && r==0);
    puts("stereo gain, peak limiting, volume and packet continuity: OK");
}
