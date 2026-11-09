// ============================================================================
//  progress.c — инвентарь, экономика, квесты, достижения, оповещения
// ============================================================================
#include "core.h"
#include <stdarg.h>

// ------------------------------------------------------------------ сообщения
void Toast(Game *g, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->toast, sizeof(g->toast), fmt, ap);
    va_end(ap);
    g->toastTimer = 4.0f;
}

void Hint(Game *g, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->hint, sizeof(g->hint), fmt, ap);
    va_end(ap);
    g->hintTimer = 6.0f;
}

void ParticleSpawn(Game *g, Vec3 pos, Vec3 vel, float life, float size, ParticleKind kind, Vec3 color) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (p->life > 0.0f) continue;
        p->pos = pos;
        p->vel = vel;
        p->life = life;
        p->maxLife = life;
        p->size = size;
        p->kind = kind;
        p->color = color;
        return;
    }
}

void AddSplash(Game *g, Vec3 at, float strength) {
    float wy = WaveHeight((Vec2){ at.x, at.z }, g->waterTime, g->env.wind);
    Vec3 base = v3(at.x, fmaxf(at.y, wy), at.z);
    int count = (int)(6.0f + strength * 26.0f);
    for (int i = 0; i < count; i++) {
        float a = rng_range(&g->rng, 0.0f, 2.0f * PI);
        float sp = rng_range(&g->rng, 0.6f, 2.4f) * (0.5f + strength);
        Vec3 v = v3(cosf(a) * sp, rng_range(&g->rng, 1.6f, 4.2f) * (0.5f + strength), sinf(a) * sp);
        ParticleSpawn(g, base, v, rng_range(&g->rng, 0.5f, 1.3f),
                      rng_range(&g->rng, 0.06f, 0.20f), PT_SPLASH, v3(0.92f, 0.97f, 1.0f));
    }
    for (int i = 0; i < (int)(strength * 8.0f); i++) {
        float a = rng_range(&g->rng, 0.0f, 2.0f * PI);
        Vec3 p = v3(base.x + cosf(a) * 0.4f, wy + 0.03f, base.z + sinf(a) * 0.4f);
        ParticleSpawn(g, p, v3(0, 0.35f, 0), rng_range(&g->rng, 1.2f, 2.6f),
                      rng_range(&g->rng, 0.30f, 0.75f), PT_FOAM, v3(0.88f, 0.94f, 0.96f));
    }
    g->splashAmount = fmaxf(g->splashAmount, strength);
    g->lastSplashPos = base;
}

// ------------------------------------------------------------------ квесты
static void quest_def(Game *g, int id, const char *title, const char *desc, const char *hint,
                      int target, int reward, int giver, int kind, int param, float paramF) {
    Quest *q = &g->quests[id];
    q->title = title; q->desc = desc; q->hint = hint;
    q->targetCount = target;
    q->progress = 0;
    q->state = QS_LOCKED;
    q->rewardCoins = reward;
    q->giverNpc = giver;
    q->kind = kind;
    q->param = param;
    q->paramF = paramF;
}

// ------------------------------------------------------------------ достижения
static void ach_def(Game *g, int id, const char *title, const char *desc, float target) {
    Achievement *a = &g->ach[id];
    a->title = title; a->desc = desc;
    a->unlocked = false; a->progress = 0; a->target = target; a->popupTimer = 0;
}

