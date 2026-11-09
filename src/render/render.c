// ============================================================================
//  render.c — отрисовка мира: небо, рельеф, вода, модели, эффекты, пост-обработка
// ============================================================================
#include "render.h"
#include "mb.h"
#include "rlgl.h"

static void set_float(Shader s, int loc, float v) { if (loc >= 0 && s.id) SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
static float g_cullDist = 260.0f;   // дальность отсечения декора

static void set_vec3(Shader s, int loc, Vector3 v) { if (loc >= 0 && s.id) SetShaderValue(s, loc, (float[]){ v.x, v.y, v.z }, SHADER_UNIFORM_VEC3); }
static void set_int(Shader s, int loc, int v) { if (loc >= 0 && s.id) SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT); }

static void lit_setup(Assets *a, Game *g, int mode, float emissive, float gloss, Vector3 tint) {
    if (!a->litOk) return;
    set_int(a->shLit, a->uMode, mode);
    set_float(a->shLit, a->uEmissive, emissive);
    set_float(a->shLit, a->uGloss, gloss);
    set_vec3(a->shLit, a->uTint, tint);
}

bool RendererInit(Renderer *r) {
    memset(r, 0, sizeof(*r));
    r->width = GetScreenWidth();
    r->height = GetScreenHeight();
    r->postEnabled = true;   // окончательно решается ниже по наличию буферов
    r->qualityScale = 1.0f;
    RendererResize(r, r->width, r->height);
    r->camera.position = (Vector3){ 0, 2, 0 };
    r->camera.target = (Vector3){ 0, 2, 1 };
    r->camera.up = (Vector3){ 0, 1, 0 };
    r->camera.fovy = 68.0f;
    r->camera.projection = CAMERA_PERSPECTIVE;
    r->postEnabled = (r->scene.id > 0);
    return r->postEnabled;
}

