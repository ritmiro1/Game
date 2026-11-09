// ============================================================================
//  fishing.c — заброс, поплавок, подсечка и вываживание (ключевая механика)
// ============================================================================
#include "core.h"

#define ROD_TIP_UP     1.52f
#define ROD_TIP_FWD    0.62f
#define ROD_TIP_SIDE   0.28f

void FishingInit(Fishing *f) {
    memset(f, 0, sizeof(*f));
    f->phase = FISH_IDLE;
    f->drag = 1.0f;
    f->lastCatchWeight = 0.0f;
    f->lastCatchSpecies = -1;
}

Vec3 FishingRodTip(const Game *g) {
    const Player *p = &g->player;
    Vec3 fwd = dir_from_yaw_pitch(p->yaw, 0.0f);
    Vec3 right = v3(cosf(p->yaw), 0.0f, -sinf(p->yaw));
    Vec3 tip = v3add(p->pos, v3(0, ROD_TIP_UP + p->bob * 0.01f, 0));
    tip = v3add(tip, v3mul(fwd, ROD_TIP_FWD));
    tip = v3add(tip, v3mul(right, ROD_TIP_SIDE));
    // Изгиб удилища тянет вершинку вниз/к рыбе
    float bend = g->fish_rod.rodBend;
    tip.y -= bend * 0.55f;
    if (g->fish_rod.phase == FISH_FLOAT || g->fish_rod.phase == FISH_HOOKED) {
        Vec3 toB = v3norm(v3sub(g->fish_rod.bobberPos, tip));
        tip = v3add(tip, v3mul(toB, bend * 0.30f));
    }
    return tip;
}

// ------------------------------------------------------------------ заброс
void FishingStartCharge(Game *g) {
    Fishing *f = &g->fish_rod;
    if (g->player.mode == PM_SWIM) { Hint(g, "В воде не забросить — доберитесь до берега или лодки"); return; }
    if (f->phase != FISH_IDLE) return;
    if (g->inv.rodDurability <= 0.02f) { Toast(g, "Удилище сломано. Почините его в магазине у Ани"); return; }
    f->phase = FISH_CHARGE;
    f->charge = 0.0f;
}

void FishingCancel(Game *g) {
    Fishing *f = &g->fish_rod;
    if (f->phase == FISH_CHARGE) { f->phase = FISH_IDLE; f->charge = 0.0f; }
    else if (f->phase == FISH_FLOAT) {
        f->phase = FISH_IDLE;
        f->bobberInWater = false;
        Toast(g, "Вы смотали снасть");
    }
}

void FishingReleaseCast(Game *g) {
    Fishing *f = &g->fish_rod;
    if (f->phase != FISH_CHARGE) return;
    float charge = saturatef(f->charge);
    int rodTier = g->inv.rodTier;
    f->castDistance = 7.0f + charge * (13.0f + 7.0f * (float)rodTier);
    Vec3 dir = dir_from_yaw_pitch(g->player.yaw, g->player.pitch);
    dir.y += 0.42f - g->player.pitch * 0.35f;
    dir = v3norm(dir);
    Vec3 tip = FishingRodTip(g);
    f->bobberPos = v3add(tip, v3mul(dir, 0.3f));
    float speed = 9.5f + charge * 13.5f;
    f->bobberVel = v3mul(dir, speed);
    f->bobberInWater = false;
    f->phase = FISH_CAST;
    f->lineLength = 0.5f;
    f->charge = 0.0f;
    // Расход наживки и износ
    if (g->inv.bait > 0) g->inv.bait--;
    g->inv.rodDurability = saturatef(g->inv.rodDurability - 0.004f);
    g->splashAmount = 0.25f;
    (void)rodTier;
}

// -------------------------------------------------------------- вываживание
void FishingSetReeling(Game *g, bool reeling) {
    Fishing *f = &g->fish_rod;
    if (f->phase != FISH_HOOKED) return;
    if (reeling) f->reeling = 1;
}