void ProgressInit(Game *g) {
    // --- квесты
    quest_def(g, 0, "Первый улов",
              "Поймайте три любые рыбы в заливе",
              "Забросьте удочку с берега или лодки и вываживайте рыбу",
              3, 120, 0, 0, 0, 0);
    quest_def(g, 1, "Заказ для ухи",
              "Поймайте двух плотв для Ани",
              "Плотва держится на мелководье у камыша",
              2, 150, 0, 1, 1, 0);
    quest_def(g, 2, "Окунь к ужину",
              "Поймайте одного окуня",
              "Окунь клюёт почти везде у берега",
              1, 110, 2, 1, 0, 0);
    quest_def(g, 3, "Ночной сторож",
              "Поймайте двух рыб после заката",
              "Ночью клюёт судак, угорь и сом",
              2, 260, 1, 2, 0, 0);
    quest_def(g, 4, "Щука-легенда",
              "Возьмите щуку весом не меньше 5 кг",
              "Крупная щука стоит в глубине у коряг",
              1, 520, 1, 4, 4, 5.0f);
    quest_def(g, 5, "Свет маяка",
              "Доберитесь до старого маяка на северном мысу",
              "Идите по западному берегу на север",
              1, 200, -1, 3, 0, 0);
    g->quests[5].state = QS_ACTIVE;   // активен изначально

    // --- достижения
    ach_def(g, 0,  "Первый улов",            "Поймайте первую рыбу", 1);
    ach_def(g, 1,  "Рыбак-новичок",          "Поймайте 10 рыб", 10);
    ach_def(g, 2,  "Знаток залива",          "Поймайте 6 разных видов рыб", 6);
    ach_def(g, 3,  "Коллекционер",           "Поймайте все 13 видов рыб", 13);
    ach_def(g, 4,  "Трофей",                 "Поймайте рыбу тяжелее 3 кг", 1);
    ach_def(g, 5,  "Ночная смена",           "Поймайте 3 рыбы ночью", 3);
    ach_def(g, 6,  "Штормовой улов",         "Поймайте рыбу в шторм", 1);
    ach_def(g, 7,  "Смотритель",             "Доберитесь до маяка", 1);
    ach_def(g, 8,  "Душа компании",          "Поговорите со всеми жителями залива", 3);
    ach_def(g, 9,  "Мастер снасти",          "Улучшите удилище до III уровня", 1);
    ach_def(g, 10, "Богач",                  "Заработайте 1000 монет", 1000);
    ach_def(g, 11, "Легенда залива",         "Поймайте Лунную рыбу", 1);
    ach_def(g, 12, "Все поручения",          "Выполните все задания жителей", 5);
    ach_def(g, 13, "Долгий день",            "Проведите в заливе 20 минут", 1200);

    // --- инвентарь
    memset(&g->inv, 0, sizeof(g->inv));
    g->inv.coins = 60;
    g->inv.rodTier = 1;
    g->inv.lureTier = 1;
    g->inv.motorTier = 1;
    g->inv.bait = 12;
    g->inv.rodDurability = 1.0f;
    g->inv.hasLantern = false;
    g->inv.hasSonar = false;
    g->inv.selectedHotbar = 0;

    g->catchCount = 0;
    g->fishCaught = 0;
    memset(g->fishSpecies, 0, sizeof(g->fishSpecies));
    g->biggestKg = 0.0f;
    g->nightsFished = 0;
    g->stormsFished = 0;
    g->visitedLighthouse = false;
    g->npcsMet = 0;
    g->coinsEarned = 0;
    g->questsCompleted = 0;
    g->playTime = 0.0f;
    g->toastTimer = 0.0f;
    g->hintTimer = 0.0f;
    g->toast[0] = 0;
    g->hint[0] = 0;
    g->caughtPopup = false;
    g->caughtPopupTimer = 0.0f;
    g->pendingPopupAch = -1;
    g->lanternOn = false;
    g->sonarOn = false;
    g->dialogActive = false;
    g->dialogChoice = 0;
}

// ------------------------------------------------------------------ инвентарь
void InventoryAddFish(Game *g, int species, float weight) {
    if (g->inv.count < MAX_ITEMS) {
        Item *it = &g->inv.items[g->inv.count++];
        it->kind = IT_FISH;
        it->tier = 0;
        it->durability = 1.0f;
        it->count = 1;
        it->fishSpecies = species;
        it->weightKg = weight;
    }
    g->fishCaught++;
    if (species >= 0 && species < MAX_FISH_SPECIES) g->fishSpecies[species]++;
    if (weight > g->biggestKg) g->biggestKg = weight;
    if (EnvIsNight(&g->env)) g->nightsFished++;
    if (g->env.weather == W_STORM) g->stormsFished++;
    if (g->catchCount < (int)(sizeof(g->catches) / sizeof(g->catches[0]))) {
        CatchRecord *c = &g->catches[g->catchCount++];
        c->species = species;
        c->weightKg = weight;
        c->hour = g->env.timeOfDay;
        c->day = g->env.day;
        c->weather = (int)g->env.weather;
    }
    if (species == 12) {
        Toast(g, "Лунная рыба в ваших руках. Залив запомнит этот вечер");
    }
}

int InventoryFishValue(const Game *g) {
    int total = 0;
    for (int i = 0; i < g->inv.count; i++) {
        const Item *it = &g->inv.items[i];
        if (it->kind != IT_FISH) continue;
        const FishSpecies *sp = FishGet(it->fishSpecies);
        float price = (float)sp->pricePerKg * it->weightKg;
        if (sp->rarity >= 3) price *= 1.5f;
        total += (int)price;
    }
    return total;
}