void RendererResize(Renderer *r, int w, int h) {
    if (w < 64) w = 64;
    if (h < 64) h = 64;
    if (r->qualityScale <= 0.0f) r->qualityScale = 1.0f;
    if (w == r->width && h == r->height && r->scene.id > 0) return;
    if (r->scene.id > 0) UnloadRenderTexture(r->scene);
    if (r->bright.id > 0) UnloadRenderTexture(r->bright);
    if (r->blurA.id > 0) UnloadRenderTexture(r->blurA);
    if (r->blurB.id > 0) UnloadRenderTexture(r->blurB);
    r->width = w;
    r->height = h;
    int sw = (int)((float)w * r->qualityScale);
    int sh = (int)((float)h * r->qualityScale);
    if (sw < 64) sw = 64;
    if (sh < 64) sh = 64;
    r->scene = LoadRenderTexture(sw, sh);
    SetTextureFilter(r->scene.texture, TEXTURE_FILTER_BILINEAR);
    int hw = sw / 2, hh = sh / 2;
    int qw = sw / 4, qh = sh / 4;
    r->bright = LoadRenderTexture(hw > 8 ? hw : 8, hh > 8 ? hh : 8);
    r->blurA = LoadRenderTexture(qw > 8 ? qw : 8, qh > 8 ? qh : 8);
    r->blurB = LoadRenderTexture(qw > 8 ? qw : 8, qh > 8 ? qh : 8);
    SetTextureFilter(r->bright.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(r->blurA.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(r->blurB.texture, TEXTURE_FILTER_BILINEAR);
}

void RendererSetQuality(Renderer *r, float renderScale, float cullDistance, bool post) {
    if (renderScale < 0.35f) renderScale = 0.35f;
    if (renderScale > 1.0f) renderScale = 1.0f;
    if (cullDistance < 60.0f) cullDistance = 60.0f;
    g_cullDist = cullDistance;
    r->postEnabled = post;
    if (r->qualityScale != renderScale) {
        r->qualityScale = renderScale;
        int w = r->width, h = r->height;
        r->width = 0; r->height = 0;      // форсируем пересоздание буферов
        RendererResize(r, w, h);
    }
}

void RendererFree(Renderer *r) {
    if (r->scene.id > 0) UnloadRenderTexture(r->scene);
    if (r->bright.id > 0) UnloadRenderTexture(r->bright);
    if (r->blurA.id > 0) UnloadRenderTexture(r->blurA);
    if (r->blurB.id > 0) UnloadRenderTexture(r->blurB);
    memset(r, 0, sizeof(*r));
}

// ---------------------------------------------------------------- окружение
static void draw_sky(Assets *a, Game *g) {
    Environment *e = &g->env;
    if (a->skyOk) {
        set_vec3(a->shSky, a->sSunDir, (Vector3){ e->sunDir.x, e->sunDir.y, e->sunDir.z });
        set_vec3(a->shSky, a->sSunColor, (Vector3){ e->sunColor.x, e->sunColor.y, e->sunColor.z });
        set_vec3(a->shSky, a->sSkyTop, (Vector3){ e->skyTop.x, e->skyTop.y, e->skyTop.z });
        set_vec3(a->shSky, a->sSkyHorizon, (Vector3){ e->skyHorizon.x, e->skyHorizon.y, e->skyHorizon.z });
        Vector3 moon = (Vector3){ -e->sunDir.x, -e->sunDir.y, -e->sunDir.z };
        if (moon.y < 0.1f) moon.y = 0.1f;
        set_vec3(a->shSky, a->sMoonDir, Vector3Normalize(moon));
        set_float(a->shSky, a->sCloud, e->cloudAmount);
        float night = 1.0f - smoothstepf(-0.06f, 0.14f, e->sunDir.y);
        set_float(a->shSky, a->sNight, night);
        set_float(a->shSky, a->sTime, g->totalTime);
    }
    DrawModel(a->sky, Vector3Zero(), 2400.0f, WHITE);
}

static void draw_terrain(Assets *a, Game *g) {
    lit_setup(a, g, 1, 0.0f, 0.05f, (Vector3){ 1, 1, 1 });
    for (int i = 0; i < a->terrainCount; i++) DrawModel(a->terrain[i], Vector3Zero(), 1.0f, WHITE);
}

static void draw_props(Assets *a, Game *g) {
    lit_setup(a, g, 0, 0.0f, 0.12f, (Vector3){ 1, 1, 1 });
    const float cull2 = (g_cullDist + 30.0f) * (g_cullDist + 30.0f);
    for (int i = 0; i < a->propGroupCount; i++) {
        Vec3 c = a->propGroupCenter[i];
        float dx = c.x - g->camPos.x, dz = c.z - g->camPos.z;
        if (dx * dx + dz * dz > cull2) continue;   // батч за пределами видимой дальности
        DrawModel(a->propGroups[i], Vector3Zero(), 1.0f, WHITE);
    }
}

static void draw_water(Assets *a, Game *g) {
    Environment *e = &g->env;
    if (a->waterOk) {
        set_float(a->shWater, a->wTime, g->waterTime);
        set_float(a->shWater, a->wWind, e->wind);
        set_vec3(a->shWater, a->wSunDir, (Vector3){ e->sunDir.x, e->sunDir.y, e->sunDir.z });
        Vector3 sc = { e->sunColor.x * e->sunIntensity, e->sunColor.y * e->sunIntensity, e->sunColor.z * e->sunIntensity };
        set_vec3(a->shWater, a->wSunColor, sc);
        set_vec3(a->shWater, a->wSkyColor, (Vector3){ e->skyHorizon.x, e->skyHorizon.y, e->skyHorizon.z });
        set_vec3(a->shWater, a->wCamPos, (Vector3){ g->camPos.x, g->camPos.y, g->camPos.z });
        set_vec3(a->shWater, a->wFogColor, (Vector3){ e->skyHorizon.x * 0.95f, e->skyHorizon.y * 0.95f, e->skyHorizon.z * 0.95f });
        set_float(a->shWater, a->wFogDensity, 0.0016f + e->fogAmount * 0.012f);
        set_vec3(a->shWater, a->wShallow, (Vector3){ 0.20f, 0.52f, 0.50f });
        set_vec3(a->shWater, a->wDeep, (Vector3){ 0.035f, 0.145f, 0.215f });
        set_vec3(a->shWater, a->wFoamC, (Vector3){ 0.94f, 0.97f, 0.98f });
        set_float(a->shWater, a->wFoamAmt, 1.0f);
    }
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    DrawModel(a->water, Vector3Zero(), 1.0f, WHITE);
    EndBlendMode();
    rlEnableDepthMask();
}

// ------------------------------------------------------------------ сущности
static void draw_boat_transformed(Assets *a, Game *g) {
    Boat *b = &g->boat;
    Matrix xf = MatrixMultiply(MatrixMultiply(MatrixRotateZ(b->roll), MatrixRotateX(b->pitch)),
                               MatrixRotateY(b->yaw));
    xf = MatrixMultiply(xf, MatrixTranslate(b->pos.x, b->pos.y, b->pos.z));
    lit_setup(a, g, 0, 0.0f, 0.22f, (Vector3){ 1, 1, 1 });
    a->boat.transform = xf;
    DrawModel(a->boat, Vector3Zero(), 1.0f, WHITE);
    a->boat.transform = MatrixIdentity();
}

static void draw_static_landmarks(Assets *a, Game *g) {
    Landmarks *lm = &g->terrain.lm;
    lit_setup(a, g, 0, 0.0f, 0.10f, (Vector3){ 1, 1, 1 });

    a->dock.transform = MatrixMultiply(MatrixRotateY(lm->dockYaw), MatrixTranslate(lm->dockPos.x, 0.0f, lm->dockPos.z));
    DrawModel(a->dock, Vector3Zero(), 1.0f, WHITE);
    a->dock.transform = MatrixIdentity();

    a->lighthouse.transform = MatrixTranslate(lm->lighthouse.x, lm->lighthouse.y, lm->lighthouse.z);
    DrawModel(a->lighthouse, Vector3Zero(), 1.0f, WHITE);
    a->lighthouse.transform = MatrixIdentity();

    a->campfire.transform = MatrixTranslate(lm->firePos.x, lm->firePos.y, lm->firePos.z);
    DrawModel(a->campfire, Vector3Zero(), 1.0f, WHITE);
    a->campfire.transform = MatrixIdentity();

    a->tent.transform = MatrixMultiply(MatrixRotateY(lm->dockYaw + 1.9f),
                                       MatrixTranslate(lm->campPos.x - 2.5f, lm->campPos.y, lm->campPos.z - 2.0f));
    DrawModel(a->tent, Vector3Zero(), 1.0f, WHITE);
    a->tent.transform = MatrixIdentity();

    a->crate.transform = MatrixMultiply(MatrixRotateY(0.6f),
                                        MatrixTranslate(lm->shopPos.x + 1.6f, lm->shopPos.y, lm->shopPos.z + 1.2f));
    DrawModel(a->crate, Vector3Zero(), 1.0f, WHITE);
    a->barrel.transform = MatrixMultiply(MatrixRotateY(1.2f),
                                         MatrixTranslate(lm->shopPos.x + 2.4f, lm->shopPos.y, lm->shopPos.z + 0.2f));
    DrawModel(a->barrel, Vector3Zero(), 1.0f, WHITE);
    a->crate.transform = MatrixIdentity();
    a->barrel.transform = MatrixIdentity();

    a->signPost.transform = MatrixMultiply(MatrixRotateY(lm->dockYaw + 0.4f),
                                           MatrixTranslate(lm->dockPos.x - 3.5f, lm->dockPos.y, lm->dockPos.z + 2.0f));
    DrawModel(a->signPost, Vector3Zero(), 1.0f, WHITE);
    a->signPost.transform = MatrixIdentity();

    // Фонарь на причале
    a->lantern.transform = MatrixTranslate(lm->dockEnd.x - 1.2f, lm->dockEnd.y + 0.35f, lm->dockEnd.z - 1.0f);
    lit_setup(a, g, 0, 0.85f, 0.05f, (Vector3){ 1.0f, 0.86f, 0.55f });
    DrawModel(a->lantern, Vector3Zero(), 1.0f, WHITE);
    a->lantern.transform = MatrixIdentity();
    lit_setup(a, g, 0, 0.0f, 0.10f, (Vector3){ 1, 1, 1 });
}

static void draw_human(Assets *a, Game *g, Vector3 basePos, float yaw, float phase,
                       Vector3 clothTint, int hat) {
    lit_setup(a, g, 0, 0.0f, 0.12f, clothTint);
    float walkSwing = sinf(phase) * 0.55f;
    float breathe = sinf(phase * 0.5f) * 0.012f;
    float lean = 0.02f;

    Matrix base = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(basePos.x, basePos.y, basePos.z));

    // Ноги (качание вокруг бедра, y=0.80)
    for (int s = 0; s < 2; s++) {
        float swing = (s == 0 ? walkSwing : -walkSwing);
        Matrix hip = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(basePos.x + (s == 0 ? -0.10f : 0.10f) * cosf(yaw),
                                                                     basePos.y + 0.80f, basePos.z + (s == 0 ? -0.10f : 0.10f) * -sinf(yaw)));
        Matrix leg = MatrixMultiply(MatrixMultiply(MatrixRotateX(swing), MatrixRotateY(yaw)),
                                    MatrixTranslate((s == 0 ? -0.10f : 0.10f), -0.80f, 0.0f));
        Matrix m = MatrixMultiply(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(basePos.x, basePos.y + 0.80f, basePos.z)),
                                  MatrixMultiply(MatrixRotateX(swing), MatrixTranslate((s == 0 ? -0.11f : 0.11f), -0.74f, 0.0f)));
        a->humanParts[s].transform = m;
        DrawModel(a->humanParts[s], Vector3Zero(), 1.0f, WHITE);
        (void)hip; (void)leg;
    }
    // Торс
    a->humanParts[2].transform = MatrixMultiply(MatrixRotateY(yaw),
                                                MatrixTranslate(basePos.x, basePos.y + 0.72f + breathe, basePos.z));
    DrawModel(a->humanParts[2], Vector3Zero(), 1.0f, WHITE);
    // Голова
    a->humanParts[3].transform = MatrixMultiply(MatrixRotateY(yaw),
                                                MatrixTranslate(basePos.x, basePos.y + 1.48f + breathe, basePos.z));
    DrawModel(a->humanParts[3], Vector3Zero(), 1.0f, WHITE);
    // Руки (машут)
    for (int s = 0; s < 2; s++) {
        float swing = (s == 0 ? -walkSwing : walkSwing) * 0.8f;
        Matrix m = MatrixMultiply(MatrixRotateY(yaw),
                                  MatrixTranslate(basePos.x + (s == 0 ? -0.26f : 0.26f) * cosf(yaw),
                                                  basePos.y + 1.36f + breathe,
                                                  basePos.z - (s == 0 ? -0.26f : 0.26f) * sinf(yaw)));
        m = MatrixMultiply(m, MatrixRotateX(swing));
        m = MatrixMultiply(m, MatrixRotateZ((s == 0 ? 0.10f : -0.10f)));
        a->humanParts[4 + s].transform = m;
        DrawModel(a->humanParts[4 + s], Vector3Zero(), 1.0f, WHITE);
    }
    // Шляпа
    if (hat >= 0 && hat < 2) {
        a->humanHat[hat].transform = MatrixMultiply(MatrixRotateY(yaw),
                                                    MatrixTranslate(basePos.x, basePos.y + 1.58f + breathe, basePos.z));
        DrawModel(a->humanHat[hat], Vector3Zero(), 1.0f, WHITE);
    }
    for (int i = 0; i < 6; i++) a->humanParts[i].transform = MatrixIdentity();
    a->humanHat[0].transform = MatrixIdentity();
    a->humanHat[1].transform = MatrixIdentity();
    (void)base; (void)lean;
}

