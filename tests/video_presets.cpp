#include "../src/video_presets.h"
#include <cassert>
#include <cstdio>
int main() {
    VideoEdit e; e.left=123; e.top=900; e.right=9000; e.bottom=8000; e.flipX=true; e.flipY=true;
    for(int i=0;i<5;i++) {
        VideoEdit p=BuiltinColorPreset(i); ApplyPresetColors(e,p);
        assert(e.left==123 && e.top==900 && e.right==9000 && e.bottom==8000 && e.flipX && e.flipY);
        assert(e.brightness==p.brightness && e.gamma==p.gamma && e.invert==p.invert);
    }
    auto old=e; VideoEdit corrupt; corrupt.gamma=0; corrupt.hue=2000; corrupt.contrast=900;
    ApplyPresetColors(e,corrupt); assert(e.gamma==25 && e.hue==180 && e.contrast==200);
    assert(e.left==old.left && e.flipX==old.flipX);
    puts("editor colour presets preserve crop and mirroring: OK");
}
