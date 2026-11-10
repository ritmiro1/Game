#include "../src/video_edit.h"
#include <cassert>
#include <cstdio>
int main() {
    VideoEdit e;
    auto a = CropRect(10, 20, 1920, 1080, e);
    assert(a.x == 10 && a.y == 20 && a.width == 1920 && a.height == 1080);
    e.left = 2500; e.top = 1000; e.right = 7500; e.bottom = 9000;
    a = CropRect(10, 20, 1920, 1080, e);
    assert(a.x == 490 && a.y == 128 && a.width == 960 && a.height == 864);
    e.left = 10000; e.right = -1; e.top = 10000; e.bottom = -1;
    a = CropRect(0, 0, 10, 10, e);
    assert(a.x >= 0 && a.y >= 0 && a.width >= 1 && a.height >= 1 && a.x + a.width <= 10 && a.y + a.height <= 10);
    uint8_t p[] = {10, 20, 200, 255}; VideoEdit c;
    AdjustColors(p, 1, c); assert(p[0] == 10 && p[1] == 20 && p[2] == 200);
    c.saturation = 0; AdjustColors(p, 1, c); assert(p[0] == p[1] && p[1] == p[2]);
    c.brightness = 100; AdjustColors(p, 1, c); assert(p[0] == 255 && p[1] == 255 && p[2] == 255);
    VideoEdit neutral, fx;
    uint8_t pixel[] = {100,100,100,77};
    fx.gamma = 200; assert(HasColorAdjustments(fx) && !SameColorAdjustments(fx, neutral));
    AdjustColors(pixel, 1, fx); assert(pixel[0] > 100 && pixel[0] == pixel[1] && pixel[1] == pixel[2] && pixel[3] == 77);
    fx = VideoEdit{}; fx.warmth = 50; pixel[0]=pixel[1]=pixel[2]=100;
    AdjustColors(pixel, 1, fx); assert(pixel[0] < 100 && pixel[2] > 100 && pixel[1] == 100);
    fx = VideoEdit{}; fx.invert = true; pixel[0]=10; pixel[1]=20; pixel[2]=200;
    assert(!SameColorAdjustments(fx, neutral));
    AdjustColors(pixel, 1, fx); assert(pixel[0] == 245 && pixel[1] == 235 && pixel[2] == 55);
    fx = VideoEdit{}; fx.hue = 90; pixel[0]=0; pixel[1]=0; pixel[2]=200;
    assert(HasColorAdjustments(fx) && !SameColorAdjustments(fx, neutral));
    AdjustColors(pixel, 1, fx); assert(pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 200);
    fx = VideoEdit{}; fx.gamma = -100; fx.hue = 300; fx.warmth = 300;
    NormalizeEdit(fx); assert(fx.gamma == 25 && fx.hue == 180 && fx.warmth == 100);
    uint8_t image[16] = {1,0,0,0, 2,0,0,0, 3,0,0,0, 4,0,0,0};
    FlipImage(image,2,2,true,false); assert(image[0]==2 && image[4]==1 && image[8]==4 && image[12]==3);
    FlipImage(image,2,2,false,true); assert(image[0]==4 && image[4]==3 && image[8]==2 && image[12]==1);
    // Crop coordinates always remain valid, including tiny images and malformed settings.
    for (int w = 1; w < 35; ++w) for (int h = 1; h < 35; ++h) {
        for (int v = -10000; v <= 20000; v += 3333) {
            VideoEdit z; z.left = v; z.right = 18000-v; z.top = 10000-v; z.bottom = v;
            auto q = CropRect(7, 9, w, h, z);
            assert(q.x >= 7 && q.y >= 9 && q.width >= 1 && q.height >= 1);
            assert(q.x + q.width <= 7 + w && q.y + q.height <= 9 + h);
        }
    }
    puts("video crop, color, gamma, hue, warmth, invert and mirror: OK");
}