static void draw_npcs(Assets *a, Game *g) {
    for (int i = 0; i < MAX_NPC; i++) {
        Npc *n = &g->npc[i];
        if (n->name == NULL) continue;
        float dist = Vector3Distance((Vector3){ n->pos.x, n->pos.y, n->pos.z }, (Vector3){ g->camPos.x, g->camPos.y, g->camPos.z });
        if (dist > 140.0f) continue;
        if (n->kind == 0) {
            Vector3 tint = { 1, 1, 1 };
            int hat = -1;
            if (i == 0) { tint = (Vector3){ 0.86f, 0.62f, 0.36f }; hat = 0; }        // Никита
            else if (i == 1) { tint = (Vector3){ 0.42f, 0.52f, 0.68f }; hat = 1; }   // Илья
            else { tint = (Vector3){ 0.82f, 0.44f, 0.50f }; hat = -1; }              // Аня
            draw_human(a, g, (Vector3){ n->pos.x, n->pos.y, n->pos.z }, n->yaw, n->animPhase, tint, hat);
        } else if (n->kind == 1) {
            lit_setup(a, g, 0, 0.0f, 0.28f, (Vector3){ 0.9f, 0.95f, 1.0f });
            a->seal.transform = MatrixMultiply(MatrixRotateY(n->yaw + sinf(n->animPhase) * 0.1f),
                                               MatrixTranslate(n->pos.x, n->pos.y, n->pos.z));
            DrawModel(a->seal, Vector3Zero(), 1.0f, WHITE);
            a->seal.transform = MatrixIdentity();
        } else {
            lit_setup(a, g, 0, 0.0f, 0.12f, (Vector3){ 1, 1, 1 });
            a->dog.transform = MatrixMultiply(MatrixRotateY(n->yaw),
                                              MatrixTranslate(n->pos.x, n->pos.y + fabsf(sinf(n->animPhase)) * 0.06f, n->pos.z));
            DrawModel(a->dog, Vector3Zero(), 1.0f, WHITE);
            a->dog.transform = MatrixIdentity();
        }
    }
}

