#include "../src/slider_math.h"
#include <cassert>
#include <cstdio>
int main() {
    for (int width : {1, 37, 200, 600}) for (int hi : {200, 300, 360}) {
        int lo = hi == 300 ? 25 : 0;
        assert(SliderValue(-1,lo,hi,width) == lo);
        assert(SliderValue(width+1,lo,hi,width) == hi);
        assert(SliderPixel(lo,lo,hi,width) == 0);
        assert(SliderPixel(hi,lo,hi,width) == width);
        int last = lo;
        for (int p = 0; p <= width; ++p) {
            int v=SliderValue(p,lo,hi,width); assert(v >= last && v >= lo && v <= hi); last=v;
        }
    }
    assert(SliderValue(20,10,10,0)==10);
    puts("slider bounds and monotonicity: OK");
}
