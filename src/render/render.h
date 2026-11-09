#ifndef TB_RENDER_H
#define TB_RENDER_H

#include "raylib.h"
#include "models.h"
#include "../sim/core.h"

typedef struct {
    RenderTexture2D scene;
    RenderTexture2D bright;
    RenderTexture2D blurA;
    RenderTexture2D blurB;
    Camera3D camera;
    bool postEnabled;
    float qualityScale;
    int width, height;
    float splashFlash;
    float bobPhase;
} Renderer;

bool RendererInit(Renderer *r);
void RendererResize(Renderer *r, int w, int h);
void RendererFree(Renderer *r);
void RenderWorld(Renderer *r, Assets *a, Game *g, float dt);
void DrawRainOverlay(Renderer *r, Assets *a, Game *g, float dt);

#endif
