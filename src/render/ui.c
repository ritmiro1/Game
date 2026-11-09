// ============================================================================
//  ui.c — весь русскоязычный интерфейс: HUD, меню, настройки, журнал, магазин
// ============================================================================
#include "ui.h"
#include "raygui.h"
#include "../platform/pak.h"
#include "../audio/audio.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// ------------------------------------------------------------------- палитра
#define C_PANEL      (Color){ 14, 26, 32, 232 }
#define C_PANEL_SOFT (Color){ 18, 32, 39, 210 }
#define C_BORDER     (Color){ 96, 148, 150, 190 }
#define C_TEXT       (Color){ 232, 238, 235, 255 }
#define C_TEXT_DIM   (Color){ 158, 176, 172, 255 }
#define C_ACCENT     (Color){ 226, 178, 96, 255 }
#define C_ACCENT2    (Color){ 96, 186, 178, 255 }
#define C_RED        (Color){ 208, 84, 72, 255 }
#define C_GREEN      (Color){ 126, 186, 106, 255 }
#define C_DARK       (Color){ 6, 12, 16, 255 }

static float S(float v) { return v * (float)GetScreenWidth() / 1400.0f; }

static const char *weather_name(WeatherKind k) {
    switch (k) {
        case W_CLEAR: return "Ясно";
        case W_CLOUDY: return "Облачно";
        case W_RAIN: return "Дождь";
        case W_FOG: return "Туман";
        case W_STORM: return "Шторм";
        default: return "—";
    }
}

static void txt(Font f, const char *t, float x, float y, float size, Color c) {
    DrawTextEx(f, t, (Vector2){ x, y }, size, size * 0.02f, c);
}

static float txt_w(Font f, const char *t, float size) {
    return MeasureTextEx(f, t, size, size * 0.02f).x;
}

static void panel(Rectangle r, Color fill, Color border) {
    DrawRectangleRounded(r, 0.045f, 6, fill);
    DrawRectangleRoundedLines(r, 0.045f, 6, border);
    DrawRectangleRoundedLines((Rectangle){ r.x + 2, r.y + 2, r.width - 4, r.height - 4 }, 0.045f, 6,
                              (Color){ border.r, border.g, border.b, 40 });
}

static void bar(Rectangle r, float v, Color fill, Color back) {
    DrawRectangleRounded(r, 0.5f, 6, back);
    if (v > 0.001f) {
        Rectangle f = { r.x, r.y, r.width * saturatef(v), r.height };
        if (f.width < r.height) f.width = r.height;
        DrawRectangleRounded(f, 0.5f, 6, fill);
    }
    DrawRectangleRoundedLines(r, 0.5f, 6, (Color){ 0, 0, 0, 90 });
}

static void draw_sky_backdrop(Game *g) {
    // Лёгкая вуаль поверх 3D-сцены в меню
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangleGradientV(0, 0, w, h, (Color){ 6, 16, 22, 190 }, (Color){ 4, 10, 14, 235 });
    float sun = saturatef(g->env.sunDir.y * 2.0f);
    Color glow = { (unsigned char)(226 * sun), (unsigned char)(176 * sun), (unsigned char)(110 * sun), 40 };
    DrawCircleGradient(w - w / 5, h / 5, h * 0.55f, glow, (Color){ 0, 0, 0, 0 });
}

// --------------------------------------------------------------------- шрифты
static int *build_codepoints(int *count) {
    static int cp[600];
    int n = 0;
    for (int c = 32; c < 127; c++) cp[n++] = c;
    for (int c = 0x400; c <= 0x45F; c++) cp[n++] = c;   // кириллица
    cp[n++] = 0x2116;  // №
    cp[n++] = 0x2014;
    cp[n++] = 0x2013;
    cp[n++] = 0x00AB;
    cp[n++] = 0x00BB;
    cp[n++] = 0x2026;
    cp[n++] = 0x00B0;
    cp[n++] = 0x2713;
    cp[n++] = 0x00D7;
    *count = n;
    return cp;
}

static Font load_font(const char *name, int size, int *cps, int cpn) {
    Font f = { 0 };
    unsigned char *data = NULL;
    unsigned int sz = 0;
    if (!PakLoad(name, &data, &sz) || !data) {
        TraceLog(LOG_WARNING, "UI: шрифт не найден: %s", name);
        return GetFontDefault();
    }
    f = LoadFontFromMemory(".ttf", data, (int)sz, size, cps, cpn);
    if (f.texture.id == 0) f = GetFontDefault();
    else SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    free(data);
    return f;
}

bool UIInit(UI *u) {
    memset(u, 0, sizeof(*u));
    int cpn = 0;
    int *cps = build_codepoints(&cpn);
    u->fontUI = load_font("DejaVuSans.ttf", 26, cps, cpn);
    u->fontHeader = load_font("DejaVuSans-Bold.ttf", 34, cps, cpn);
    u->fontTitle = load_font("DejaVuSans-Bold.ttf", 92, cps, cpn);
    u->fontSmall = load_font("DejaVuSans.ttf", 20, cps, cpn);
    u->ready = u->fontUI.texture.id != 0;

    GuiSetFont(u->fontUI);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 24);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 1);
    GuiSetStyle(DEFAULT, BORDER_WIDTH, 1);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x0E1A20FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x16262EFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x1E3A42FF);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x2A5460FF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x608A8CFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0xE2B260FF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xE8EEEBFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xFFFFFFFF);
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, 0x14242BFF);
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, 0x1E3C44FF);
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, 0x2B5A62FF);
    GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, 0x4E7678FF);
    GuiSetStyle(BUTTON, BORDER_COLOR_FOCUSED, 0xE2B260FF);
    GuiSetStyle(SLIDER, BASE_COLOR_NORMAL, 0x18282EFF);
    GuiSetStyle(SLIDER, BASE_COLOR_FOCUSED, 0x1E3C44FF);
    GuiSetStyle(SLIDER, BORDER_COLOR_NORMAL, 0x40646AFF);
    GuiSetStyle(SLIDER, TEXT_COLOR_NORMAL, 0xE8EEEBFF);
    GuiSetStyle(SLIDER, TEXT_COLOR_FOCUSED, 0xFFFFFFFF);
    GuiSetStyle(TOGGLE, BASE_COLOR_NORMAL, 0x16262EFF);
    GuiSetStyle(TOGGLE, BASE_COLOR_FOCUSED, 0x1E3C44FF);
    GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, 0x2E6068FF);
    GuiSetStyle(TOGGLE, TEXT_COLOR_PRESSED, 0xFFFFFFFF);
    GuiSetStyle(CHECKBOX, BASE_COLOR_NORMAL, 0x16262EFF);
    GuiSetStyle(CHECKBOX, BASE_COLOR_FOCUSED, 0x1E3C44FF);
    return u->ready;
}

