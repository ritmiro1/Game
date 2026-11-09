// ============================================================================
//  ТИХИЙ ЗАЛИВ — main.c
//  Атмосферный 3D-симулятор рыбалки и исследований. Русский интерфейс.
// ============================================================================
#include "raylib.h"
#include "rlgl.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "sim/core.h"
#include "platform/pak.h"
#include "render/models.h"
#include "render/render.h"
#include "render/ui.h"
#include "audio/audio.h"

#include <stdio.h>
#include <string.h>

static Game G;
static Assets A;
static Renderer R;
static UI UIx;

static void input_collect(Game *g, int *flags, Vec3 *move, Vec3 *look) {
    *flags = 0;
    *move = v3(0, 0, 0);
    Vector2 md = GetMouseDelta();
    *look = v3(md.x, md.y, 0);
    if (IsKeyDown(KEY_W)) move->z += 1.0f;
    if (IsKeyDown(KEY_S)) move->z -= 1.0f;
    if (IsKeyDown(KEY_A)) move->x -= 1.0f;
    if (IsKeyDown(KEY_D)) move->x += 1.0f;
    if (IsKeyDown(KEY_LEFT_SHIFT)) *flags |= IN_SPRINT;
    if (IsKeyDown(KEY_C) || IsKeyDown(KEY_LEFT_CONTROL)) *flags |= IN_CROUCH;
    if (IsKeyPressed(KEY_SPACE)) *flags |= IN_JUMP;
    if (IsKeyPressed(KEY_E)) *flags |= IN_USE;
    if (IsKeyPressed(KEY_F)) *flags |= IN_BOARD;
    if (IsKeyPressed(KEY_L)) *flags |= IN_LANTERN;
    if (IsKeyPressed(KEY_N)) *flags |= IN_SONAR;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) *flags |= IN_FISH;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) *flags |= IN_MOUSEDN;
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) *flags |= IN_ALT;
    // Фрикцион
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        g->fish_rod.drag = clampf(g->fish_rod.drag + wheel * 0.05f, 0.5f, 1.6f);
    }
}

static void audio_frame(Game *g, float dt) {
    Vec3 dir = v3sub(g->camTarget, g->camPos);
    if (v3len2(dir) < 0.0001f) dir = v3(0, 0, 1);
    dir = v3norm(dir);
    float night = 1.0f - smoothstepf(-0.06f, 0.14f, g->env.sunDir.y);
    float height = g->player.pos.y - SEA_LEVEL;
    float waterNear = (g->player.mode == PM_BOAT) ? 1.0f : (1.0f - smoothstepf(1.0f, 16.0f, height));
    if (g->player.mode == PM_SWIM) waterNear = 1.0f;
    float fireDist = v3len(v3flat(v3sub(g->player.pos, g->terrain.lm.firePos)));
    float fireNear = 1.0f - smoothstepf(2.0f, 18.0f, fireDist);
    float engine = g->boat.occupied ? saturatef(v3len(g->boat.vel) / 6.5f) : 0.0f;
    bool rainWood = (g->player.mode == PM_BOAT) || (v3len(v3flat(v3sub(g->player.pos, g->terrain.lm.campPos))) < 6.0f);
    AudioLevels lv = { g->settings.masterVolume, g->settings.musicVolume,
                       g->settings.sfxVolume, g->settings.ambientVolume };
    AudioApplyLevels(lv);
    AudioUpdate(dt, (Vector3){ g->camPos.x, g->camPos.y, g->camPos.z },
                (Vector3){ dir.x, dir.y, dir.z },
                g->env.wind, g->env.rainAmount, night, waterNear, fireNear, engine,
                rainWood, g->player.mode == PM_BOAT ? 1.0f : 0.0f);
}

static MusicId pick_music(Game *g) {
    if (g->fish_rod.phase == FISH_HOOKED) return MUS_TENSION;
    if (g->caughtPopupTimer > 2.6f) return MUS_WIN;
    if (EnvIsNight(&g->env)) return MUS_NIGHT;
    return MUS_CALM;
}

// ------------------------------------------------------------- старт/загрузка
static void start_new_game(void) {
    SimNewGame(&G, 20251109u);
    AssetsBuildPropGroups(&A, &G);
    AudioSetMusic(MUS_CALM);
}

