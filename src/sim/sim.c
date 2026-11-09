// ============================================================================
//  sim.c — оркестрация симуляции: ввод, камера, частицы, взаимодействия
// ============================================================================
#include "core.h"

static void default_settings(Game *g) {
    g->settings.quality = 2;
    g->settings.masterVolume = 0.90f;
    g->settings.musicVolume = 0.55f;
    g->settings.sfxVolume = 0.85f;
    g->settings.ambientVolume = 0.75f;
    g->settings.sensitivity = 1.0f;
    g->settings.fov = 68.0f;
    g->settings.invertY = false;
    g->settings.tiltShift = true;
    g->settings.bloom = true;
    g->settings.ssao = true;
    g->settings.rainEffects = true;
    g->settings.headBob = true;
    g->settings.shadowQuality = 2;
    g->settings.language = 0;
}

void SimNewGame(Game *g, uint32_t seed) {
    Settings keep = g->settings;
    char savePath[512];
    snprintf(savePath, sizeof(savePath), "%s", g->savePath);
    memset(g, 0, sizeof(Game));
    g->settings = keep;
    if (savePath[0]) snprintf(g->savePath, sizeof(g->savePath), "%s", savePath);
    if (g->settings.fov < 30.0f) default_settings(g);

    rng_seed(&g->rng, seed ^ 0xC0FFEEu);
    WorldGenerate(g, seed);

    g->env.dayFraction = 0.30f;    // ~7:12 утра
    g->env.day = 1;
    g->env.weather = W_CLEAR;
    g->env.weatherTimer = 240.0f;
    g->env.wind = 0.3f;
    g->env.windDir = (Vec2){ 0.72f, 0.69f };
    g->env.cloudAmount = 0.2f;
    g->env.rainAmount = 0.0f;
    g->env.fogAmount = 0.15f;
    g->env.lightningTimer = 8.0f;
    EnvComputeSky(&g->env);

    FishDBInit();
    ProgressInit(g);
    PlayerInit(g, v3(g->terrain.lm.spawnPos.x, g->terrain.lm.spawnPos.y + 0.2f, g->terrain.lm.spawnPos.z));
    BoatInit(g, g->terrain.lm.boatStart, g->terrain.lm.boatYaw);
    FishingInit(&g->fish_rod);
    NpcInitAll(g);
    FishSpawnAll(g);
    memset(g->particles, 0, sizeof(g->particles));
    g->screen = GS_PLAY;
    g->prevScreen = GS_TITLE;
    g->journalTab = 0;
    g->shopSelected = 0;
    g->pendingPopupAch = -1;
    g->camShake = 0.0f;
    g->waterTime = 0.0f;
    g->totalTime = 0.0f;
    g->prevInput = 0;
    Toast(g, "Тихий Залив ждёт вас. Удочка — ЛКМ, ходьба — WASD");
    Hint(g, "Подойдите к дедушке Никите на причале и нажмите E");
    g->camPos = v3(g->player.pos.x, g->player.pos.y + 1.66f, g->player.pos.z);
    g->camTarget = v3add(g->camPos, dir_from_yaw_pitch(g->player.yaw, g->player.pitch));
}

void SimInit(Game *g, uint32_t seed) {
    memset(g, 0, sizeof(Game));
    default_settings(g);
    BuildSavePath(g);
    SimNewGame(g, seed);
}

void SimRespawnPlayer(Game *g) {
    Vec3 sp = g->terrain.lm.spawnPos;
    g->player.pos = v3(sp.x, WorldHeight(g, sp.x, sp.z) + 0.2f, sp.z);
    g->player.vel = v3(0, 0, 0);
    g->player.mode = PM_FOOT;
    g->player.health = 1.0f;
    g->player.stamina = 1.0f;
    g->fish_rod.phase = FISH_IDLE;
    g->boat.occupied = false;
    g->screen = GS_PLAY;
}

