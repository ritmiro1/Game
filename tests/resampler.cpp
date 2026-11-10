#include "../src/resampler.h"
#include <cassert>
#include <cmath>
#include <cstdio>
static std::vector<float> run(const std::vector<float>& in, int chunk, double speed) {
    SpeedResampler r(2); r.reset(speed);
    std::vector<float> all, out;
    for (size_t pos = 0; pos < in.size()/2;) {
        size_t n = std::min((size_t)chunk, in.size()/2-pos);
        r.process(&in[pos*2], n, speed, out);
        all.insert(all.end(), out.begin(), out.end()); pos += n;
    }
    return all;
}
int main() {
    std::vector<float> in(48000*2);
    for (int i=0;i<48000;i++) in[2*i]=in[2*i+1]=0.5f*std::sin(2*3.141592653589793*440*i/48000);
    for(double speed : {0.5, 1.0, 1.5, 2.0}) {
        auto a=run(in, 48000, speed), b=run(in, 173, speed);
        assert(a.size()==b.size());
        for(size_t i=0;i<a.size();i++) { assert(std::isfinite(a[i])); assert(std::abs(a[i]-b[i]) < 1e-6); }
        assert(std::abs((double)a.size()/2 - 48000/speed) < 40);
        for(size_t i=2;i<a.size();i+=2) assert(std::abs(a[i]-a[i-2]) < 0.13);
    }
    // At 2x, suppress tones above the new Nyquist frequency instead of aliasing.
    std::vector<float> high(48000*2);
    for (int i=0;i<48000;i++) high[2*i]=high[2*i+1]=0.5f*std::sin(2*3.141592653589793*18000*i/48000);
    auto filtered = run(high, 257, 2.0);
    double energy = 0;
    for (size_t i=200; i<filtered.size(); i+=2) energy += filtered[i]*filtered[i];
    assert(std::sqrt(energy / ((filtered.size()-200)/2)) < 0.02);

    // Exercise speed changes at packet boundaries without resetting filter history.
    SpeedResampler changing(2); changing.reset();
    std::vector<float> joined, part;
    for (size_t pos = 0; pos < in.size()/2; ) {
        size_t n = std::min((size_t)173, in.size()/2-pos);
        double speed = pos < 12000 ? 1.0 : pos < 24000 ? 2.0 : pos < 36000 ? 0.5 : 1.0;
        changing.process(&in[pos*2], n, speed, part);
        joined.insert(joined.end(), part.begin(), part.end()); pos += n;
    }
    for (size_t i=2;i<joined.size();i+=2) assert(std::abs(joined[i]-joined[i-2]) < 0.14);
    puts("resampler: packet continuity, duration, alias rejection, speed changes OK");
}