void InventorySellAllFish(Game *g) {
    int total = InventoryFishValue(g);
    int n = 0;
    for (int i = 0; i < g->inv.count;) {
        if (g->inv.items[i].kind == IT_FISH) {
            for (int k = i; k < g->inv.count - 1; k++) g->inv.items[k] = g->inv.items[k + 1];
            g->inv.count--;
            n++;
        } else i++;
    }
    if (n == 0) { Toast(g, "В садке нет рыбы"); return; }
    g->inv.coins += total;
    g->coinsEarned += total;
    Toast(g, "Сдано рыб: %d. Получено %d монет", n, total);
}

// ------------------------------------------------------------------ магазин
typedef struct { const char *name; const char *desc; int price; int kind; int value; } ShopItem;
static const ShopItem SHOP[] = {
    { "Удилище «Тихий берег» II", "Дальше заброс, крепче леска",        260, 0, 2 },
    { "Удилище «Северный мыс» III", "Профессиональная снасть",           720, 0, 3 },
    { "Приманка «Серебро» II",     "Лучше берёт редкую рыбу",            320, 1, 2 },
    { "Приманка «Луна» III",       "Максимальный шанс трофея",           850, 1, 3 },
    { "Лодочный мотор",            "Скорость лодки выше",                900, 2, 2 },
    { "Фонарь",                    "Светит ночью и в тумане",            180, 3, 1 },
    { "Сонар",                     "Показывает рыбу под водой",          640, 4, 1 },
    { "Наживка (10 шт.)",          "Без наживки клёв хуже",               60, 5, 10 },
    { "Починка удилища",           "Восстанавливает снасть",              45, 6, 0 }
};
int ShopItemCount(void) { return (int)(sizeof(SHOP) / sizeof(SHOP[0])); }
const char *ShopItemName(int i) { return (i >= 0 && i < ShopItemCount()) ? SHOP[i].name : ""; }
const char *ShopItemDesc(int i) { return (i >= 0 && i < ShopItemCount()) ? SHOP[i].desc : ""; }
int ShopItemPrice(int i) { return (i >= 0 && i < ShopItemCount()) ? SHOP[i].price : 0; }
bool ShopItemOwned(const Game *g, int i) {
    if (i < 0 || i >= ShopItemCount()) return false;
    const ShopItem *it = &SHOP[i];
    switch (it->kind) {
        case 0: return g->inv.rodTier >= it->value;
        case 1: return g->inv.lureTier >= it->value;
        case 2: return g->inv.motorTier >= it->value;
        case 3: return g->inv.hasLantern;
        case 4: return g->inv.hasSonar;
        default: return false;
    }
}
bool ShopBuy(Game *g, int i) {
    if (i < 0 || i >= ShopItemCount()) return false;
    const ShopItem *it = &SHOP[i];
    if (it->kind <= 4 && it->kind >= 0 && ShopItemOwned(g, i)) { Toast(g, "Уже куплено"); return false; }
    if (g->inv.coins < it->price) { Toast(g, "Не хватает монет: нужно %d", it->price); return false; }
    g->inv.coins -= it->price;
    switch (it->kind) {
        case 0:
            if (it->value > g->inv.rodTier) g->inv.rodTier = it->value;
            g->inv.rodDurability = 1.0f;
            Toast(g, "Куплено: %s", it->name); break;
        case 1: g->inv.lureTier = it->value; Toast(g, "Куплено: %s", it->name); break;
        case 2: g->inv.motorTier = it->value; Toast(g, "Куплено: %s", it->name); break;
        case 3: g->inv.hasLantern = true; Toast(g, "Куплено: %s", it->name); break;
        case 4: g->inv.hasSonar = true; Toast(g, "Куплено: %s", it->name); break;
        case 5: g->inv.bait += it->value; Toast(g, "Куплено: %s (всего %d)", it->name, g->inv.bait); break;
        case 6: g->inv.rodDurability = 1.0f; Toast(g, "Удилище починено"); break;
        default: break;
    }
    return true;
}

