#include "../src/filter_pool.h"
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
    std::vector<uint8_t> input(100003 * 4);
    uint32_t seed=7;
    for (auto& v : input) { seed=seed*1664525u+1013904223u; v=(uint8_t)(seed>>24); }
    FilterPool pool;
    for (int i=0;i<30;i++) {
        auto expected=input, actual=input;
        VideoEdit e; e.hue=(i*19)%361-180; e.saturation=(i*13)%201;
        e.gamma=25+(i*31)%276; e.brightness=(i*17)%201-100; e.warmth=(i*11)%201-100;
        e.contrast=(i*43)%201; e.invert=(i%2)!=0;
        AdjustColors(expected.data(),expected.size()/4,e);
        pool.apply(actual.data(),actual.size()/4,e);
        assert(actual==expected);
    }
    auto small=input; VideoEdit e; e.hue=30; e.saturation=80;
    auto ref=small; AdjustColors(ref.data(),123,e); pool.apply(small.data(),123,e); assert(small==ref);
    puts("parallel filter output matches serial: OK");
}