// ------------------------------------------------------------------- камера
static void update_camera(Game *g, float dt) {
    Player *p = &g->player;
    Vec3 eye, target;
    float bobAmt = g->settings.headBob ? 1.0f : 0.0f;

    if (p->mode == PM_BOAT) {
        float bob = sinf(g->boat.bobPhase * 1.4f) * 0.045f;
        Vec3 dir = dir_from_yaw_pitch(p->yaw, p->pitch);
        Vec3 back = v3mul(dir, -4.6f);
        eye = v3add(v3add(p->pos, v3(0, 1.55f + bob, 0)), back);
        eye = v3add(eye, v3(0, 0.8f, 0));
        float wy = WaveHeight((Vec2){ eye.x, eye.z }, g->waterTime, g->env.wind);
        if (eye.y < wy + 0.55f) eye.y = wy + 0.55f;
        target = v3add(eye, dir);
        target.y += 0.15f;
    } else if (p->mode == PM_SWIM) {
        float bob = sinf(p->bob * 1.2f) * 0.05f * bobAmt;
        eye = v3add(p->pos, v3(0, 0.45f + bob, 0));
        Vec3 dir = dir_from_yaw_pitch(p->yaw, p->pitch);
        target = v3add(eye, dir);
    } else {
        float speed = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
        float bobY = sinf(p->bob * 3.6f) * 0.032f * saturatef(speed / 3.0f) * bobAmt;
        float bobX = cosf(p->bob * 1.8f) * 0.022f * saturatef(speed / 3.0f) * bobAmt;
        float eyeH = p->crouch ? 1.15f : 1.66f;
        Vec3 right = v3(cosf(p->yaw), 0, -sinf(p->yaw));
        eye = v3add(p->pos, v3(0, eyeH + bobY, 0));
        eye = v3add(eye, v3mul(right, bobX));
        Vec3 dir = dir_from_yaw_pitch(p->yaw, p->pitch);
        target = v3add(eye, dir);
    }
    // Тряска
    if (g->camShake > 0.0005f) {
        float t = g->totalTime * 47.0f;
        eye.x += sinf(t * 1.7f) * g->camShake * 0.22f;
        eye.y += sinf(t * 2.3f) * g->camShake * 0.18f;
        eye.z += cosf(t * 1.9f) * g->camShake * 0.22f;
        g->camShake = fmaxf(0.0f, g->camShake - dt * 0.55f);
    }
    g->camPos = eye;
    g->camTarget = target;
    float wy = WaveHeight((Vec2){ eye.x, eye.z }, g->waterTime, g->env.wind);
    g->underwaterCam = (eye.y < wy - 0.02f) && (WorldWaterDepth(g, eye.x, eye.z) > 0.4f);
}

// ------------------------------------------------------------------ частицы
static void update_particles(Game *g, float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        if (p->kind == PT_SPLASH) p->vel.y -= 12.0f * dt;
        else if (p->kind == PT_FOAM) p->vel.y -= 2.0f * dt;
        else if (p->kind == PT_SMOKE) p->vel.y += 0.55f * dt;
        else if (p->kind == PT_LEAF) {
            p->vel.x += sinf(g->totalTime * 2.0f + (float)i) * 0.4f * dt;
            p->vel.y -= 0.9f * dt;
        }
        p->vel = v3mul(p->vel, 1.0f - dt * 0.55f);
        p->pos = v3add(p->pos, v3mul(p->vel, dt));
        if (p->kind == PT_SPLASH || p->kind == PT_FOAM) {
            float wy = WaveHeight((Vec2){ p->pos.x, p->pos.z }, g->waterTime, g->env.wind);
            if (p->pos.y < wy) { p->life = 0.0f; }
        }
    }
}

// Обновление только визуальной части мира (пауза, меню)
void SimUpdateIdle(Game *g, float dt) {
    EnvUpdate(g, dt);
    update_particles(g, dt);
    update_camera(g, dt);
    if (g->toastTimer > 0.0f) g->toastTimer -= dt;
    if (g->hintTimer > 0.0f) g->hintTimer -= dt;
    if (g->caughtPopupTimer > 0.0f) g->caughtPopupTimer -= dt;
    for (int i = 0; i < MAX_ACHIEVEMENTS; i++)
        if (g->ach[i].popupTimer > 0.0f) g->ach[i].popupTimer -= dt;
    g->splashAmount = fmaxf(0.0f, g->splashAmount - dt * 1.6f);
}

// ------------------------------------------------------- вспомогательный ввод
static void interact_scan(Game *g) {
    g->interactPrompt[0] = 0;
    g->interactNpc = -1;
    Player *p = &g->player;
    // Лодка
    float d = v3len(v3flat(v3sub(g->boat.pos, p->pos)));
    if (p->mode != PM_BOAT && d < 4.2f && !g->boat.occupied) {
        snprintf(g->interactPrompt, sizeof(g->interactPrompt), "F — сесть в лодку");
        g->interactProgress = 1.0f;
        return;
    }
    int n = NpcNearest(g, 3.6f);
    if (n >= 0) {
        g->interactNpc = n;
        if (g->npc[n].isMerchant && g->npc[n].questId >= 0 && g->quests[2].state != QS_LOCKED)
            snprintf(g->interactPrompt, sizeof(g->interactPrompt), "E — поговорить с %s / магазин", g->npc[n].name);
        else
            snprintf(g->interactPrompt, sizeof(g->interactPrompt), "E — поговорить с %s", g->npc[n].name);
    } else if (p->mode == PM_BOAT) {
        snprintf(g->interactPrompt, sizeof(g->interactPrompt), "F — сойти на берег");
    }
}