static void fight_step(Game *g, float dt) {
    Fishing *f = &g->fish_rod;
    const FishSpecies *sp = FishGet(f->hookedSpecies);
    f->fightTime += dt;

    // Тяга рыбы: колебания + рывки, затухающие по мере усталости
    float t = f->fightTime;
    float osc = 0.55f + 0.45f * sinf(t * 2.15f + (float)f->hookedSpecies * 1.3f);
    float surge = 1.0f;
    if (sinf(t * 0.85f + 2.1f) > 0.86f) surge = 1.55f;
    float weightFactor = saturatef(f->hookedWeight / fmaxf(0.5f, sp->maxKg)) * 0.75f + 0.45f;
    float fatigue = 1.0f - saturatef(f->fightTime / (9.0f + f->hookedWeight * 2.4f)) * 0.45f;
    f->fishPull = clampf(sp->strength * osc * surge * weightFactor * fatigue, 0.0f, 1.6f);

    bool held = f->reeling != 0;
    if (held) {
        float rate = 0.115f + 0.045f * (float)g->inv.rodTier;
        f->reelProgress += dt * rate * clampf(1.25f - f->fishPull * 0.55f, 0.15f, 1.25f);
        f->tension += dt * (0.42f + f->fishPull * 0.78f) / clampf(f->drag, 0.5f, 1.6f);
        g->player.stamina = saturatef(g->player.stamina - dt * 0.055f);
        if (g->player.stamina <= 0.001f) {
            f->tension += dt * 0.35f;   // силы кончились — леска натягивается
        }
    } else {
        f->tension -= dt * 0.92f;
        f->reelProgress -= dt * 0.052f * f->fishPull;
    }
    f->reelProgress = clampf(f->reelProgress, 0.0f, 1.0f);
    f->tension = clampf(f->tension, 0.0f, 1.45f);
    f->rodBend = dampf(f->rodBend, saturatef(f->tension * 1.15f), 4.0f, dt);

    // Лодка раскачивается от борьбы
    if (g->player.mode == PM_BOAT) {
        g->boat.roll += sinf(t * 6.0f) * 0.0016f * f->fishPull;
    }

    // Обрыв лески
    if (f->tension > 1.0f) {
        f->escapeTimer += dt;
        f->lineBreakFlash = 1.0f;
        if (f->escapeTimer > 0.42f) {
            f->phase = FISH_IDLE;
            f->bobberInWater = false;
            f->rodBend = 0.0f;
            g->inv.rodDurability = saturatef(g->inv.rodDurability - 0.05f);
            g->camShake = fmaxf(g->camShake, 0.16f);
            Toast(g, "Леска не выдержала! Рыба ушла. Ослабьте фрикцион или тяните плавнее");
            AddSplash(g, f->bobberPos, 0.5f);
            return;
        }
    } else {
        f->escapeTimer = fmaxf(0.0f, f->escapeTimer - dt * 1.6f);
    }

    // Успешное вываживание
    if (f->reelProgress >= 0.999f) {
        int species = f->hookedSpecies;
        float kg = f->hookedWeight;
        f->phase = FISH_LANDED;
        f->hookedTimer = 0.0f;
        f->bobberInWater = false;
        f->rodBend = 0.0f;
        InventoryAddFish(g, species, kg);
        AchievementAdd(g, 1.0f, species, kg);
        QuestNotifyCatch(g, species, kg);
        f->lastCatchSpecies = species;
        f->lastCatchWeight = kg;
        f->lastCatchTime = g->env.timeOfDay;
        g->caughtPopup = true;
        g->caughtPopupTimer = 4.2f;
        g->caughtPopupSpecies = species;
        g->caughtPopupWeight = kg;
        AddSplash(g, f->bobberPos, 0.85f);
        g->camShake = fmaxf(g->camShake, 0.08f);
        Toast(g, "Улов! %s — %.2f кг", FishGet(species)->name, kg);
        if (g->inv.rodDurability > 0.0f)
            g->inv.rodDurability = saturatef(g->inv.rodDurability - 0.002f);
        // Трофей мог закрыть квест/достижение — проверим сразу
        ProgressTick(g, 0.0f);
    }
}