void UIFree(UI *u) {
    if (u->fontUI.texture.id) UnloadFont(u->fontUI);
    if (u->fontHeader.texture.id) UnloadFont(u->fontHeader);
    if (u->fontTitle.texture.id) UnloadFont(u->fontTitle);
    if (u->fontSmall.texture.id) UnloadFont(u->fontSmall);
    memset(u, 0, sizeof(*u));
}

void UIResetRequests(UI *u) {
    u->saveRequested = false;
    u->quitRequested = false;
    u->newGameRequested = false;
    u->loadRequested = false;
}

// ------------------------------------------------------------------------- HUD
static void draw_crosshair(Game *g) {
    int cx = GetScreenWidth() / 2, cy = GetScreenHeight() / 2;
    float sz = S(4);
    Color c = { 235, 240, 238, 150 };
    DrawCircle(cx, cy, sz * 0.55f, c);
    if (g->fish_rod.phase != FISH_IDLE) return;
    DrawLineEx((Vector2){ cx - sz * 2, cy }, (Vector2){ cx - sz, cy }, 1.4f, c);
    DrawLineEx((Vector2){ cx + sz, cy }, (Vector2){ cx + sz * 2, cy }, 1.4f, c);
}

static void draw_fishing_widgets(UI *u, Game *g) {
    Fishing *f = &g->fish_rod;
    int w = GetScreenWidth(), h = GetScreenHeight();
    float cx = w * 0.5f;
    float by = h - S(210);

    if (f->phase == FISH_CHARGE) {
        Rectangle r = { cx - S(150), by, S(300), S(16) };
        bar(r, f->charge, C_ACCENT, (Color){ 10, 18, 22, 200 });
        txt(u->fontSmall, "Сила заброса", r.x, r.y - S(26), S(19), C_TEXT);
        txt(u->fontSmall, "ЛКМ — держать, отпустить — заброс", r.x, r.y + S(24), S(17), C_TEXT_DIM);
    }
    if (f->phase == FISH_FLOAT) {
        if (f->biteWindow > 0.0f) {
            float pulse = 0.6f + 0.4f * sinf(g->totalTime * 24.0f);
            Rectangle r = { cx - S(180), by - S(30), S(360), S(56) };
            panel(r, (Color){ 90, 24, 18, (unsigned char)(210 * pulse) }, C_ACCENT);
            const char *t = "ПОКЛЁВКА! ЛКМ — подсекать!";
            txt(u->fontHeader, t, cx - txt_w(u->fontHeader, t, S(30)) * 0.5f, r.y + S(14), S(30), C_TEXT);
        } else {
            txt(u->fontSmall, "Ждём поклёвку...", cx - S(70), by, S(19), C_TEXT_DIM);
        }
    }
    if (f->phase == FISH_HOOKED) {
        const FishSpecies *sp = FishGet(f->hookedSpecies);
        Rectangle tR = { cx - S(170), by, S(340), S(18) };
        bar(tR, f->tension, f->tension > 0.82f ? C_RED : (f->tension > 0.6f ? C_ACCENT : C_GREEN),
            (Color){ 10, 18, 22, 200 });
        txt(u->fontSmall, "Натяжение лески", tR.x, tR.y - S(26), S(19), C_TEXT);
        Rectangle pR = { cx - S(170), by + S(40), S(340), S(12) };
        bar(pR, f->reelProgress, C_ACCENT2, (Color){ 10, 18, 22, 200 });
        txt(u->fontSmall, "Вываживание", pR.x, pR.y + S(16), S(17), C_TEXT_DIM);
        const char *hint = "Держите ЛКМ и отпускайте при красном натяжении. Колесо мыши — фрикцион";
        txt(u->fontSmall, hint, cx - txt_w(u->fontSmall, hint, S(16)) * 0.5f, h - S(58), S(16), C_TEXT_DIM);
        char buf[128];
        snprintf(buf, sizeof(buf), "Фрикцион: %.1f", f->drag);
        txt(u->fontSmall, buf, tR.x + tR.width + S(16), tR.y, S(16), C_TEXT_DIM);
        (void)sp;
    }
}

static void draw_achievement_popup(UI *u, Game *g) {
    for (int i = 0; i < MAX_ACHIEVEMENTS; i++) {
        if (g->ach[i].popupTimer <= 0.0f) continue;
        float t = g->ach[i].popupTimer;
        float alpha = saturatef(t / 0.6f) * saturatef((5.0f - t) / 0.4f);
        int w = GetScreenWidth();
        Rectangle r = { w * 0.5f - S(230), S(96), S(460), S(74) };
        Color fill = { 22, 40, 34, (unsigned char)(235 * alpha) };
        panel(r, fill, (Color){ C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, (unsigned char)(220 * alpha) });
        Color tc = C_TEXT; tc.a = (unsigned char)(255 * alpha);
        Color dc = C_ACCENT; dc.a = (unsigned char)(255 * alpha);
        txt(u->fontSmall, "ДОСТИЖЕНИЕ ОТКРЫТО", r.x + S(18), r.y + S(10), S(16), dc);
        txt(u->fontHeader, g->ach[i].title, r.x + S(18), r.y + S(28), S(26), tc);
        txt(u->fontSmall, g->ach[i].desc, r.x + S(18), r.y + S(56), S(15), C_TEXT_DIM);
    }
}