static bool load_existing(void) {
    BuildSavePath(&G);
    if (!SaveExists(G.savePath)) return false;
    return LoadGame(&G, G.savePath);
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Тихий Залив — 3D рыбалка");
    SetWindowMinSize(1024, 600);
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    SetTargetFPS(60);

    TraceLog(LOG_INFO, "=== ТИХИЙ ЗАЛИВ %s ===", TB_VERSION);
    PakOpen();
    TraceLog(LOG_INFO, "Ресурсы: %s (файлов в архиве: %d)", PakGetPath(), PakCount());

    // Экран загрузки
    BeginDrawing();
    ClearBackground((Color){ 6, 14, 18, 255 });
    EndDrawing();

    bool uiOk = UIInit(&UIx);
    BeginDrawing();
    UIDrawLoading(&UIx, "Загрузка ресурсов...", 0.15f);
    EndDrawing();

    SimInit(&G, 20251109u);

    BeginDrawing();
    UIDrawLoading(&UIx, "Сборка 3D-моделей и материалов...", 0.35f);
    EndDrawing();
    AssetsInit(&A);

    BeginDrawing();
    UIDrawLoading(&UIx, "Построение рельефа, воды и растительности...", 0.6f);
    EndDrawing();
    AssetsBuildWorld(&A, &G);

    BeginDrawing();
    UIDrawLoading(&UIx, "Загрузка звуковых банков...", 0.85f);
    EndDrawing();
    AudioInit();
    AudioApplyLevels((AudioLevels){ G.settings.masterVolume, G.settings.musicVolume,
                                    G.settings.sfxVolume, G.settings.ambientVolume });

    RendererInit(&R);
    G.screen = GS_TITLE;
    BuildSavePath(&G);

    BeginDrawing();
    UIDrawLoading(&UIx, "Готово", 1.0f);
    EndDrawing();

    float fade = 1.0f;
    float titleTime = 0.0f;
    bool running = true;
    while (running && !WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;
        titleTime += dt;

        UIResetRequests(&UIx);
        int flags = 0;
        Vec3 move = v3(0, 0, 0), look = v3(0, 0, 0);
        bool cursorFree = (G.screen != GS_PLAY);

        if (G.screen == GS_PLAY) {
            input_collect(&G, &flags, &move, &look);
            if (IsKeyPressed(KEY_ESCAPE)) { G.screen = GS_PAUSE; cursorFree = true; }
            if (IsKeyPressed(KEY_J)) { G.screen = GS_JOURNAL; G.journalTab = 0; cursorFree = true; }
        } else if (G.screen == GS_DIALOG) {
            if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                NpcTalkAdvance(&G);
                if (!G.dialogActive) {
                    if (G.screen == GS_DIALOG) G.screen = GS_PLAY;
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) { G.dialogActive = false; G.screen = GS_PLAY; }
        } else if (G.screen == GS_PAUSE) {
            if (IsKeyPressed(KEY_ESCAPE)) G.screen = GS_PLAY;
        } else if (G.screen == GS_JOURNAL) {
            if (IsKeyPressed(KEY_J) || IsKeyPressed(KEY_ESCAPE)) {
                G.screen = (G.prevScreen == GS_PAUSE) ? GS_PAUSE : GS_PLAY;
            }
        } else if (G.screen == GS_SETTINGS) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                G.screen = (G.prevScreen == GS_PAUSE) ? GS_PAUSE : (G.prevScreen == GS_TITLE ? GS_TITLE : GS_PLAY);
            }
        } else if (G.screen == GS_SHOP) {
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_E)) G.screen = GS_PLAY;
        } else if (G.screen == GS_ACHIEVEMENTS) {
            if (IsKeyPressed(KEY_ESCAPE)) G.screen = (G.prevScreen == GS_TITLE) ? GS_TITLE : GS_PAUSE;
        }

        // Симуляция мира
        if (G.screen == GS_PLAY || G.screen == GS_DIALOG) {
            SimUpdate(&G, dt, flags, move, look);
        } else if (G.screen == GS_TITLE) {
            float pt = G.playTime;
            SimUpdate(&G, dt * 0.6f, 0, v3(0, 0, 0), v3(0, 0, 0));
            G.playTime = pt;
        } else {
            // Пауза: живёт только окружение
            SimUpdateIdle(&G, dt * 0.35f);
        }

        // Кинематографичная камера в главном меню
        if (G.screen == GS_TITLE) {
            Vec3 center = v3(G.terrain.lm.boatStart.x, 6.0f, G.terrain.lm.boatStart.z);
            float ang = titleTime * 0.055f;
            float radius = 78.0f;
            G.camPos = v3(center.x + cosf(ang) * radius, 26.0f + sinf(titleTime * 0.12f) * 6.0f,
                          center.z + sinf(ang) * radius);
            G.camTarget = v3(center.x, 4.0f, center.z);
        }

        // Сохранение/загрузка
        if (UIx.saveRequested) {
            BuildSavePath(&G);
            if (SaveGame(&G, G.savePath)) Toast(&G, "Сохранено: %s", G.savePath);
        }
        if (UIx.loadRequested) {
            unsigned int savedSeed = SavePeekSeed(G.savePath);
            if (savedSeed != 0 && savedSeed != A.builtSeed) {
                Toast(&G, "Сохранение из другого мира — загрузка невозможна");
            } else if (load_existing()) {
                G.screen = GS_PLAY;
                Toast(&G, "Игра загружена");
            } else {
                Toast(&G, "Сохранение не найдено");
            }
        }
        if (UIx.newGameRequested) {
            start_new_game();
            G.screen = GS_PLAY;
        }
        if (UIx.quitRequested) running = false;

        // Музыка и звук
        AudioSetMusic(pick_music(&G));
        audio_frame(&G, dt);

        // Звуковые события симуляции
        if (G.fish_rod.phase == FISH_HOOKED && G.fish_rod.reeling) {
            static float reelTimer = 0.0f;
            reelTimer += dt;
            if (reelTimer > 0.11f) {
                reelTimer = 0.0f;
                AudioPlay(SFX_REEL, (Vector3){ G.fish_rod.bobberPos.x, G.fish_rod.bobberPos.y, G.fish_rod.bobberPos.z }, 0.5f, 1.0f);
            }
        }
        if (G.splashAmount > 0.28f) {
            static float splashCd = 0.0f;
            splashCd -= dt;
            if (splashCd <= 0.0f) {
                splashCd = 0.35f;
                AudioPlay(SFX_SPLASH_SMALL, (Vector3){ G.lastSplashPos.x, G.lastSplashPos.y, G.lastSplashPos.z }, 0.6f,
                          0.9f + G.splashAmount * 0.4f);
            }
        }
        if (G.env.lightningFlash > 0.92f) {
            static float thunderCd = 0.0f;
            thunderCd -= dt;
            if (thunderCd <= 0.0f) {
                thunderCd = 4.0f;
                AudioPlay2D(SFX_THUNDER, 0.8f, 1.0f);
                G.camShake = fmaxf(G.camShake, 0.3f);
            }
        }

        // ------------------------------------------------------------ отрисовка
        if (IsWindowResized()) RendererResize(&R, GetScreenWidth(), GetScreenHeight());
        RenderWorld(&R, &A, &G, dt);
        DrawRainOverlay(&R, &A, &G, dt);

        switch (G.screen) {
            case GS_TITLE:        UIDrawTitle(&UIx, &A, &G); break;
            case GS_PLAY:         UIDrawHUD(&UIx, &A, &G); break;
            case GS_PAUSE:        UIDrawHUD(&UIx, &A, &G); UIDrawPause(&UIx, &A, &G); break;
            case GS_SETTINGS:
                if (G.prevScreen == GS_TITLE) UIDrawTitle(&UIx, &A, &G);
                else { UIDrawHUD(&UIx, &A, &G); UIDrawPause(&UIx, &A, &G); }
                UIDrawSettings(&UIx, &A, &G);
                break;
            case GS_JOURNAL:      UIDrawJournal(&UIx, &A, &G); break;
            case GS_SHOP:         UIDrawShop(&UIx, &A, &G); break;
            case GS_DIALOG:       UIDrawDialog(&UIx, &A, &G); break;
            case GS_ACHIEVEMENTS: UIDrawAchievements(&UIx, &A, &G); break;
            default: break;
        }

        // Затемнение при загрузке/переходах
        if (fade > 0.0f) {
            UIDrawFade(&UIx, fade);
            fade = fmaxf(0.0f, fade - dt * 1.2f);
        }

        // Курсор: в игре скрыт, в меню — виден
        if (cursorFree) {
            if (IsCursorHidden()) ShowCursor();
        } else if (!IsCursorHidden()) HideCursor();
        if (!uiOk) TraceLog(LOG_WARNING, "Шрифты интерфейса не загружены!");
    }

    // ------------------------------------------------------------- завершение
    BuildSavePath(&G);
    if (G.screen != GS_TITLE || G.playTime > 5.0f) SaveGame(&G, G.savePath);
    RendererFree(&R);
    AssetsFree(&A);
    AudioShutdown();
    UIFree(&UIx);
    PakClose();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