// ---------------------------------------------------------------- обновление
void FishingUpdate(Game *g, float dt) {
    Fishing *f = &g->fish_rod;
    f->lineBreakFlash = fmaxf(0.0f, f->lineBreakFlash - dt * 2.0f);

    Vec3 tip = FishingRodTip(g);

    switch (f->phase) {
        case FISH_IDLE:
            f->rodBend = dampf(f->rodBend, 0.06f, 3.0f, dt);
            break;

        case FISH_CHARGE: {
            f->charge = saturatef(f->charge + dt * 0.95f);
            f->rodBend = dampf(f->rodBend, -0.25f * f->charge, 6.0f, dt); // натяг назад
            break;
        }

        case FISH_CAST: {
            f->bobberVel.y -= 17.5f * dt;
            f->bobberPos = v3add(f->bobberPos, v3mul(f->bobberVel, dt));
            f->lineLength = v3len(v3sub(f->bobberPos, tip));
            WorldClampToMap(g, &f->bobberPos, 2.0f);
            // Попадание в воду
            float waterY = WaveHeight((Vec2){ f->bobberPos.x, f->bobberPos.z }, g->waterTime, g->env.wind);
            float ground = WorldHeight(g, f->bobberPos.x, f->bobberPos.z);
            if (f->bobberPos.y <= waterY) {
                f->bobberPos.y = waterY;
                f->bobberInWater = true;
                f->phase = FISH_FLOAT;
                f->floatTimer = 0.0f;
                f->depthAtBobber = fmaxf(0.0f, waterY - ground);
                f->biteTimer = rng_range(&g->rng, 4.5f, 13.0f) *
                               (1.28f - 0.11f * (float)g->inv.lureTier);
                if (f->depthAtBobber < 0.35f) {
                    Toast(g, "Слишком мелко — здесь рыба не клюнет");
                    f->biteTimer *= 3.0f;
                }
                AddSplash(g, f->bobberPos, 0.22f);
            } else if (f->bobberPos.y <= ground + 0.05f) {
                // Заброс на сушу
                f->phase = FISH_IDLE;
                f->rodBend = 0.0f;
                Toast(g, "Поплавок упал на берег. Забрасывайте в воду");
            }
            if (v3len(f->bobberVel) < 0.2f && f->phase == FISH_CAST) { f->phase = FISH_IDLE; }
            break;
        }

        case FISH_FLOAT: {
            f->floatTimer += dt;
            float wy = WaveHeight((Vec2){ f->bobberPos.x, f->bobberPos.z }, g->waterTime, g->env.wind);
            f->bobberPos.y = dampf(f->bobberPos.y, wy + 0.03f, 9.0f, dt);
            // Дрейф по ветру
            float drift = 0.22f * g->env.wind;
            f->bobberPos.x += g->env.windDir.x * drift * dt;
            f->bobberPos.z += g->env.windDir.z * drift * dt;
            f->lineLength = v3len(v3sub(f->bobberPos, tip));
            f->rodBend = dampf(f->rodBend, 0.10f, 2.0f, dt);
            f->depthAtBobber = fmaxf(0.0f, wy - WorldHeight(g, f->bobberPos.x, f->bobberPos.z));

            f->lastNibble -= dt;
            f->biteTimer -= dt;

            if (f->biteWindow > 0.0f) {
                // Окно подсечки
                f->biteWindow -= dt;
                f->biteIndicator = true;
                f->bobberPos.y -= fabsf(sinf(f->floatTimer * 26.0f)) * 0.10f;
                if (f->biteWindow <= 0.0f) {
                    f->biteIndicator = false;
                    f->phase = FISH_IDLE;
                    f->rodBend = 0.0f;
                    if (g->inv.bait > 0) g->inv.bait--;
                    Toast(g, "Поклёвка сорвалась — рыба ушла с наживкой. Подсекайте быстрее");
                }
            } else {
                f->biteIndicator = false;
                // Предпоклёвочные подрагивания
                if (f->lastNibble <= 0.0f && f->biteTimer < 3.0f) {
                    f->lastNibble = rng_range(&g->rng, 0.5f, 1.6f);
                    f->bobberPos.y -= 0.05f;
                    g->splashAmount = fmaxf(g->splashAmount, 0.12f);
                }
                if (f->biteTimer <= 0.0f) {
                    int hour = (int)g->env.timeOfDay;
                    int sp = FishRollSpecies(g, f->depthAtBobber, hour, g->env.weather);
                    f->bitingSpecies = sp;
                    f->biteWindow = 1.45f - (float)FishGet(sp)->rarity * 0.12f;
                    f->bobberPos.y -= 0.18f;
                    AddSplash(g, f->bobberPos, 0.35f);
                    g->splashAmount = fmaxf(g->splashAmount, 0.35f);
                }
            }
            break;
        }

        case FISH_HOOKED: {
            fight_step(g, dt);
            // Позиция поплавка при вываживании: тянется к игроку
            Vec3 toPlayer = v3norm(v3sub(g->player.pos, f->bobberPos));
            f->bobberPos = v3add(f->bobberPos, v3mul(toPlayer, dt * (0.8f + f->reelProgress * 1.6f)));
            float wy = WaveHeight((Vec2){ f->bobberPos.x, f->bobberPos.z }, g->waterTime, g->env.wind);
            f->bobberPos.y = lerpf(f->bobberPos.y, wy + 0.06f + sinf(f->fightTime * 5.0f) * 0.12f, saturatef(dt * 3.0f));
            f->lineLength = v3len(v3sub(f->bobberPos, tip));
            if (v3len(v3sub(f->bobberPos, g->player.pos)) < 3.2f) {
                f->reelProgress = fmaxf(f->reelProgress, 0.96f);
            }
            break;
        }

        case FISH_LANDED: {
            f->hookedTimer += dt;
            f->rodBend = dampf(f->rodBend, 0.05f, 4.0f, dt);
            if (f->hookedTimer > 0.9f) { f->phase = FISH_IDLE; }
            break;
        }
        default: break;
    }
    // Сбрасываем флаг подмотки: он выставляется заново каждый кадр из ввода
    f->reeling = 0;
}