static void draw_catch_popup(UI *u, Game *g) {
    if (!g->caughtPopup) return;
    const FishSpecies *sp = FishGet(g->caughtPopupSpecies);
    int w = GetScreenWidth();
    Rectangle r = { w - S(360), S(180), S(330), S(120) };
    float a = saturatef(g->caughtPopupTimer / 0.5f);
    panel(r, (Color){ 16, 30, 36, (unsigned char)(225 * a) }, C_BORDER);
    txt(u->fontSmall, "В САДКЕ", r.x + S(16), r.y + S(10), S(15), C_ACCENT2);
    txt(u->fontHeader, sp->name, r.x + S(16), r.y + S(30), S(28), C_TEXT);
    char buf[96];
    snprintf(buf, sizeof(buf), "%.2f кг · %d монет/кг", g->caughtPopupWeight, sp->pricePerKg);
    txt(u->fontSmall, buf, r.x + S(16), r.y + S(66), S(17), C_TEXT_DIM);
    int rarity = sp->rarity;
    for (int i = 0; i < 5; i++) {
        Color c = (i < rarity + 1) ? C_ACCENT : (Color){ 60, 70, 72, 200 };
        DrawCircle((int)(r.x + S(20) + i * S(16)), (int)(r.y + S(96)), S(4), c);
    }
}

void UIDrawToast(UI *u, Game *g) {
    if (g->toastTimer <= 0.0f || !g->toast[0]) return;
    int w = GetScreenWidth();
    float a = saturatef(g->toastTimer / 0.5f);
    float tw = txt_w(u->fontSmall, g->toast, S(21)) + S(40);
    Rectangle r = { w * 0.5f - tw * 0.5f, GetScreenHeight() - S(140), tw, S(44) };
    panel(r, (Color){ 12, 24, 30, (unsigned char)(220 * a) }, C_BORDER);
    txt(u->fontSmall, g->toast, r.x + S(20), r.y + S(10), S(21), C_TEXT);
}