static void draw_gulls(Assets *a, Game *g) {
    lit_setup(a, g, 0, 0.0f, 0.2f, (Vector3){ 1, 1, 1 });
    for (int i = 0; i < 7; i++) {
        float t = g->totalTime * (0.19f + (float)i * 0.017f) + (float)i * 1.7f;
        float radius = 26.0f + (float)i * 7.0f;
        Vector3 center = { g->terrain.lm.boatStart.x + (float)(i % 3) * 22.0f - 22.0f, 12.0f + (float)i * 1.6f,
                           g->terrain.lm.boatStart.z + (float)(i % 2) * 30.0f - 15.0f };
        Vector3 p = { center.x + cosf(t) * radius, center.y + sinf(t * 1.7f) * 1.4f, center.z + sinf(t) * radius };
        float yaw = -t + PI * 0.5f;
        Matrix m = MatrixMultiply(MatrixMultiply(MatrixRotateY(yaw), MatrixRotateX(sinf(t * 6.0f) * 0.35f)),
                                  MatrixTranslate(p.x, p.y, p.z));
        a->gull.transform = m;
        DrawModel(a->gull, Vector3Zero(), 1.0f, WHITE);
        a->gull.transform = MatrixIdentity();
    }
}

static void draw_bobber_and_line(Assets *a, Game *g) {
    Fishing *f = &g->fish_rod;
    if (f->phase == FISH_IDLE) return;
    Vec3 tipS = FishingRodTip(g);
    Vector3 tip = { tipS.x, tipS.y, tipS.z };
    Vector3 bob = { f->bobberPos.x, f->bobberPos.y, f->bobberPos.z };

    if (f->phase == FISH_FLOAT || f->phase == FISH_CAST || f->phase == FISH_HOOKED) {
        // Леска с провисом
        float dist = Vector3Distance(tip, bob);
        float sag = (1.0f - saturatef(f->tension)) * fminf(dist * 0.16f, 1.8f);
        if (f->phase == FISH_HOOKED) sag *= 0.25f;
        const int SEG = 18;
        Vector3 prev = tip;
        for (int i = 1; i <= SEG; i++) {
            float t = (float)i / (float)SEG;
            Vector3 p = Vector3Lerp(tip, bob, t);
            p.y -= sinf(t * PI) * sag;
            DrawLine3D(prev, p, (Color){ 235, 238, 240, 190 });
            prev = p;
        }
        lit_setup(a, g, 0, 0.25f, 0.3f, (Vector3){ 1, 1, 1 });
        a->bobber.transform = MatrixTranslate(bob.x, bob.y, bob.z);
        DrawModel(a->bobber, Vector3Zero(), 1.0f, WHITE);
        a->bobber.transform = MatrixIdentity();
    }
}