// Вызывается из sim.c при нажатии ЛКМ в момент окна поклёвки
bool FishingTryHook(Game *g) {
    Fishing *f = &g->fish_rod;
    if (f->phase != FISH_FLOAT || f->biteWindow <= 0.0f) return false;
    const FishSpecies *sp = FishGet(f->bitingSpecies);
    f->hookedSpecies = f->bitingSpecies;
    // Вес: смещение к небольшим особям, но с шансом на трофей
    float t = powf(rng_f(&g->rng), 1.8f - 0.25f * (float)g->inv.rodTier);
    f->hookedWeight = lerpf(sp->minKg, sp->maxKg, saturatef(t));
    f->hookedWeight = roundf(f->hookedWeight * 100.0f) / 100.0f;
    f->phase = FISH_HOOKED;
    f->tension = 0.32f;
    f->reelProgress = 0.0f;
    f->fightTime = 0.0f;
    f->escapeTimer = 0.0f;
    f->biteWindow = 0.0f;
    f->biteIndicator = false;
    f->hookedTimer = 0.0f;
    g->player.stamina = saturatef(g->player.stamina + 0.05f);
    g->camShake = fmaxf(g->camShake, 0.12f);
    AddSplash(g, f->bobberPos, 0.7f);
    Toast(g, "Подсечка! Вываживайте: держите ЛКМ, следите за натяжением");
    return true;
}
