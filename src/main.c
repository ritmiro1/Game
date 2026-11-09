// ============================================================================
//  ТИХИЙ ЗАЛИВ — main.c
//  Атмосферный 3D-симулятор рыбалки и исследований. Русский интерфейс.
//
//  Запуск: TikhiyZaliv_v1.0.0.exe            — обычный режим
//          TikhiyZaliv_v1.0.0.exe --safe     — безопасный режим (без звука и пост-эффектов)
//          файл safe.txt рядом с .exe        — то же самое
//  Журнал запуска: startup.log рядом с .exe (или в %APPDATA%\TikhiyZaliv)
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
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// Небольшая часть OpenGL, нужная для журнала (без подключения GL-заголовков)
#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01u
#define GL_VERSION  0x1F02u
#endif
#if defined(_WIN32)
  __declspec(dllimport) const unsigned char *__stdcall glGetString(unsigned int name);
#else
  extern const unsigned char *glGetString(unsigned int name);
#endif

#if defined(_WIN32)
  #include <direct.h>
  // Windows.h не подключаем: он конфликтует с типами raylib (Rectangle, CloseWindow, ShowCursor).
  // Объявляем только то, что нужно для сообщения о фатальной ошибке.
  __declspec(dllimport) int __stdcall MessageBoxA(void *hWnd, const char *text,
                                                  const char *caption, unsigned int uType);
  #ifndef MB_OK
  #define MB_OK 0x00000000u
  #define MB_ICONERROR 0x00000010u
  #endif
#endif

static Game G;
static Assets A;
static Renderer R;
static UI UIx;

static FILE *logFile = NULL;
static bool safeMode = false;
static bool lowMode = false;
static bool uiReady = false;

// ------------------------------------------------------------------ журнал
static void startup_log_open(void) {
    char path[700];
    const char *dir = GetApplicationDirectory();
    snprintf(path, sizeof(path), "%sstartup.log", dir);
    logFile = fopen(path, "w");
    if (!logFile) {
        const char *appdata = getenv("APPDATA");
        if (appdata) {
#if defined(_WIN32)
            char d2[600];
            snprintf(d2, sizeof(d2), "%s\\TikhiyZaliv", appdata);
            _mkdir(d2);
            snprintf(path, sizeof(path), "%s\\startup.log", d2);
            logFile = fopen(path, "w");
#endif
        }
    }
    if (!logFile) logFile = fopen("startup.log", "w");
}

static void trace_to_log(int level, const char *text, va_list args) {
    if (!logFile) return;
    fprintf(logFile, "[R%02d] ", level);
    vfprintf(logFile, text, args);
    fputc('\n', logFile);
    fflush(logFile);
}

static void logf_(const char *fmt, ...) {
    if (!logFile) return;
    fprintf(logFile, "[%8.2f] ", (double)GetTime());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logFile, fmt, ap);
    va_end(ap);
    fputc('\n', logFile);
    fflush(logFile);
}

// ------------------------------------------------------------ экран загрузки
static void boot_frame(const char *text, float progress) {
    BeginDrawing();
    ClearBackground((Color){ 6, 14, 18, 255 });
    if (uiReady) UIDrawLoading(&UIx, text, progress);
    else DrawText("Тихий Залив — загрузка...", 40, 40, 24, (Color){ 226, 178, 96, 255 });
    EndDrawing();   // EndDrawing сам опрашивает события Windows — окно не «зависает»
}

static void on_progress(float fraction, const char *stage) {
    boot_frame(stage, fraction);
}

// -------------------------------------------------------------------- звук
static void audio_frame(Game *g, float dt);

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
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        g->fish_rod.drag = clampf(g->fish_rod.drag + wheel * 0.05f, 0.5f, 1.6f);
    }
}