static void draw_rod(Assets *a, Game *g) {
    Player *p = &g->player;
    if (p->mode == PM_SWIM) return;
    float bend = g->fish_rod.rodBend;
    Vec3 fwd = dir_from_yaw_pitch(p->yaw, 0.0f);
    Vec3 right = v3(cosf(p->yaw), 0, -sinf(p->yaw));

    // Ручка удилища у правой руки
    Vec3 hand = v3add(p->pos, v3(0, 1.34f, 0));
    hand = v3add(hand, v3mul(right, 0.28f));
    hand = v3add(hand, v3mul(fwd, 0.30f));
    float pitchBase = p->pitch * 0.55f + 0.55f;

    lit_setup(a, g, 0, 0.0f, 0.35f, (Vector3){ 1, 1, 1 });
    Vector3 cursor = { hand.x, hand.y, hand.z };
    float angle = pitchBase;
    float segLen = 0.62f;
    for (int i = 0; i < 4; i++) {
        Vec3 dir = dir_from_yaw_pitch(p->yaw, angle);
        Vector3 mid = { cursor.x, cursor.y + segLen * 0.5f, cursor.z };
        Matrix m = MatrixMultiply(MatrixRotateY(p->yaw), MatrixTranslate(cursor.x, cursor.y, cursor.z));
        // Наклон сегмента: поворот вокруг X на -(angle) + небольшая доп. кривизна
        Matrix tilt = MatrixRotateX(-(angle) + 0.0f);
        a->rodSegment[i].transform = MatrixMultiply(MatrixMultiply(MatrixRotateY(p->yaw), tilt),
                                                    MatrixTranslate(cursor.x, cursor.y, cursor.z));
        DrawModel(a->rodSegment[i], Vector3Zero(), 1.0f, WHITE);
        cursor.x += dir.x * segLen;
        cursor.y += dir.y * segLen;
        cursor.z += dir.z * segLen;
        segLen *= 0.93f;
        angle -= 0.10f + bend * 0.42f;
        (void)mid; (void)m;
    }
    for (int i = 0; i < 4; i++) a->rodSegment[i].transform = MatrixIdentity();
}

