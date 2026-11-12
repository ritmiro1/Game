#include "../src/frame_rate_gate.h"
#include <cassert>
#include <cstdio>
int main() {
    FrameRateGate gate;
    int allowed=0;
    for(int i=0;i<600;i++) allowed+=gate.allow(100.0+i/60.0,30);
    assert(allowed>=299 && allowed<=301);
    gate=FrameRateGate{}; allowed=0;
    for(int i=0;i<240;i++) allowed+=gate.allow(100.0+i/24.0,30);
    assert(allowed==240);
    assert(gate.allow(200,30)); // recovery after long stall
    assert(!gate.allow(200.001,30));
    assert(gate.allow(200.002,0)); assert(gate.allow(200.003,0));
    assert(gate.allow(201,30)); assert(gate.allow(1,30)); // clock reset
    puts("frame presentation gate: OK");
}
