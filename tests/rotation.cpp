#include "../src/rotation.h"
#include <cassert>
#include <cstdio>
int main() {
    RotationClock c;
    assert(!c.tick(100,true,false,900)); assert(!c.tick(999,true,false,900));
    assert(c.tick(1000,true,false,900)); assert(!c.tick(1001,true,false,900));
    assert(!c.tick(1900,true,true,900)); assert(!c.tick(4000,true,false,900));
    assert(c.tick(4900,true,false,900));
    assert(!c.tick(5000,false,false,900)); assert(!c.tick(6000,true,false,900));
    bool ok[]={true,false,true};
    auto exists=[&](size_t i){return ok[i];};
    assert(NextPlayable(3,0,exists)==2); assert(NextPlayable(3,2,exists)==0);
    ok[0]=false; assert(NextPlayable(3,2,exists)==-1);
    assert(NextPlayable(1,0,exists)==-1); assert(NextPlayable(0,-1,exists)==-1);
    puts("rotation pause, timing and missing files: OK");
}
