#include "../src/trim.h"
#include <cassert>
#include <cstdio>
int main() {
    AudioRange r = TrimAudioRange(48000, 48000, 0, .25, .75);
    assert(r.first == 12000 && r.last == 36000);
    r = TrimAudioRange(1024, 48000, 1.0, 0, -1);
    assert(r.first == 0 && r.last == 1024);
    r = TrimAudioRange(1024, 48000, 1.0, 1.0 + 256.0/48000, 1.0 + 512.0/48000);
    assert(r.first == 256 && r.last == 512);
    r = TrimAudioRange(1024, 48000, 0, 1, 2);
    assert(r.first == 1024 && r.last == 1024);
    r = TrimAudioRange(1024, 48000, 2, 0, 1);
    assert(r.first == 0 && r.last == 0);
    puts("trim: PCM boundaries OK");
}