// ---------------------------------------------------------------- достижения
static void unlock(Game *g, int id) {
    if (id < 0 || id >= MAX_ACHIEVEMENTS) return;
    if (g->ach[id].unlocked) return;
    g->ach[id].unlocked = true;
    g->ach[id].progress = g->ach[id].target;
    g->ach[id].popupTimer = 5.0f;
    g->pendingPopupAch = id;
    snprintf(g->pendingAchText, sizeof(g->pendingAchText), "%s", g->ach[id].title);
}

void AchievementAdd(Game *g, float amount, int species, float weight) {
    (void)species; (void)weight;
    g->ach[0].progress += amount;
    g->ach[1].progress += amount;
    if (amount > 0.0f) {
        // счётчик видов пересчитывается в ProgressTick
    }
}

static void ach_progress(Game *g, int id, float value) {
    if (g->ach[id].unlocked) return;
    g->ach[id].progress = value;
    if (value >= g->ach[id].target - 0.001f) unlock(g, id);
}

static void check_achievements(Game *g) {
    ach_progress(g, 0, (float)g->fishCaught);
    ach_progress(g, 1, (float)g->fishCaught);
    int speciesCount = 0;
    for (int i = 0; i < FishSpeciesCount(); i++) if (g->fishSpecies[i] > 0) speciesCount++;
    ach_progress(g, 2, (float)speciesCount);
    ach_progress(g, 3, (float)speciesCount);
    if (g->biggestKg > 3.0f) ach_progress(g, 4, 1.0f);
    ach_progress(g, 5, (float)g->nightsFished);
    if (g->stormsFished > 0) ach_progress(g, 6, 1.0f);
    if (g->visitedLighthouse) ach_progress(g, 7, 1.0f);
    ach_progress(g, 8, (float)g->npcsMet);
    if (g->inv.rodTier >= 3) ach_progress(g, 9, 1.0f);
    ach_progress(g, 10, (float)g->coinsEarned);
    if (g->fishSpecies[12] > 0) ach_progress(g, 11, 1.0f);
    int done = 0;
    for (int i = 0; i < 5; i++) if (g->quests[i].state == QS_DONE) done++;
    ach_progress(g, 12, (float)done);
    ach_progress(g, 13, g->playTime);
}

// -------------------------------------------------------------------- квесты
void QuestAccept(Game *g, int id) {
    if (id < 0 || id >= MAX_QUESTS) return;
    Quest *q = &g->quests[id];
    if (q->state != QS_LOCKED) return;
    q->state = QS_ACTIVE;
    Toast(g, "Новое задание: %s", q->title);
}

static void quest_complete(Game *g, int id) {
    Quest *q = &g->quests[id];
    if (q->state != QS_ACTIVE) return;
    q->state = QS_DONE;
    g->inv.coins += q->rewardCoins;
    g->coinsEarned += q->rewardCoins;
    g->questsCompleted++;
    Toast(g, "Задание выполнено: %s (+%d монет)", q->title, q->rewardCoins);
    g->camShake = fmaxf(g->camShake, 0.05f);
}

void QuestNotifyCatch(Game *g, int species, float weight) {
    for (int i = 0; i < MAX_QUESTS; i++) {
        Quest *q = &g->quests[i];
        if (q->state != QS_ACTIVE) continue;
        bool inc = false;
        switch (q->kind) {
            case 0: inc = true; break;                                  // любая рыба
            case 1: inc = (species == q->param); break;                 // конкретный вид
            case 2: inc = EnvIsNight(&g->env); break;                   // ночью
            case 4: inc = (species == q->param && weight >= q->paramF); break; // трофей
            default: break;
        }
        if (inc) {
            q->progress++;
            if (q->progress >= q->targetCount) quest_complete(g, i);
            else Toast(g, "%s: %d/%d", q->title, q->progress, q->targetCount);
        }
    }
}

void ProgressTick(Game *g, float dt) {
    g->playTime += dt;
    // Маяк: приближение к ориентиру
    Quest *ql = &g->quests[5];
    if (ql->state == QS_ACTIVE) {
        float d = v3len(v3flat(v3sub(g->player.pos, g->terrain.lm.lighthouse)));
        if (d < 22.0f) {
            g->visitedLighthouse = true;
            ql->progress = 1;
            quest_complete(g, 5);
        }
    }
    // Автовыдача второго задания Никиты после первого
    if (g->quests[0].state == QS_DONE && g->quests[1].state == QS_ACTIVE && g->quests[2].state == QS_LOCKED) {
        // (выдаётся в диалоге, здесь только страховка от софтлока)
    }
    check_achievements(g);
}