void UIDrawHUD(UI *u, Assets *a, Game *g) {
    (void)a;
    int w = GetScreenWidth();
    Environment *e = &g->env;

    // Верхняя левая панель: время, день, погода
    Rectangle tl = { S(18), S(18), S(268), S(96) };
    panel(tl, C_PANEL_SOFT, C_BORDER);
    char buf[128];
    int hour = (int)e->timeOfDay;
    int minute = (int)((e->timeOfDay - hour) * 60.0f);
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    txt(u->fontHeader, buf, tl.x + S(16), tl.y + S(8), S(34), C_TEXT);
    snprintf(buf, sizeof(buf), "День %d · %s", e->day, weather_name(e->weather));
    txt(u->fontSmall, buf, tl.x + S(16), tl.y + S(50), S(18), C_TEXT_DIM);
    snprintf(buf, sizeof(buf), "Ветер %.0f%%  ·  Туман %.0f%%", e->wind * 100.0f, e->fogAmount * 100.0f);
    txt(u->fontSmall, buf, tl.x + S(16), tl.y + S(72), S(15), (Color){ 130, 150, 148, 255 });

    // Монеты
    Rectangle coin = { S(18) + tl.width + S(12), S(18), S(168), S(52) };
    panel(coin, C_PANEL_SOFT, C_BORDER);
    snprintf(buf, sizeof(buf), "Монет: %d", g->inv.coins);
    txt(u->fontSmall, buf, coin.x + S(14), coin.y + S(14), S(19), C_ACCENT);
    // Наживка
    Rectangle bait = { coin.x, coin.y + S(60), S(168), S(40) };
    panel(bait, (Color){ 18, 32, 39, 180 }, (Color){ 96, 148, 150, 120 });
    snprintf(buf, sizeof(buf), "Наживка: %d", g->inv.bait);
    txt(u->fontSmall, buf, bait.x + S(14), bait.y + S(10), S(17), C_TEXT_DIM);

    // Квесты (правая панель)
    float qy = S(18);
    int activeCount = 0;
    for (int i = 0; i < MAX_QUESTS; i++) if (g->quests[i].state == QS_ACTIVE) activeCount++;
    if (activeCount > 0) {
        Rectangle q = { w - S(360), qy, S(342), S(38) + S(46) * (float)(activeCount > 3 ? 3 : activeCount) };
        panel(q, (Color){ 12, 24, 30, 205 }, C_BORDER);
        txt(u->fontSmall, "ЗАДАНИЯ", q.x + S(14), q.y + S(10), S(16), C_ACCENT);
        float yy = q.y + S(36);
        int shown = 0;
        for (int i = 0; i < MAX_QUESTS && shown < 3; i++) {
            Quest *qq = &g->quests[i];
            if (qq->state != QS_ACTIVE) continue;
            txt(u->fontUI, qq->title, q.x + S(14), yy, S(19), C_TEXT);
            snprintf(buf, sizeof(buf), "%d / %d", qq->progress, qq->targetCount);
            txt(u->fontSmall, buf, q.x + q.width - S(16) - txt_w(u->fontSmall, buf, S(17)), yy + S(2), S(17), C_ACCENT2);
            Rectangle pb = { q.x + S(14), yy + S(24), q.width - S(30), S(5) };
            bar(pb, (float)qq->progress / (float)fmaxf(1.0f, (float)qq->targetCount), C_ACCENT2,
                (Color){ 10, 18, 22, 200 });
            yy += S(46);
            shown++;
        }
    }

    // Полоса выносливости
    Rectangle st = { S(18), GetScreenHeight() - S(98), S(230), S(14) };
    bar(st, g->player.stamina, g->player.stamina < 0.25f ? C_RED : C_GREEN, (Color){ 8, 16, 20, 210 });
    txt(u->fontSmall, "Силы", st.x, st.y - S(22), S(16), C_TEXT_DIM);
    if (g->player.health < 0.99f) {
        Rectangle hp = { st.x, st.y + S(26), S(230), S(12) };
        bar(hp, g->player.health, C_RED, (Color){ 8, 16, 20, 210 });
    }

    // Хотбар
    float hbY = GetScreenHeight() - S(66);
    const char *slots[4] = { "1 Удочка", "2 Приманка", "3 Фонарь", "4 Сонар" };
    for (int i = 0; i < 4; i++) {
        Rectangle r = { S(18) + (float)i * S(116), hbY, S(108), S(48) };
        bool active = (g->inv.selectedHotbar == i);
        panel(r, active ? (Color){ 28, 52, 56, 235 } : (Color){ 12, 22, 28, 200 },
              active ? C_ACCENT : (Color){ 80, 120, 122, 140 });
        Color tc = C_TEXT_DIM;
        if (i == 2 && g->lanternOn) tc = C_ACCENT;
        if (i == 3 && g->sonarOn) tc = C_ACCENT2;
        if (i == 2 && !g->inv.hasLantern) tc = (Color){ 90, 100, 100, 180 };
        if (i == 3 && !g->inv.hasSonar) tc = (Color){ 90, 100, 100, 180 };
        txt(u->fontSmall, slots[i], r.x + S(10), r.y + S(14), S(16), tc);
    }

    // Подсказка о взаимодействии
    if (g->interactPrompt[0]) {
        float tw = txt_w(u->fontUI, g->interactPrompt, S(21)) + S(36);
        Rectangle r = { w * 0.5f - tw * 0.5f, GetScreenHeight() - S(160), tw, S(46) };
        panel(r, (Color){ 14, 28, 34, 225 }, C_ACCENT);
        txt(u->fontUI, g->interactPrompt, r.x + S(18), r.y + S(10), S(21), C_TEXT);
    }

    // Подсказки-советы
    if (g->hintTimer > 0.0f && g->hint[0]) {
        float tw = txt_w(u->fontSmall, g->hint, S(18));
        Rectangle r = { w * 0.5f - tw * 0.5f - S(20), GetScreenHeight() - S(232), tw + S(40), S(40) };
        panel(r, (Color){ 10, 20, 26, 200 }, (Color){ 96, 148, 150, 120 });
        txt(u->fontSmall, g->hint, r.x + S(20), r.y + S(10), S(18), C_TEXT_DIM);
    }

    draw_fishing_widgets(u, g);
    draw_catch_popup(u, g);
    draw_achievement_popup(u, g);
    UIDrawToast(u, g);

    // Индикатор режима передвижения
    const char *mode = (g->player.mode == PM_BOAT) ? "В лодке" :
                       (g->player.mode == PM_SWIM ? "Вплавь" : "Пешком");
    txt(u->fontSmall, mode, (float)w - txt_w(u->fontSmall, mode, S(17)) - S(20),
        GetScreenHeight() - S(34), S(17), C_TEXT_DIM);

    if (g->fish_rod.phase == FISH_IDLE && g->player.mode != PM_SWIM)
        draw_crosshair(g);
}

// ---------------------------------------------------------------- главное меню
void UIDrawTitle(UI *u, Assets *a, Game *g) {
    (void)a;
    draw_sky_backdrop(g);
    int w = GetScreenWidth(), h = GetScreenHeight();

    const char *title = "ТИХИЙ ЗАЛИВ";
    float ts = S(92);
    txt(u->fontTitle, title, w * 0.5f - txt_w(u->fontTitle, title, ts) * 0.5f, h * 0.13f, ts, C_TEXT);
    DrawRectangle((int)(w * 0.5f - S(220)), (int)(h * 0.13f + ts * 1.12f), (int)S(440), (int)S(2), C_ACCENT);
    const char *sub = "атмосферный 3D-симулятор рыбалки и исследований";
    txt(u->fontUI, sub, w * 0.5f - txt_w(u->fontUI, sub, S(23)) * 0.5f, h * 0.13f + ts * 1.25f, S(23), C_TEXT_DIM);

    float bw = S(360), bh = S(56), bx = w * 0.5f - bw * 0.5f, by = h * 0.45f;
    GuiSetFont(u->fontUI);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)S(23));

    bool hasSave = SaveExists(g->savePath);
    if (!hasSave) GuiDisable();
    if (GuiButton((Rectangle){ bx, by, bw, bh }, "Продолжить")) {
        AudioPlay2D(SFX_UI_CLICK, 0.8f, 1.0f);
        u->loadRequested = true;
    }
    if (!hasSave) GuiEnable();
    if (GuiButton((Rectangle){ bx, by + S(68), bw, bh }, "Новая игра")) {
        AudioPlay2D(SFX_UI_CLICK, 0.8f, 1.0f);
        u->newGameRequested = true;
    }
    if (GuiButton((Rectangle){ bx, by + S(136), bw, bh }, "Настройки")) {
        AudioPlay2D(SFX_UI_CLICK, 0.8f, 1.0f);
        g->prevScreen = GS_TITLE;
        g->screen = GS_SETTINGS;
    }
    if (GuiButton((Rectangle){ bx, by + S(204), bw, bh }, "Достижения")) {
        AudioPlay2D(SFX_UI_CLICK, 0.8f, 1.0f);
        g->prevScreen = GS_TITLE;
        g->screen = GS_ACHIEVEMENTS;
    }
    if (GuiButton((Rectangle){ bx, by + S(272), bw, bh }, "Выход")) {
        AudioPlay2D(SFX_UI_CLICK, 0.8f, 1.0f);
        u->quitRequested = true;
    }

    const char *hint = "WASD — движение · ЛКМ — заброс и подмотка · E — действие · F — лодка · J — журнал · Esc — пауза";
    txt(u->fontSmall, hint, w * 0.5f - txt_w(u->fontSmall, hint, S(16)) * 0.5f, h - S(46), S(16), C_TEXT_DIM);
    const char *ver = "Версия " TB_VERSION " · 100 концептов 3D-игр в комплекте";
    txt(u->fontSmall, ver, w * 0.5f - txt_w(u->fontSmall, ver, S(15)) * 0.5f, h - S(26), S(15),
        (Color){ 110, 128, 126, 255 });
}