static void audio_frame(Game *g, float dt) {
    if (!AudioIsAvailable()) return;
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

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strstr(argv[i], "safe") || strstr(argv[i], "nosound")) safeMode = true;
        if (strstr(argv[i], "low")) lowMode = true;
    }
    char safeFile[700];
    snprintf(safeFile, sizeof(safeFile), "%ssafe.txt", GetApplicationDirectory());
    if (FileExists(safeFile)) safeMode = true;

    // Журнал открываем ДО окна: тогда в него попадает информация raylib об OpenGL
    startup_log_open();
    SetTraceLogCallback(trace_to_log);
    SetTraceLogLevel(LOG_INFO);
    logf_("=== ТИХИЙ ЗАЛИВ %s (сборка 3: адаптивное качество) ===", TB_VERSION);
    logf_("Аргументы: safe=%d low=%d", (int)safeMode, (int)lowMode);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "Тихий Залив — 3D рыбалка");
    if (!IsWindowReady()) {
        logf_("ОШИБКА: окно/OpenGL 3.3 недоступны");
#if defined(_WIN32)
        MessageBoxA(NULL,
            "Не удалось создать окно OpenGL 3.3.\n\nОбновите драйвер видеокарты или запустите игру на ПК с поддержкой OpenGL 3.3.",
            "Тихий Залив", MB_ICONERROR | MB_OK);
#endif
        if (logFile) fclose(logFile);
        return 1;
    }
    logf_("Безопасный режим: %s", safeMode ? "да" : "нет");
    logf_("Экран: %dx%d, монитор %dx%d", GetScreenWidth(), GetScreenHeight(),
          GetMonitorWidth(GetCurrentMonitor()), GetMonitorHeight(GetCurrentMonitor()));

    // Не раздуваем окно больше экрана — иначе часть интерфейса уходит за край
    int mw = GetMonitorWidth(GetCurrentMonitor());
    int mh = GetMonitorHeight(GetCurrentMonitor());
    if (mw > 0 && mh > 0 && (GetScreenWidth() > mw - 40 || GetScreenHeight() > mh - 80)) {
        SetWindowSize(mw * 3 / 4, mh * 3 / 4);
        logf_("Окно подогнано под монитор: %dx%d", GetScreenWidth(), GetScreenHeight());
    }
    SetWindowMinSize(640, 400);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    {
        const char *rend = (const char *)glGetString(GL_RENDERER);
        const char *vers = (const char *)glGetString(GL_VERSION);
        logf_("GL renderer: %s", rend ? rend : "?");
        logf_("GL version: %s", vers ? vers : "?");
        if (rend && (strstr(rend, "GDI Generic") || strstr(rend, "llvmpipe") ||
                     strstr(rend, "Software") || strstr(rend, "Basic Render"))) {
            lowMode = true;
            logf_("Обнаружен программный/базовый OpenGL — включён низкий режим");
        }
    }

    boot_frame("Запуск...", 0.02f);
    logf_("Окно создано, первый кадр отрисован");

    PakOpen();
    logf_("PakOpen: %d, путь: %s, файлов: %d", (int)PakCount(), PakGetPath(), PakCount());

    uiReady = UIInit(&UIx);
    logf_("UIInit: %s", uiReady ? "ок" : "шрифты не загружены");
    boot_frame("Интерфейс готов...", 0.10f);

    // --- звук: только после первого кадра и только если устройство живо
    if (!safeMode) {
        boot_frame("Инициализация звука...", 0.14f);
        logf_("InitAudioDevice...");
        InitAudioDevice();
        logf_("InitAudioDevice: готово (device ready = %d)", (int)IsAudioDeviceReady());
        if (IsAudioDeviceReady()) {
            boot_frame("Загрузка звуков и музыки...", 0.18f);
            logf_("AudioInit...");
            bool audioOk = AudioInit();
            logf_("AudioInit: %s", audioOk ? "ок" : "не удалось");
            AudioApplyLevels((AudioLevels){ G.settings.masterVolume, G.settings.musicVolume,
                                            G.settings.sfxVolume, G.settings.ambientVolume });
        } else {
            AudioSetAvailable(false);
            logf_("Звук отключён: устройство недоступно");
        }
    } else {
        AudioSetAvailable(false);
        logf_("Звук отключён безопасным режимом");
    }

    boot_frame("Генерация острова...", 0.22f);
    logf_("SimInit...");
    SimInit(&G, 20251109u);
    int aliveFish = 0;
    for (int i = 0; i < MAX_FISH; i++) if (G.fish[i].alive) aliveFish++;
    logf_("SimInit: ок (рыб %d, деревьев %d, камней %d)", aliveFish, G.terrain.treeCount, G.terrain.rockCount);

    AssetsSetProgressCallback(on_progress);
    boot_frame("Загрузка текстур и шейдеров...", 0.25f);
    logf_("AssetsInit...");
    AssetsInit(&A);
    logf_("AssetsInit: шейдеры lit=%d water=%d sky=%d blur=%d composite=%d",
          A.litOk, A.waterOk, A.skyOk, A.blurOk, A.compositeOk);

    boot_frame("Построение мира...", 0.30f);
    logf_("AssetsBuildWorld...");
    AssetsBuildWorld(&A, &G);
    logf_("AssetsBuildWorld: рельеф %d, батчи %d, рыб %d", A.terrainCount, A.propGroupCount, A.fishModelCount);
    AssetsSetProgressCallback(NULL);

    boot_frame("Подготовка пост-обработки...", 0.98f);
    logf_("RendererInit...");
    RendererInit(&R);
    if (safeMode) RendererSetQuality(&R, 1.0f, 260.0f, false);
    if (lowMode) RendererSetQuality(&R, 0.7f, 110.0f, false);
    logf_("RendererInit: пост-обработка %s, масштаб %.2f",
          R.postEnabled ? "вкл" : "выкл", (double)R.qualityScale);

    // Прогрев: первый кадр 3D-сцены может быть очень дорогим (компиляция шейдеров,
    // заполнение буферов). Делаем его под экраном загрузки и сразу подбираем качество.
    boot_frame("Проверка видеокарты...", 0.99f);
    {
        float q = R.qualityScale;
        double t0 = GetTime();
        RenderWorld(&R, &A, &G, 1.0f / 60.0f);
        double warm = (GetTime() - t0) * 1000.0;
        logf_("Пробный кадр: %.0f мс (пост %d, масштаб %.2f)", warm, (int)R.postEnabled, (double)q);
        if (warm > 900.0) {
            RendererSetQuality(&R, 0.5f, 80.0f, false);
            SetWindowSize(1024, 576);
            logf_("Очень долгий первый кадр — окно 1024x576, минимальное качество");
        } else if (warm > 300.0) {
            RendererSetQuality(&R, 0.7f, 120.0f, false);
        }
        if (!R.postEnabled || R.qualityScale != q) {
            t0 = GetTime();
            RenderWorld(&R, &A, &G, 1.0f / 60.0f);
            logf_("Повторный пробный кадр: %.0f мс (пост %d, масштаб %.2f)",
                  (GetTime() - t0) * 1000.0, (int)R.postEnabled, (double)R.qualityScale);
        }
    }

    G.screen = GS_TITLE;
    BuildSavePath(&G);
    logf_("Сохранение: %s (есть: %d)", G.savePath, (int)SaveExists(G.savePath));
    boot_frame("Готово", 1.0f);
    logf_("Загрузка завершена за %.2f с", (double)GetTime());

    float fade = 1.0f;
    float titleTime = 0.0f;
    bool running = true;
    int frames = 0;
    float avgFrame = 1.0f / 60.0f;

    while (running && !WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;
        titleTime += dt;
        frames++;
        avgFrame = avgFrame * 0.94f + dt * 0.06f;
        if (frames >= 240 && frames % 600 == 0 && avgFrame > 0.042f && R.postEnabled) {
            R.postEnabled = false;
            logf_("Авто-качество: пост-обработка отключена (%.0f FPS)", 1.0f / avgFrame);
        }

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
            if (SaveGame(&G, G.savePath)) Toast(&G, "Сохранено");
            else Toast(&G, "Не удалось сохранить игру");
            logf_("Сохранение: %s", G.savePath);
        }
        if (UIx.loadRequested) {
            unsigned int savedSeed = SavePeekSeed(G.savePath);
            if (savedSeed != 0 && savedSeed != A.builtSeed) {
                Toast(&G, "Сохранение из другого мира — загрузка невозможна");
            } else if (load_existing()) {
                G.screen = GS_PLAY;
                Toast(&G, "Игра загружена");
                logf_("Загрузка сохранения: ок");
            } else {
                Toast(&G, "Сохранение не найдено");
            }
        }
        if (UIx.newGameRequested) {
            logf_("Новая игра");
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
        if (frames <= 3) logf_("кадр %d: отрисовка начата", frames);
        double tRender = GetTime();
        RenderWorld(&R, &A, &G, dt);
        double frameMs = (GetTime() - tRender) * 1000.0;
        if (frames <= 3) logf_("кадр %d: отрисован за %.0f мс", frames, frameMs);
        if (frameMs > 500.0) {
            logf_("Медленный кадр: %.0f мс (пост %d, масштаб %.2f)", frameMs,
                  (int)R.postEnabled, (double)R.qualityScale);
        }
        if (frames == 1 && frameMs > 700.0 && (R.postEnabled || R.qualityScale > 0.95f)) {
            RendererSetQuality(&R, 0.7f, 120.0f, false);
            logf_("Первый кадр медленный (%.0f мс) — включено облегчённое качество", frameMs);
        }
        if (frames == 90 && avgFrame > 0.075f && (R.postEnabled || R.qualityScale > 0.6f)) {
            RendererSetQuality(&R, 0.6f, 90.0f, false);
            logf_("Низкий FPS (%.0f) — качество понижено ещё раз", 1.0f / avgFrame);
        }
        if (frames % 300 == 0) logf_("FPS: %.0f", 1.0f / avgFrame);
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

        if (fade > 0.0f) {
            UIDrawFade(&UIx, fade);
            fade = fmaxf(0.0f, fade - dt * 1.2f);
        }

        if (cursorFree) {
            if (IsCursorHidden()) ShowCursor();
        } else if (!IsCursorHidden()) HideCursor();
    }

    // ------------------------------------------------------------- завершение
    logf_("Выход из игры");
    BuildSavePath(&G);
    if (G.screen != GS_TITLE || G.playTime > 5.0f) SaveGame(&G, G.savePath);
    RendererFree(&R);
    AssetsFree(&A);
    AudioShutdown();
    UIFree(&UIx);
    PakClose();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    logf_("Завершено");
    if (logFile) fclose(logFile);
    return 0;
}
