#pragma once
#include "video_edit.h"

// A preset only changes colour; crop and mirror remain untouched.
inline void ApplyPresetColors(VideoEdit& dst, VideoEdit src) {
    NormalizeEdit(src);
    dst.brightness=src.brightness; dst.contrast=src.contrast; dst.saturation=src.saturation;
    dst.gamma=src.gamma; dst.warmth=src.warmth; dst.hue=src.hue; dst.invert=src.invert;
}
inline VideoEdit BuiltinColorPreset(int id) {
    VideoEdit p;
    switch (id) {
    case 1: p.contrast=115; p.saturation=90; p.gamma=95; p.warmth=-8; break; // film
    case 2: p.brightness=-12; p.contrast=105; p.saturation=80; p.gamma=85; p.warmth=-20; break; // night
    case 3: p.saturation=0; p.contrast=110; break; // monochrome
    case 4: p.warmth=25; p.saturation=110; p.gamma=105; break; // warm
    default: break;
    }
    return p;
}