// --------------------------------------------------------------------- пауза
void UIDrawPause(UI *u, Assets *a, Game *g) {
    (void)a;
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, (Color){ 4, 10, 14, 190 });
    Rectangle p = { w * 0.5f - S(240), h * 0.5f - S(230), S(480), S(460) };
    panel(p, C_PANEL, C_BORDER);
    const char *t = "ПАУЗА";
    txt(u->fontHeader, t, p.x + p.width * 0.5f - txt_w(u->fontHeader, t, S(40)) * 0.5f, p.y + S(22), S(40), C_TEXT);

    float bw = S(400), bh = S(54), bx = p.x + p.width * 0.5f - bw * 0.5f, by = p.y + S(92);
    GuiSetFont(u->fontUI);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)S(22));
    if (GuiButton((Rectangle){ bx, by, bw, bh }, "Продолжить")) g->screen = GS_PLAY;
    if (GuiButton((Rectangle){ bx, by + S(64), bw, bh }, "Сохранить игру")) {
        u->saveRequested = true;
        Toast(g, "Игра сохранена");
    }
    if (GuiButton((Rectangle){ bx, by + S(128), bw, bh }, "Настройки")) {
        g->prevScreen = GS_PAUSE;
        g->screen = GS_SETTINGS;
    }
    if (GuiButton((Rectangle){ bx, by + S(192), bw, bh }, "Журнал")) {
        g->prevScreen = GS_PAUSE;
        g->screen = GS_JOURNAL;
    }
    if (GuiButton((Rectangle){ bx, by + S(256), bw, bh }, "Выйти в главное меню")) {
        u->saveRequested = true;
        g->screen = GS_TITLE;
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "Время в игре: %d мин · Поймано рыб: %d · Достижений: %d/%d",
             (int)(g->playTime / 60.0f), g->fishCaught,
             ({ int c = 0; for (int i = 0; i < MAX_ACHIEVEMENTS; i++) if (g->ach[i].unlocked) c++; c; }),
             MAX_ACHIEVEMENTS);
    txt(u->fontSmall, buf, p.x + S(20), p.y + p.height - S(46), S(16), C_TEXT_DIM);
}

// ------------------------------------------------------------------ настройки
void UIDrawSettings(UI *u, Assets *a, Game *g) {
    (void)a;
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, (Color){ 4, 10, 14, 205 });
    Rectangle p = { w * 0.5f - S(430), h * 0.5f - S(320), S(860), S(640) };
    panel(p, C_PANEL, C_BORDER);
    txt(u->fontHeader, "НАСТРОЙКИ", p.x + S(26), p.y + S(18), S(34), C_TEXT);
    txt(u->fontSmall, "Все изменения применяются сразу", p.x + S(26), p.y + S(58), S(16), C_TEXT_DIM);

    GuiSetFont(u->fontUI);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)S(20));
    float colW = S(390);
    float x1 = p.x + S(26), x2 = p.x + S(26) + colW + S(28);
    float y = p.y + S(96);

    // Качество графики
    txt(u->fontSmall, "Качество графики", x1, y, S(18), C_ACCENT2);
    int q = g->settings.quality;
    if (GuiToggleGroup((Rectangle){ x1, y + S(26), colW, S(42) }, "Низкое;Среднее;Высокое;Ультра", &q) >= 0) {
        g->settings.quality = q;
        g->settings.tiltShift = q >= 1;
        g->settings.bloom = q >= 1;
        g->settings.rainEffects = q >= 1;
        g->settings.shadowQuality = q;
        if (q == 0) { g->settings.tiltShift = false; g->settings.bloom = false; }
    }

    y += S(88);
    txt(u->fontSmall, "Общая громкость", x1, y, S(18), C_ACCENT2);
    GuiSliderBar((Rectangle){ x1, y + S(26), colW, S(30) }, NULL, NULL, &g->settings.masterVolume, 0.0f, 1.0f);
    y += S(72);
    txt(u->fontSmall, "Музыка", x1, y, S(18), C_ACCENT2);
    GuiSliderBar((Rectangle){ x1, y + S(26), colW, S(30) }, NULL, NULL, &g->settings.musicVolume, 0.0f, 1.0f);
    y += S(72);
    txt(u->fontSmall, "Звуковые эффекты", x1, y, S(18), C_ACCENT2);
    GuiSliderBar((Rectangle){ x1, y + S(26), colW, S(30) }, NULL, NULL, &g->settings.sfxVolume, 0.0f, 1.0f);
    y += S(72);
    txt(u->fontSmall, "Эмбиент (вода, ветер, дождь)", x1, y, S(18), C_ACCENT2);
    GuiSliderBar((Rectangle){ x1, y + S(26), colW, S(30) }, NULL, NULL, &g->settings.ambientVolume, 0.0f, 1.0f);

    float y2 = p.y + S(96);
    txt(u->fontSmall, "Чувствительность мыши", x2, y2, S(18), C_ACCENT2);
    GuiSliderBar((Rectangle){ x2, y2 + S(26), colW, S(30) }, NULL, NULL, &g->settings.sensitivity, 0.2f, 3.0f);
    y2 += S(72);
    txt(u->fontSmall, "Угол обзора (FOV)", x2, y2, S(18), C_ACCENT2);
    GuiSliderBar((Rectangle){ x2, y2 + S(26), colW, S(30) }, NULL, NULL, &g->settings.fov, 55.0f, 95.0f);
    y2 += S(80);

    GuiCheckBox((Rectangle){ x2, y2, S(30), S(30) }, "Инверсия мыши по Y", &g->settings.invertY);
    y2 += S(44);
    GuiCheckBox((Rectangle){ x2, y2, S(30), S(30) }, "Блум и свечение", &g->settings.bloom);
    y2 += S(44);
    GuiCheckBox((Rectangle){ x2, y2, S(30), S(30) }, "Кинематографичный Tilt-Shift", &g->settings.tiltShift);
    y2 += S(44);
    GuiCheckBox((Rectangle){ x2, y2, S(30), S(30) }, "Эффекты дождя", &g->settings.rainEffects);
    y2 += S(44);
    GuiCheckBox((Rectangle){ x2, y2, S(30), S(30) }, "Покачивание камеры при ходьбе", &g->settings.headBob);
    y2 += S(58);

    txt(u->fontSmall, "Управление: WASD — ходьба · Shift — бег · Space — прыжок · C — присесть",
        p.x + S(26), p.y + p.height - S(92), S(16), C_TEXT_DIM);
    txt(u->fontSmall, "ЛКМ — заброс/подмотка · ПКМ — отмена · E — действие · F — лодка · L — фонарь · J — журнал",
        p.x + S(26), p.y + p.height - S(70), S(16), C_TEXT_DIM);

    if (GuiButton((Rectangle){ p.x + p.width - S(210), p.y + p.height - S(56), S(184), S(42) }, "Назад")) {
        AudioPlay2D(SFX_UI_CLICK, 0.7f, 1.0f);
        g->screen = (g->prevScreen == GS_PAUSE) ? GS_PAUSE : (g->prevScreen == GS_TITLE ? GS_TITLE : GS_PLAY);
    }
}