static void draw_fish(Assets *a, Game *g) {
    lit_setup(a, g, 0, 0.05f, 0.45f, (Vector3){ 1, 1, 1 });
    int drawn = 0;
    for (int i = 0; i < MAX_FISH && drawn < 40; i++) {
        FishInst *f = &g->fish[i];
        if (!f->alive) continue;
        float d2 = (f->pos.x - g->camPos.x) * (f->pos.x - g->camPos.x) +
                   (f->pos.z - g->camPos.z) * (f->pos.z - g->camPos.z);
        if (d2 > 55.0f * 55.0f) continue;
        if (g->underwaterCam == false && f->pos.y > -0.4f) continue;
        if (f->species < 0 || f->species >= a->fishModelCount) continue;
        float wobble = sinf(f->wiggle) * 0.30f;
        Matrix m = MatrixMultiply(MatrixMultiply(MatrixRotateY(f->yaw + wobble * 0.5f),
                                                 MatrixRotateX(sinf(f->wiggle * 0.7f) * 0.12f)),
                                  MatrixTranslate(f->pos.x, f->pos.y, f->pos.z));
        m = MatrixMultiply(m, MatrixScale(f->sizeScale, f->sizeScale, f->sizeScale));
        a->fish[f->species].transform = m;
        DrawModel(a->fish[f->species], Vector3Zero(), 1.0f, WHITE);
        a->fish[f->species].transform = MatrixIdentity();
        drawn++;
    }
}

static Camera3D cam_from_game(Game *g) {
    Camera3D c = { 0 };
    c.position = (Vector3){ g->camPos.x, g->camPos.y, g->camPos.z };
    c.target = (Vector3){ g->camTarget.x, g->camTarget.y, g->camTarget.z };
    c.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    c.fovy = (g->settings.fov > 20.0f) ? g->settings.fov : 60.0f;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

static void draw_particles(Assets *a, Game *g) {
    (void)a;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (p->life <= 0.0f) continue;
        float t = p->life / p->maxLife;
        Color c = { p->color.x > 1 ? 255 : (unsigned char)(p->color.x * 255),
                    (unsigned char)(clampf(p->color.y, 0, 1) * 255),
                    (unsigned char)(clampf(p->color.z, 0, 1) * 255),
                    (unsigned char)(clampf(t, 0, 1) * 220) };
        float size = p->size * (0.5f + t * 0.7f);
        Rectangle src = { 0, 0, (float)a->texFoam.width, (float)a->texFoam.height };
        if (p->kind == PT_SPLASH) src = (Rectangle){ 0, 0, (float)a->texGlow.width, (float)a->texGlow.height };
        Texture2D tex = (p->kind == PT_SPLASH) ? a->texGlow : a->texFoam;
        src.width = (float)tex.width;
        src.height = (float)tex.height;
        DrawBillboardRec(cam_from_game(g), tex, src, (Vector3){ p->pos.x, p->pos.y, p->pos.z },
                         (Vector2){ size, size }, c);
    }
}

static void draw_lantern_glow(Assets *a, Game *g) {
    if (!g->lanternOn && g->player.mode != PM_BOAT) return;
    Vector3 p = { g->player.pos.x, g->player.pos.y + 1.5f, g->player.pos.z };
    Vec3 fwd = dir_from_yaw_pitch(g->player.yaw, 0);
    p.x += fwd.x * 0.4f;
    p.z += fwd.z * 0.4f;
    float flick = g->lanternFlicker;
    Color c = { 255, (unsigned char)(200 * flick), (unsigned char)(130 * flick), (unsigned char)(70 * flick) };
    Rectangle src = { 0, 0, (float)a->texGlow.width, (float)a->texGlow.height };
    DrawBillboardRec(cam_from_game(g), a->texGlow, src, p, (Vector2){ 3.2f, 3.2f }, c);
}

