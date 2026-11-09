#ifndef TB_UI_H
#define TB_UI_H

#include "raylib.h"
#include "render.h"
#include "../sim/core.h"

typedef struct {
    Font fontUI;
    Font fontHeader;
    Font fontTitle;
    Font fontSmall;
    bool ready;
    bool saveRequested;
    bool quitRequested;
    bool newGameRequested;
    bool loadRequested;
    bool settingsOpen;
    float hoverTimer;
    int journalTab;
    int messageScroll;
} UI;

bool UIInit(UI *u);
void UIFree(UI *u);
void UIResetRequests(UI *u);

// Все экраны интерфейса
void UIDrawHUD(UI *u, Assets *a, Game *g);
void UIDrawTitle(UI *u, Assets *a, Game *g);
void UIDrawPause(UI *u, Assets *a, Game *g);
void UIDrawSettings(UI *u, Assets *a, Game *g);
void UIDrawJournal(UI *u, Assets *a, Game *g);
void UIDrawShop(UI *u, Assets *a, Game *g);
void UIDrawDialog(UI *u, Assets *a, Game *g);
void UIDrawAchievements(UI *u, Assets *a, Game *g);
void UIDrawLoading(UI *u, const char *text, float progress);
void UIDrawToast(UI *u, Game *g);
void UIDrawFade(UI *u, float alpha);

#endif