// -------------------------------------------------------------------- журнал
static void draw_table_header(UI *u, float x, float y, float w, const char *c1, const char *c2) {
    txt(u->fontSmall, c1, x, y, S(16), C_ACCENT2);
    txt(u->fontSmall, c2, x + w - txt_w(u->fontSmall, c2, S(16)), y, S(16), C_ACCENT2);
    DrawRectangle((int)x, (int)(y + S(22)), (int)w, 1, (Color){ 96, 148, 150, 90 });
}

void UIDrawJournal(UI *u, Assets *a, Game *g) {
    (void)a;
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, (Color){ 4, 10, 14, 210 });
    Rectangle p = { w * 0.5f - S(470), h * 0.5f - S(320), S(940), S(640) };
    panel(p, C_PANEL, C_BORDER);
    txt(u->fontHeader, "ЖУРНАЛ РЫБАКА", p.x + S(26), p.y + S(16), S(32), C_TEXT);

    const char *tabs = "Улов;Задания;Достижения;Энциклопедия";
    int t = u->journalTab;
    GuiSetFont(u->fontUI);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)S(19));
    t = GuiToggleGroup((Rectangle){ p.x + S(26), p.y + S(64), S(620), S(38) }, tabs, &t);
    u->journalTab = t;

    float cx = p.x + S(26), cy = p.y + S(120), cw = p.width - S(52);
    char buf[256];

    if (u->journalTab == 0) {
        draw_table_header(u, cx, cy, cw, "Рыба в садке", "Вес / цена");
        float y = cy + S(34);
        int shown = 0;
        int total = 0;
        for (int i = g->inv.count - 1; i >= 0 && shown < 11; i--) {
            Item *it = &g->inv.items[i];
            if (it->kind != IT_FISH) continue;
            const FishSpecies *sp = FishGet(it->fishSpecies);
            txt(u->fontUI, sp->name, cx, y, S(20), C_TEXT);
            snprintf(buf, sizeof(buf), "%.2f кг · %d мон.", it->weightKg, (int)(it->weightKg * sp->pricePerKg));
            txt(u->fontSmall, buf, cx + cw - txt_w(u->fontSmall, buf, S(17)), y + S(2), S(17), C_ACCENT);
            y += S(32);
            shown++;
        }
        int fishTotal = InventoryFishValue(g);
        (void)total;
        if (shown == 0) txt(u->fontUI, "Садок пуст. Самое время выйти на воду.", cx, y, S(20), C_TEXT_DIM);
        snprintf(buf, sizeof(buf), "Всего рыб: %d · Общая стоимость: %d монет", g->fishCaught, fishTotal);
        txt(u->fontSmall, buf, cx, p.y + p.height - S(48), S(18), C_TEXT_DIM);
    } else if (u->journalTab == 1) {
        float y = cy;
        for (int i = 0; i < MAX_QUESTS; i++) {
            Quest *q = &g->quests[i];
            if (q->state == QS_LOCKED) continue;
            Color c = q->state == QS_DONE ? C_GREEN : C_TEXT;
            txt(u->fontUI, q->title, cx, y, S(21), c);
            snprintf(buf, sizeof(buf), "%s", q->state == QS_DONE ? "выполнено" :
                     (q->progress >= q->targetCount ? "готово к сдаче" : "в работе"));
            txt(u->fontSmall, buf, cx + cw - txt_w(u->fontSmall, buf, S(16)), y + S(2), S(16),
                q->state == QS_DONE ? C_GREEN : C_ACCENT2);
            y += S(28);
            txt(u->fontSmall, q->desc, cx + S(8), y, S(17), C_TEXT_DIM);
            y += S(24);
            snprintf(buf, sizeof(buf), "Прогресс: %d / %d · Награда: %d монет", q->progress, q->targetCount, q->rewardCoins);
            txt(u->fontSmall, buf, cx + S(8), y, S(16), (Color){ 130, 150, 148, 255 });
            y += S(36);
            if (y > p.y + p.height - S(60)) break;
        }
    } else if (u->journalTab == 2) {
        float y = cy;
        int unlocked = 0;
        for (int i = 0; i < MAX_ACHIEVEMENTS; i++) {
            Achievement *ach = &g->ach[i];
            if (ach->unlocked) unlocked++;
        }
        snprintf(buf, sizeof(buf), "Открыто %d из %d", unlocked, MAX_ACHIEVEMENTS);
        txt(u->fontSmall, buf, cx, y, S(17), C_ACCENT2);
        y += S(28);
        for (int i = 0; i < MAX_ACHIEVEMENTS; i++) {
            Achievement *ach = &g->ach[i];
            Color c = ach->unlocked ? C_ACCENT : (Color){ 110, 124, 122, 255 };
            txt(u->fontUI, ach->title, cx, y, S(19), c);
            txt(u->fontSmall, ach->desc, cx + S(300), y + S(2), S(16), ach->unlocked ? C_TEXT_DIM : (Color){ 84, 96, 94, 255 });
            if (!ach->unlocked) {
                snprintf(buf, sizeof(buf), "%.0f%%", ach->target > 0 ? saturatef(ach->progress / ach->target) * 100.0f : 0.0f);
                txt(u->fontSmall, buf, cx + cw - txt_w(u->fontSmall, buf, S(16)), y + S(2), S(16), (Color){ 110, 124, 122, 255 });
            }
            y += S(30);
            if (y > p.y + p.height - S(60)) break;
        }
    } else {
        float y = cy;
        snprintf(buf, sizeof(buf), "Видов открыто: %d из %d", ({ int c = 0; for (int i = 0; i < FishSpeciesCount(); i++) if (g->fishSpecies[i] > 0) c++; c; }), FishSpeciesCount());
        txt(u->fontSmall, buf, cx, y, S(17), C_ACCENT2);
        y += S(30);
        for (int i = 0; i < FishSpeciesCount(); i++) {
            const FishSpecies *sp = FishGet(i);
            bool known = g->fishSpecies[i] > 0;
            txt(u->fontUI, known ? sp->name : "???", cx, y, S(19), known ? C_TEXT : (Color){ 96, 110, 108, 255 });
            if (known) {
                snprintf(buf, sizeof(buf), "%.1f-%.1f кг · глубина %.0f-%.0f м · %02d:00-%02d:00",
                         sp->minKg, sp->maxKg, sp->depthMin, sp->depthMax, sp->hourFrom, sp->hourTo);
                txt(u->fontSmall, buf, cx + S(220), y + S(4), S(14), C_TEXT_DIM);
            } else {
                txt(u->fontSmall, "ещё не поймана", cx + S(220), y + S(4), S(14), (Color){ 96, 110, 108, 255 });
            }
            y += S(28);
            if (y > p.y + p.height - S(60)) break;
        }
    }

    if (GuiButton((Rectangle){ p.x + p.width - S(200), p.y + p.height - S(56), S(174), S(42) }, "Закрыть (J)")) {
        AudioPlay2D(SFX_UI_CLICK, 0.7f, 1.0f);
        g->screen = (g->prevScreen == GS_PAUSE) ? GS_PAUSE : GS_PLAY;
    }
}