// ------------------------------------------------------------------ пост-эффекты
static void post_process(Renderer *r, Assets *a, Game *g) {
    if (!r->postEnabled || !a->blurOk || !a->compositeOk) {
        BeginBlendMode(BLEND_ALPHA);
        DrawTexturePro(r->scene.texture,
                       (Rectangle){ 0, 0, (float)r->scene.texture.width, (float)-r->scene.texture.height },
                       (Rectangle){ 0, 0, (float)r->width, (float)r->height }, (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndBlendMode();
        return;
    }
    Shader bl = a->shBlur;
    // 1. Яркий проход (половинное разрешение)
    BeginTextureMode(r->bright);
    ClearBackground(BLACK);
    BeginShaderMode(bl);
    set_float(bl, a->bThreshold, 0.68f);
    SetShaderValue(bl, a->bDir, (float[]){ 1.0f / (float)r->bright.texture.width, 0.0f }, SHADER_UNIFORM_VEC2);
    DrawTextureRec(r->scene.texture, (Rectangle){ 0, 0, (float)r->scene.texture.width, (float)-r->scene.texture.height },
                   (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
    EndTextureMode();

    // 2. Размытие блума (2 прохода) в blurA
    BeginTextureMode(r->blurA);
    ClearBackground(BLACK);
    BeginShaderMode(bl);
    set_float(bl, a->bThreshold, 0.0f);
    SetShaderValue(bl, a->bDir, (float[]){ 1.0f / (float)r->bright.texture.width, 0.0f }, SHADER_UNIFORM_VEC2);
    DrawTextureRec(r->bright.texture, (Rectangle){ 0, 0, (float)r->bright.texture.width, (float)-r->bright.texture.height },
                   (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginTextureMode(r->blurB);
    ClearBackground(BLACK);
    BeginShaderMode(bl);
    set_float(bl, a->bThreshold, 0.0f);
    SetShaderValue(bl, a->bDir, (float[]){ 0.0f, 1.0f / (float)r->blurA.texture.height }, SHADER_UNIFORM_VEC2);
    DrawTextureRec(r->blurA.texture, (Rectangle){ 0, 0, (float)r->blurA.texture.width, (float)-r->blurA.texture.height },
                   (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
    EndTextureMode();

    // 3. Размытая копия сцены для Tilt-Shift (горизонтально в blurA, вертикально обратно в blurB)
    BeginTextureMode(r->blurA);
    ClearBackground(BLACK);
    BeginShaderMode(bl);
    set_float(bl, a->bThreshold, 0.0f);
    SetShaderValue(bl, a->bDir, (float[]){ 2.0f / (float)r->scene.texture.width, 0.0f }, SHADER_UNIFORM_VEC2);
    DrawTextureRec(r->scene.texture, (Rectangle){ 0, 0, (float)r->scene.texture.width, (float)-r->scene.texture.height },
                   (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
    EndTextureMode();

    // 4. Вертикальный проход для Tilt-Shift: сцена -> bright (половинное разрешение)
    BeginTextureMode(r->bright);
    ClearBackground(BLACK);
    BeginShaderMode(bl);
    set_float(bl, a->bThreshold, 0.0f);
    SetShaderValue(bl, a->bDir, (float[]){ 0.0f, 1.5f / (float)r->blurA.texture.height }, SHADER_UNIFORM_VEC2);
    DrawTextureRec(r->blurA.texture, (Rectangle){ 0, 0, (float)r->blurA.texture.width, (float)-r->blurA.texture.height },
                   (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
    EndTextureMode();

    // 5. Композиция
    Shader cp = a->shComposite;
    rlActiveTextureSlot(1);
    rlEnableTexture(r->blurB.texture.id);
    rlActiveTextureSlot(2);
    rlEnableTexture(r->bright.texture.id);
    rlActiveTextureSlot(0);

    BeginShaderMode(cp);
    int unit1 = 1, unit2 = 2;
    SetShaderValue(cp, GetShaderLocation(cp, "textureBloom"), &unit1, SHADER_UNIFORM_INT);
    SetShaderValue(cp, GetShaderLocation(cp, "textureBlur"), &unit2, SHADER_UNIFORM_INT);
    float res[2] = { (float)r->scene.texture.width, (float)r->scene.texture.height };
    SetShaderValue(cp, a->cRes, res, SHADER_UNIFORM_VEC2);
    set_float(cp, a->cBloom, g->settings.bloom ? 0.75f : 0.0f);
    set_float(cp, a->cDof, g->settings.tiltShift ? 0.65f : 0.0f);
    set_float(cp, a->cVig, 0.55f);
    float sat = 1.06f, con = 1.04f;
    if (g->env.weather == W_FOG) sat = 0.92f;
    if (g->env.weather == W_STORM) { sat = 0.88f; con = 1.10f; }
    set_float(cp, a->cSat, sat);
    set_float(cp, a->cCon, con);
    set_float(cp, a->cTime, g->totalTime);
    float under = g->underwaterCam ? 1.0f : 0.0f;
    set_float(cp, a->cUnder, under);
    set_float(cp, a->cFlash, g->env.lightningFlash * 0.55f);
    float tint[3] = { 1.0f, 1.0f, 1.0f };
    if (g->env.weather == W_STORM) { tint[0] = 0.94f; tint[1] = 0.97f; tint[2] = 1.04f; }
    if (EnvIsNight(&g->env)) { tint[0] = 0.90f; tint[1] = 0.94f; tint[2] = 1.07f; }
    SetShaderValue(cp, a->cTint, tint, SHADER_UNIFORM_VEC3);
    DrawTexturePro(r->scene.texture,
                   (Rectangle){ 0, 0, (float)r->scene.texture.width, (float)-r->scene.texture.height },
                   (Rectangle){ 0, 0, (float)r->width, (float)r->height }, (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndShaderMode();
}

// -------------------------------------------------------------------- основной
static void draw_scene(Renderer *r, Assets *a, Game *g) {
    Environment *e = &g->env;
    Color clear = { (unsigned char)(clampf(e->skyHorizon.x, 0, 1) * 255),
                    (unsigned char)(clampf(e->skyHorizon.y, 0, 1) * 255),
                    (unsigned char)(clampf(e->skyHorizon.z, 0, 1) * 255), 255 };
    ClearBackground(clear);

    BeginMode3D(r->camera);
    SetLitParams(a, g);
    draw_sky(a, g);
    draw_terrain(a, g);
    draw_props(a, g);
    draw_static_landmarks(a, g);
    draw_npcs(a, g);
    draw_gulls(a, g);
    draw_boat_transformed(a, g);
    draw_fish(a, g);
    draw_rod(a, g);
    draw_bobber_and_line(a, g);
    draw_particles(a, g);
    draw_lantern_glow(a, g);
    draw_water(a, g);
    EndMode3D();
}

void RenderWorld(Renderer *r, Assets *a, Game *g, float dt) {
    (void)dt;
    r->camera.position = (Vector3){ g->camPos.x, g->camPos.y, g->camPos.z };
    r->camera.target = (Vector3){ g->camTarget.x, g->camTarget.y, g->camTarget.z };
    r->camera.up = (Vector3){ 0, 1, 0 };
    r->camera.fovy = g->settings.fov > 20.0f ? g->settings.fov : 68.0f;
    r->camera.projection = CAMERA_PERSPECTIVE;

    // Если пост-обработка недоступна — рисуем прямо в буфер экрана
    if (!r->postEnabled || r->scene.id == 0) {
        draw_scene(r, a, g);
        return;
    }

    BeginTextureMode(r->scene);
    draw_scene(r, a, g);
    EndTextureMode();
    post_process(r, a, g);
}

// Дождь и снег — экранный слой поверх сцены
void DrawRainOverlay(Renderer *r, Assets *a, Game *g, float dt) {
    (void)r; (void)dt;
    float rain = g->env.rainAmount;
    if (rain < 0.02f || !g->settings.rainEffects) return;
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    float scroll = fmodf(g->totalTime * 620.0f, (float)h);
    int tiles = 4;
    for (int i = 0; i < tiles; i++) {
        Rectangle src = { 0, 0, (float)a->texRain.width, -(float)a->texRain.height };
        Rectangle dst = { (float)((i % 2) * w / 2), fmodf(scroll + (float)i * (float)h / (float)tiles, (float)h) - (float)h,
                          (float)w / 2.0f, (float)h };
        DrawTexturePro(a->texRain, src, dst, (Vector2){ 0, 0 }, 0.0f,
                       (Color){ 255, 255, 255, (unsigned char)(rain * 70) });
        dst.y += (float)h;
        DrawTexturePro(a->texRain, src, dst, (Vector2){ 0, 0 }, 0.0f,
                       (Color){ 255, 255, 255, (unsigned char)(rain * 70) });
    }
    // Крупные капли ближе к камере
    if (rain > 0.4f) {
        Color c = { 220, 235, 245, (unsigned char)(rain * 90) };
        for (int i = 0; i < 90; i++) {
            float rx = fmodf((float)i * 137.5f, (float)w);
            float ry = fmodf((float)i * 271.3f + g->totalTime * 900.0f, (float)h);
            DrawLineEx((Vector2){ rx, ry }, (Vector2){ rx + 2.0f, ry + 26.0f }, 1.4f, c);
        }
    }
}