void SimUpdate(Game *g, float dt, int input, Vec3 moveIn, Vec3 lookIn) {
    if (dt > 0.1f) dt = 0.1f;
    g->dt = dt;
    g->totalTime += dt;

    Player *p = &g->player;
    Fishing *f = &g->fish_rod;

    // --- мышь
    float sens = 0.0022f * g->settings.sensitivity;
    p->yaw -= lookIn.x * sens;
    p->pitch -= lookIn.y * sens * (g->settings.invertY ? -1.0f : 1.0f);
    p->pitch = clampf(p->pitch, -1.35f, 1.25f);
    p->yaw = angle_wrap(p->yaw);

    bool pressJump = (input & IN_JUMP) && !(g->prevInput & IN_JUMP);
    bool pressUse = (input & IN_USE) && !(g->prevInput & IN_USE);
    bool pressBoard = (input & IN_BOARD) && !(g->prevInput & IN_BOARD);
    bool pressLantern = (input & IN_LANTERN) && !(g->prevInput & IN_LANTERN);
    bool pressSonar = (input & IN_SONAR) && !(g->prevInput & IN_SONAR);
    bool pressFish = (input & IN_FISH) && !(g->prevInput & IN_FISH);
    bool releaseFish = !(input & IN_FISH) && (g->prevInput & IN_FISH);
    bool pressAlt = (input & IN_ALT) && !(g->prevInput & IN_ALT);

    // --- окружение
    EnvUpdate(g, dt);

    // --- движение
    Vec3 fwd = v3(sinf(p->yaw), 0, cosf(p->yaw));
    Vec3 right = v3(cosf(p->yaw), 0, -sinf(p->yaw));
    Vec3 worldMove = v3add(v3mul(fwd, moveIn.z), v3mul(right, moveIn.x));
    if (v3len(worldMove) > 1.0f) worldMove = v3norm(worldMove);
    p->crouch = (input & IN_CROUCH) != 0;

    if (p->mode == PM_BOAT) {
        Boat *b = &g->boat;
        b->throttle = dampf(b->throttle, moveIn.z, 3.0f, dt);
        b->steer = dampf(b->steer, moveIn.x, 6.0f, dt);
        if (pressFish || f->phase != FISH_IDLE) {
            // во время рыбалки лодка не газует
            b->throttle = dampf(b->throttle, 0.0f, 5.0f, dt);
        }
    } else {
        PlayerUpdate(g, dt, worldMove, pressJump, (input & IN_SPRINT) != 0, p->crouch);
    }
    BoatUpdate(g, dt);

    // --- взаимодействия
    interact_scan(g);
    if (pressBoard) {
        if (p->mode == PM_BOAT) BoatLeave(g);
        else if (v3len(v3flat(v3sub(g->boat.pos, p->pos))) < 6.0f) BoatBoard(g);
    }
    if (pressUse) {
        if (g->interactNpc >= 0) {
            NpcTalkBegin(g, g->interactNpc);
            g->dialogLineCount = 0;
            for (int i = 0; i < MAX_DIALOG; i++) if (g->dialogText[i][0]) g->dialogLineCount = i + 1;
            if (g->dialogActive && g->dialogLineCount > 0) g->screen = GS_DIALOG;
            else NpcTalkEnd(g);
        }
    }
    if (pressLantern) {
        if (g->inv.hasLantern) {
            g->lanternOn = !g->lanternOn;
            Toast(g, g->lanternOn ? "Фонарь включён" : "Фонарь выключен");
        } else Toast(g, "Фонаря нет. Купите его в магазине у Ани");
    }
    if (pressSonar) {
        if (g->inv.hasSonar) {
            g->sonarOn = !g->sonarOn;
            Toast(g, g->sonarOn ? "Сонар включён" : "Сонар выключен");
        } else Toast(g, "Сонар не куплен");
    }

    // --- рыбалка
    if (pressFish) {
        if (f->phase == FISH_FLOAT && f->biteWindow > 0.0f) FishingTryHook(g);
        else if (f->phase == FISH_IDLE) FishingStartCharge(g);
        else if (f->phase == FISH_HOOKED) FishingSetReeling(g, true);
    }
    if (releaseFish) {
        if (f->phase == FISH_CHARGE) FishingReleaseCast(g);
    }
    if (pressAlt) FishingCancel(g);
    if ((input & IN_FISH) && f->phase == FISH_HOOKED) FishingSetReeling(g, true);
    if (f->phase != FISH_IDLE && p->mode == PM_SWIM) FishingCancel(g);

    FishUpdate(g, dt);
    FishingUpdate(g, dt);
    NpcUpdate(g, dt);
    ProgressTick(g, dt);
    update_particles(g, dt);
    update_camera(g, dt);

    // --- таймеры интерфейса
    if (g->toastTimer > 0.0f) g->toastTimer -= dt;
    if (g->hintTimer > 0.0f) g->hintTimer -= dt;
    if (g->caughtPopupTimer > 0.0f) g->caughtPopupTimer -= dt;
    if (g->caughtPopupTimer <= 0.0f) g->caughtPopup = false;
    for (int i = 0; i < MAX_ACHIEVEMENTS; i++)
        if (g->ach[i].popupTimer > 0.0f) g->ach[i].popupTimer -= dt;
    g->splashAmount = fmaxf(0.0f, g->splashAmount - dt * 1.6f);
    g->lanternFlicker = 0.82f + 0.18f * sinf(g->totalTime * 11.0f) * sinf(g->totalTime * 3.7f);
    if (g->boat.occupied) g->player.pos = BoatDeckPoint(&g->boat, 0.0f, -0.35f);

    g->prevInput = input;
}