// -------------------------------------------------------------------- магазин
void UIDrawShop(UI *u, Assets *a, Game *g) {
    (void)a;
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, (Color){ 4, 10, 14, 205 });
    Rectangle p = { w * 0.5f - S(420), h * 0.5f - S(300), S(840), S(600) };
    panel(p, C_PANEL, C_BORDER);
    txt(u->fontHeader, "ЛАВКА АНИ", p.x + S(26), p.y + S(16), S(32), C_TEXT);
    char buf[160];
    snprintf(buf, sizeof(buf), "Монеты: %d · Рыба в садке: %d", g->inv.coins, g->inv.count);
    txt(u->fontSmall, buf, p.x + S(26), p.y + S(58), S(18), C_ACCENT);

    GuiSetFont(u->fontUI);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)S(18));
    float y = p.y + S(98);
    int n = ShopItemCount();
    for (int i = 0; i < n; i++) {
        bool owned = ShopItemOwned(g, i);
        Rectangle row = { p.x + S(22), y, p.width - S(44), S(46) };
        DrawRectangleRounded(row, 0.2f, 4, owned ? (Color){ 16, 30, 30, 160 } : (Color){ 20, 36, 42, 200 });
        txt(u->fontUI, ShopItemName(i), row.x + S(14), row.y + S(8), S(19), owned ? C_TEXT_DIM : C_TEXT);
        txt(u->fontSmall, ShopItemDesc(i), row.x + S(14), row.y + S(28), S(14), C_TEXT_DIM);
        snprintf(buf, sizeof(buf), "%d", ShopItemPrice(i));
        txt(u->fontSmall, buf, row.x + row.width - S(140), row.y + S(14), S(18), C_ACCENT);
        char label[32];
        snprintf(label, sizeof(label), owned ? "Куплено" : "Купить");
        if (!owned && GuiButton((Rectangle){ row.x + row.width - S(108), row.y + S(7), S(96), S(32) }, label)) {
            if (ShopBuy(g, i)) AudioPlay2D(SFX_COIN, 0.9f, 1.0f);
            else AudioPlay2D(SFX_UI_CLICK, 0.6f, 0.8f);
        }
        y += S(52);
    }

    float sellY = p.y + p.height - S(112);
    snprintf(buf, sizeof(buf), "Сдать всю рыбу (+%d монет)", InventoryFishValue(g));
    if (GuiButton((Rectangle){ p.x + S(24), sellY, S(400), S(46) }, buf)) {
        InventorySellAllFish(g);
        AudioPlay2D(SFX_COIN, 0.9f, 1.05f);
    }
    if (GuiButton((Rectangle){ p.x + p.width - S(210), sellY, S(186), S(46) }, "Уйти")) {
        AudioPlay2D(SFX_UI_CLICK, 0.7f, 1.0f);
        g->screen = GS_PLAY;
    }
    txt(u->fontSmall, "Аня: «Свежая рыба — всегда в цене. Трофейную беру дороже.»",
        p.x + S(24), p.y + p.height - S(54), S(16), C_TEXT_DIM);
}

