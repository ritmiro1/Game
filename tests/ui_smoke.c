// Безэкранный прогон всей игры: модели, рендер-пайплайн, интерфейс, звук, сохранения.
// raylib заменён заглушками, поэтому проверяются только вычисления и жизненный цикл.
#include "raylib.h"

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
#include <stdlib.h>
#include <time.h>

static Game G;
static Assets A;
static Renderer R;
static UI UIx;

static int fails = 0;
static void check(bool cond, const char *what) {
    if (!cond) { printf("  [FAIL] %s\n", what); fails++; }
    else printf("  [ ok ] %s\n", what);
}

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static void frame_all(void) {
    double t0 = now_s();
    RenderWorld(&R, &A, &G, 1.0f / 60.0f);
    switch (G.screen) {
        case GS_TITLE: UIDrawTitle(&UIx, &A, &G); break;
        case GS_PLAY: UIDrawHUD(&UIx, &A, &G); break;
        case GS_PAUSE: UIDrawHUD(&UIx, &A, &G); UIDrawPause(&UIx, &A, &G); break;
        case GS_SETTINGS: UIDrawSettings(&UIx, &A, &G); break;
        case GS_JOURNAL: UIDrawJournal(&UIx, &A, &G); break;
        case GS_SHOP: UIDrawShop(&UIx, &A, &G); break;
        case GS_DIALOG: UIDrawDialog(&UIx, &A, &G); break;
        case GS_ACHIEVEMENTS: UIDrawAchievements(&UIx, &A, &G); break;
        default: break;
    }
    UIDrawFade(&UIx, 0.4f);
    UIResetRequests(&UIx);
    double dtm = now_s() - t0;
    if (dtm > 0.5) printf("  [slow] кадр занял %.2f с (экран %d)\n", dtm, (int)G.screen);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Безэкранный прогон «Тихий Залив» ===\n");
    InitWindow(1400, 900, "test");
    InitAudioDevice();

    bool pakOk = PakOpen();
    printf("PAK: %s, файлов %d\n", PakGetPath(), PakCount());
    check(pakOk && PakCount() > 50, "архив ресурсов открыт и проиндексирован");

    unsigned char *data = NULL; unsigned int size = 0;
    check(PakLoad("DejaVuSans.ttf", &data, &size) && size > 1000, "шрифт читается из архива");
    free(data); data = NULL;
    check(PakLoad("tex_sand.png", &data, &size) && size > 500, "текстура читается из архива");
    free(data); data = NULL;
    check(PakLoad("sfx_cast.wav", &data, &size) && size > 500, "звук читается из архива");
    free(data);
    check(!PakLoad("нет_такого_файла.png", &data, &size), "отсутствующий файл корректно не найден");

    SimInit(&G, 20251109u);
    int aliveFish = 0;
    for (int i = 0; i < MAX_FISH; i++) if (G.fish[i].alive) aliveFish++;
    printf("Живых рыб в мире: %d\n", aliveFish);
    check(aliveFish > MAX_FISH / 2, "рыбы созданы");

    UIInit(&UIx);
    check(UIx.ready, "интерфейс инициализирован");

    AssetsInit(&A);
    AssetsBuildWorld(&A, &G);
    check(A.terrainCount > 0, "рельеф построен");
    check(A.propGroupCount > 0, "растительность собрана в батчи");
    check(A.fishModelCount == 13, "13 моделей рыб");
    check(A.humanParts[0].meshCount > 0, "части персонажа построены");

    RendererInit(&R);
    printf("Пост-обработка: %s\n", R.postEnabled ? "вкл" : "выкл (нет GL — ожидаемо)");

    // Все экраны по несколько кадров
    G.screen = GS_TITLE;
    for (int i = 0; i < 5; i++) frame_all();
    check(true, "титульный экран отрисован");

    G.screen = GS_PLAY;
    for (int i = 0; i < 30; i++) { SimUpdateIdle(&G, 1.0f / 60.0f); frame_all(); }
    check(true, "HUD отрисован");

    // Рыбалка: заброс -> поклёвка -> подсечка -> вываживание
    G.player.pos = v3(G.terrain.lm.boatStart.x, 0.6f, G.terrain.lm.boatStart.z);
    G.player.mode = PM_FOOT;
    for (int i = 0; i < 4; i++) SimUpdate(&G, 1.0f / 60.0f, IN_BOARD, v3(0, 0, 0), v3(0, 0, 0));
    check(G.player.mode == PM_BOAT, "посадка в лодку");
    G.player.yaw = G.terrain.lm.boatYaw + 1.2f;
    G.player.pitch = -0.05f;
    int caught0 = G.fishCaught;
    int hookedOk = 0;
    for (int attempt = 0; attempt < 6 && !hookedOk; attempt++) {
        SimUpdate(&G, 1.0f / 60.0f, IN_FISH, v3(0, 0, 0), v3(0, 0, 0));
        for (int i = 0; i < 70; i++) {
            SimUpdate(&G, 1.0f / 60.0f, IN_FISH, v3(0, 0, 0), v3(0, 0, 0));
            frame_all();
        }
        SimUpdate(&G, 1.0f / 60.0f, 0, v3(0, 0, 0), v3(0, 0, 0));
        for (int i = 0; i < 2400; i++) {
            SimUpdate(&G, 1.0f / 60.0f, 0, v3(0, 0, 0), v3(0, 0, 0));
            frame_all();
            if (G.fish_rod.biteWindow > 0.0f || G.fish_rod.phase == FISH_IDLE) break;
        }
        if (G.fish_rod.biteWindow > 0.0f) {
            SimUpdate(&G, 1.0f / 60.0f, IN_FISH, v3(0, 0, 0), v3(0, 0, 0));
            if (G.fish_rod.phase == FISH_HOOKED) hookedOk = 1;
        }
    }
    check(hookedOk == 1, "поклёвка и подсечка работают");
    if (hookedOk) {
        for (int i = 0; i < 60 * 120; i++) {
            int in = ((G.fish_rod.tension < 0.72f) && (i % 3 != 2)) ? IN_FISH : 0;
            SimUpdate(&G, 1.0f / 60.0f, in, v3(0, 0, 0), v3(0, 0, 0));
            frame_all();
            if (G.fishCaught > caught0 || G.fish_rod.phase == FISH_IDLE) break;
        }
    }
    printf("Поймано рыб в прогоне: %d (вид %s), монет %d\n", G.fishCaught - caught0,
           FishGet(G.fish_rod.lastCatchSpecies) ? FishGet(G.fish_rod.lastCatchSpecies)->name : "-",
           G.inv.coins);
    check(G.fishCaught > caught0, "рыба поймана через полный цикл интерфейса");
    check(G.inv.count >= 0 && G.inv.count <= MAX_ITEMS, "инвентарь в допустимых границах");

    // Магазин
    G.screen = GS_SHOP;
    for (int i = 0; i < 4; i++) frame_all();
    check(true, "магазин отрисован");

    // Журнал по всем вкладкам
    G.screen = GS_JOURNAL;
    for (int t = 0; t < 4; t++) { G.journalTab = t; frame_all(); frame_all(); }
    check(true, "журнал: 4 вкладки отрисованы");

    // Диалог с NPC
    G.screen = GS_DIALOG;
    G.dialogActive = true;
    NpcTalkBegin(&G, 0);
    for (int i = 0; i < 40; i++) {
        if (i % 6 == 5) NpcTalkAdvance(&G);
        frame_all();
        if (!G.dialogActive) NpcTalkBegin(&G, 1);
    }
    G.dialogActive = false;
    check(true, "диалоги NPC отрисованы");

    // Настройки, пауза, достижения
    G.screen = GS_SETTINGS; for (int i = 0; i < 3; i++) frame_all();
    G.screen = GS_PAUSE; for (int i = 0; i < 3; i++) frame_all();
    G.screen = GS_ACHIEVEMENTS; for (int i = 0; i < 3; i++) frame_all();
    check(true, "настройки, пауза и достижения отрисованы");

    // Ночь, шторм, подводная камера
    G.screen = GS_PLAY;
    G.env.timeOfDay = 1.5f; G.env.weather = W_STORM; G.env.rainAmount = 1.0f;
    G.underwaterCam = true;
    for (int i = 0; i < 20; i++) { SimUpdateIdle(&G, 1.0f / 60.0f); frame_all(); }
    check(true, "ночь/шторм/под водой отрисованы");
    G.underwaterCam = false;
    G.env.timeOfDay = 12.0f; G.env.weather = W_FOG;
    for (int i = 0; i < 10; i++) frame_all();

    // Звук
    AudioInit();
    AudioLevels lv = { 0.8f, 0.6f, 0.9f, 0.7f };
    AudioApplyLevels(lv);
    for (int i = 0; i < 60; i++) AudioUpdate(1.0f / 60.0f, (Vector3){ 0, 2, 0 }, (Vector3){ 0, 0, -1 }, 0.4f, 0.2f, 0.0f, 0.8f, 0.1f, 0.0f, false, 0.0f);
    AudioSetMusic(MUS_CALM);
    AudioSetMusic(MUS_NIGHT);
    AudioSetMusic(MUS_TENSION);
    AudioPlay2D(SFX_UI_CLICK, 0.7f, 1.0f);
    check(true, "звуковая подсистема работает");

    // Сохранение и загрузка
    BuildSavePath(&G);
    G.inv.coins = 777;
    bool saved = SaveGame(&G, G.savePath);
    int coinsBefore = G.inv.coins;
    G.inv.coins = 1;
    bool loaded = LoadGame(&G, G.savePath);
    check(saved && loaded, "сохранение/загрузка безэкранно");
    check(G.inv.coins == coinsBefore, "монеты восстановлены");

    // Долгий прогон: 600 кадров всех экранов
    for (int i = 0; i < 600; i++) {
        G.screen = (GameScreen)(i % 9);
        SimUpdateIdle(&G, 1.0f / 60.0f);
        frame_all();
    }
    check(true, "600 кадров без падений");

    RendererFree(&R);
    AssetsFree(&A);
    AudioShutdown();
    UIFree(&UIx);
    PakClose();
    CloseAudioDevice();
    CloseWindow();

    printf("\n=== ИТОГ: %s (%d ошибок) ===\n", fails ? "ПРОВАЛ" : "УСПЕХ", fails);
    return fails ? 1 : 0;
}