// -------------------------------------------------------------------- диалог
void UIDrawDialog(UI *u, Assets *a, Game *g) {
    (void)a;
    int w = GetScreenWidth(), h = GetScreenHeight();
    Npc *n = (g->dialogNpc >= 0 && g->dialogNpc < MAX_NPC) ? &g->npc[g->dialogNpc] : NULL;
    Rectangle p = { w * 0.5f - S(480), h - S(250), S(960), S(200) };
    DrawRectangle(0, 0, w, h, (Color){ 4, 10, 14, 90 });
    panel(p, (Color){ 14, 26, 32, 244 }, C_BORDER);
    if (n) {
        txt(u->fontHeader, n->name, p.x + S(24), p.y + S(14), S(26), C_ACCENT);
        txt(u->fontSmall, n->role, p.x + S(24) + txt_w(u->fontHeader, n->name, S(26)) + S(16),
            p.y + S(22), S(16), C_TEXT_DIM);
    }
    int line = g->dialogLine;
    if (line < 0) line = 0;
    if (line >= MAX_DIALOG) line = MAX_DIALOG - 1;
    const char *text = g->dialogText[line];
    // Перенос длинных реплик
    float maxW = p.width - S(48);
    float fs = S(20);
    char chunk[512];
    int ci = 0;
    float y = p.y + S(58);
    for (const char *ptr = text; *ptr && ci < (int)sizeof(chunk) - 1; ptr++) {
        chunk[ci++] = *ptr;
        chunk[ci] = 0;
        if (txt_w(u->fontUI, chunk, fs) > maxW || *ptr == '\n') {
            txt(u->fontUI, chunk, p.x + S(24), y, fs, C_TEXT);
            y += fs * 1.35f;
            ci = 0;
            chunk[0] = 0;
        }
    }
    if (ci > 0) txt(u->fontUI, chunk, p.x + S(24), y, fs, C_TEXT);

    int total = 0;
    for (int i = 0; i < MAX_DIALOG; i++) if (g->dialogText[i][0]) total = i + 1;
    char buf[64];
    snprintf(buf, sizeof(buf), "%d / %d", line + 1, total > 0 ? total : 1);
    txt(u->fontSmall, buf, p.x + p.width - S(110), p.y + p.height - S(40), S(16), C_TEXT_DIM);
    txt(u->fontSmall, "E — далее", p.x + S(24), p.y + p.height - S(40), S(17), C_ACCENT);
}

// --------------------------------------------------------------- достижения
void UIDrawAchievements(UI *u, Assets *a, Game *g) {
    (void)a;
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, (Color){ 4, 10, 14, 215 });
    Rectangle p = { w * 0.5f - S(420), h * 0.5f - S(300), S(840), S(600) };
    panel(p, C_PANEL, C_BORDER);
    txt(u->fontHeader, "ДОСТИЖЕНИЯ", p.x + S(26), p.y + S(16), S(32), C_TEXT);
    float y = p.y + S(78);
    for (int i = 0; i < MAX_ACHIEVEMENTS; i++) {
        Achievement *ach = &g->ach[i];
        Color c = ach->unlocked ? C_ACCENT : (Color){ 120, 132, 130, 255 };
        DrawCircle((int)(p.x + S(34)), (int)(y + S(12)), S(5), c);
        txt(u->fontUI, ach->title, p.x + S(54), y, S(20), c);
        txt(u->fontSmall, ach->desc, p.x + S(54), y + S(24), S(15), C_TEXT_DIM);
        if (ach->unlocked) txt(u->fontSmall, "открыто", p.x + p.width - S(120), y + S(4), S(16), C_GREEN);
        y += S(46);
    }
    if (GuiButton((Rectangle){ p.x + p.width - S(200), p.y + p.height - S(56), S(174), S(42) }, "Назад")) {
        g->screen = (g->prevScreen == GS_TITLE) ? GS_TITLE : GS_PAUSE;
    }
}

// -------------------------------------------------------------- загрузка/фейд
void UIDrawLoading(UI *u, const char *text, float progress) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    ClearBackground((Color){ 6, 14, 18, 255 });
    const char *title = "ТИХИЙ ЗАЛИВ";
    txt(u->fontTitle, title, w * 0.5f - txt_w(u->fontTitle, title, S(80)) * 0.5f, h * 0.32f, S(80),
        (Color){ 226, 232, 228, 255 });
    txt(u->fontUI, text, w * 0.5f - txt_w(u->fontUI, text, S(22)) * 0.5f, h * 0.58f, S(22), C_TEXT_DIM);
    Rectangle r = { w * 0.5f - S(240), h * 0.66f, S(480), S(10) };
    bar(r, progress, C_ACCENT, (Color){ 16, 26, 30, 255 });
    const char *tip = "Совет: в тумане и ночью клюёт совсем другая рыба";
    txt(u->fontSmall, tip, w * 0.5f - txt_w(u->fontSmall, tip, S(16)) * 0.5f, h * 0.74f, S(16),
        (Color){ 110, 128, 126, 255 });
}

void UIDrawFade(UI *u, float alpha) {
    (void)u;
    if (alpha <= 0.001f) return;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 4, 10, 14, (unsigned char)(clampf(alpha, 0, 1) * 255) });
}
