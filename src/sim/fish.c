// ============================================================================
//  fish.c — база видов рыб и поведение подводной фауны
// ============================================================================
#include "core.h"

static FishSpecies DB[MAX_FISH_SPECIES];
static int DBCount = 0;

const FishSpecies *FishGet(int id) {
    if (id < 0 || id >= DBCount) id = 0;
    return &DB[id];
}
int FishSpeciesCount(void) { return DBCount; }

static void add_species(const char *name, const char *latin, const char *desc,
                        float minKg, float maxKg, int rarity, float dMin, float dMax,
                        int hFrom, int hTo, float strength, int price,
                        Vec3 colA, Vec3 colB, float len, float hgt, int fin) {
    if (DBCount >= MAX_FISH_SPECIES) return;
    FishSpecies *f = &DB[DBCount];
    f->name = name; f->latin = latin; f->desc = desc;
    f->minKg = minKg; f->maxKg = maxKg; f->rarity = rarity;
    f->depthMin = dMin; f->depthMax = dMax;
    f->hourFrom = hFrom; f->hourTo = hTo;
    for (int i = 0; i < W_COUNT; i++) f->weatherBonus[i] = 1.0f;
    f->strength = strength; f->pricePerKg = price;
    f->colorA = colA; f->colorB = colB;
    f->bodyLen = len; f->bodyHeight = hgt; f->finStyle = fin;
    DBCount++;
}

void FishDBInit(void) {
    DBCount = 0;
    add_species("Окунь", "Perca fluviatilis",
        "Полосатый хозяин прибрежных зарослей. Берёт охотно почти в любую погоду.",
        0.12f, 1.20f, 0, 1.0f, 8.0f, 5, 22, 0.34f, 95,
        v3(0.42f, 0.50f, 0.24f), v3(0.86f, 0.72f, 0.30f), 0.34f, 0.34f, 0);
    DB[0].weatherBonus[W_CLOUDY] = 1.25f; DB[0].weatherBonus[W_RAIN] = 1.15f;

    add_species("Плотва", "Rutilus rutilus",
        "Серебристая стайная рыбка. Верный спутник утра на тихой воде.",
        0.06f, 0.90f, 0, 0.8f, 6.0f, 4, 20, 0.28f, 70,
        v3(0.72f, 0.76f, 0.80f), v3(0.88f, 0.64f, 0.42f), 0.28f, 0.30f, 0);
    add_species("Краснопёрка", "Scardinius erythrophthalmus",
        "Ярко-красные плавники выдают её издалека. Держится у камыша.",
        0.10f, 0.85f, 0, 1.0f, 4.5f, 7, 19, 0.30f, 85,
        v3(0.80f, 0.66f, 0.30f), v3(0.92f, 0.34f, 0.24f), 0.27f, 0.32f, 0);

    add_species("Лещ", "Abramis brama",
        "Широкий, неторопливый, осторожный. Клюёт на зорьке и в тумане.",
        0.40f, 3.60f, 1, 2.0f, 10.0f, 4, 9, 0.52f, 115,
        v3(0.55f, 0.50f, 0.38f), v3(0.78f, 0.74f, 0.62f), 0.46f, 0.46f, 0);
    DB[3].weatherBonus[W_FOG] = 1.45f;
    DB[3].weatherBonus[W_CLEAR] = 1.1f;

    add_species("Щука", "Esox lucius",
        "Зубастая хищница. Берёт живца на рассвете и в дождь, любит коряги.",
        1.00f, 9.50f, 2, 2.0f, 13.0f, 5, 10, 0.82f, 185,
        v3(0.30f, 0.42f, 0.26f), v3(0.66f, 0.72f, 0.44f), 0.72f, 0.22f, 1);
    DB[4].weatherBonus[W_RAIN] = 1.35f; DB[4].weatherBonus[W_STORM] = 1.2f;

    add_species("Судак", "Sander lucioperca",
        "Ночной охотник с колючим плавником. Стеклянные глаза видят в темноте.",
        0.80f, 6.20f, 2, 4.0f, 17.0f, 20, 4, 0.86f, 205,
        v3(0.46f, 0.50f, 0.54f), v3(0.72f, 0.74f, 0.68f), 0.62f, 0.26f, 1);

    add_species("Карп", "Cyprinus carpio",
        "Тяжёлый, упрямый, умный. Вываживание требует терпения и крепкой лески.",
        2.00f, 14.00f, 2, 2.5f, 11.0f, 5, 11, 0.95f, 155,
        v3(0.62f, 0.44f, 0.22f), v3(0.86f, 0.72f, 0.44f), 0.78f, 0.40f, 0);

    add_species("Форель", "Salmo trutta",
        "Холодная вода и каменистое дно. Пятнистая, стремительная, пугливая.",
        0.30f, 4.20f, 3, 1.5f, 8.0f, 5, 10, 0.74f, 225,
        v3(0.50f, 0.36f, 0.26f), v3(0.90f, 0.64f, 0.52f), 0.52f, 0.28f, 1);
    DB[7].weatherBonus[W_FOG] = 1.3f;

    add_species("Угорь", "Anguilla anguilla",
        "Скользкий обитатель глубины. В дождь подходит ближе к берегу.",
        0.50f, 3.20f, 3, 6.0f, 22.0f, 21, 4, 0.90f, 245,
        v3(0.20f, 0.26f, 0.24f), v3(0.44f, 0.48f, 0.34f), 0.88f, 0.12f, 2);

    add_species("Камбала", "Platichthys flesus",
        "Плоская, как ладонь. Лежит на песчаном дне залива.",
        0.20f, 2.60f, 1, 4.0f, 19.0f, 0, 24, 0.56f, 135,
        v3(0.60f, 0.56f, 0.42f), v3(0.80f, 0.78f, 0.62f), 0.36f, 0.42f, 2);

    add_species("Сельдь", "Clupea harengus",
        "Стайная, стремительная, блестит как осколок зеркала.",
        0.12f, 0.70f, 1, 5.0f, 24.0f, 6, 18, 0.42f, 95,
        v3(0.74f, 0.80f, 0.86f), v3(0.52f, 0.62f, 0.76f), 0.24f, 0.22f, 0);

    add_species("Сом", "Silurus glanis",
        "Хозяин глубины. Усы, тёмная спина, чудовищная сила в ночном вываживании.",
        5.00f, 42.00f, 4, 8.0f, 28.0f, 22, 5, 1.00f, 195,
        v3(0.24f, 0.22f, 0.20f), v3(0.46f, 0.42f, 0.36f), 1.10f, 0.26f, 1);

    add_species("Лунная рыба", "Luna argenteus",
        "Легенда залива: светится в туманные ночи и уходит в глубину на рассвете.",
        0.80f, 7.60f, 4, 11.0f, 31.0f, 22, 3, 0.98f, 900,
        v3(0.72f, 0.86f, 0.96f), v3(0.96f, 0.92f, 0.62f), 0.54f, 0.52f, 2);
    DB[DBCount - 1].weatherBonus[W_FOG] = 2.2f;
    DB[DBCount - 1].weatherBonus[W_STORM] = 1.5f;
}

// --------------------------------------------------------------- выбор вида
int FishRollSpecies(Game *g, float depth, int hour, WeatherKind weather) {
    float weight[MAX_FISH_SPECIES];
    float total = 0.0f;
    int lure = g->inv.lureTier;
    for (int i = 0; i < DBCount; i++) {
        const FishSpecies *f = &DB[i];
        float w = 0.0f;
        if (depth >= f->depthMin - 1.0f && depth <= f->depthMax + 1.0f) {
            float mid = (f->depthMin + f->depthMax) * 0.5f;
            float halfSpan = (f->depthMax - f->depthMin) * 0.5f + 0.001f;
            w = 1.0f - 0.55f * saturatef(fabsf(depth - mid) / halfSpan);
        } else continue;
        // Время суток: окно (с переходом через полночь)
        bool inWindow;
        if (f->hourFrom <= f->hourTo) inWindow = (hour >= f->hourFrom && hour <= f->hourTo);
        else inWindow = (hour >= f->hourFrom || hour <= f->hourTo);
        w *= inWindow ? 1.0f : 0.16f;
        // Редкость
        static const float rarityWeight[5] = { 5.2f, 3.0f, 1.45f, 0.62f, 0.20f };
        w *= rarityWeight[f->rarity < 5 ? f->rarity : 4];
        // Погода
        w *= f->weatherBonus[weather];
        // Приманка: более дорогая повышает шанс редкой рыбы
        w *= 1.0f + (float)f->rarity * 0.30f * (float)(lure - 1);
        // Наживка повышает общий клёв
        if (g->inv.bait <= 0) w *= 0.55f;
        if (w < 0.0f) w = 0.0f;
        weight[i] = w;
        total += w;
    }
    if (total <= 0.0001f) return 0;
    float roll = rng_f(&g->rng) * total;
    for (int i = 0; i < DBCount; i++) {
        roll -= weight[i];
        if (roll <= 0.0f) return i;
    }
    return 0;
}

// ------------------------------------------------------------- спавн и жизнь
static bool find_water_spot(Game *g, Vec3 center, float radius, float dMin, float dMax, Vec3 *out) {
    for (int attempt = 0; attempt < 24; attempt++) {
        float a = rng_range(&g->rng, 0.0f, 2.0f * PI);
        float r = rng_range(&g->rng, radius * 0.15f, radius);
        float x = center.x + cosf(a) * r;
        float z = center.z + sinf(a) * r;
        if (fabsf(x) > WORLD_HALF - 6 || fabsf(z) > WORLD_HALF - 6) continue;
        float d = WorldWaterDepth(g, x, z);
        if (d >= dMin && d <= dMax) {
            *out = v3(x, -clampf(d * 0.55f, 0.6f, 8.0f), z);
            return true;
        }
    }
    return false;
}

void FishSpawnAll(Game *g) {
    for (int i = 0; i < MAX_FISH; i++) {
        FishInst *f = &g->fish[i];
        memset(f, 0, sizeof(*f));
        Vec3 spot;
        if (!find_water_spot(g, g->terrain.lm.boatStart, 190.0f, 1.6f, 26.0f, &spot)) continue;
        f->alive = true;
        f->home = spot;
        f->homeRadius = rng_range(&g->rng, 6.0f, 26.0f);
        f->pos = spot;
        f->yaw = rng_range(&g->rng, 0.0f, 2.0f * PI);
        float depth = -spot.y;
        f->species = FishRollSpecies(g, depth, (int)g->env.timeOfDay, g->env.weather);
        f->vel = v3(0, 0, 0);
        f->wiggle = rng_range(&g->rng, 0.0f, 6.0f);
        f->sizeScale = rng_range(&g->rng, 0.75f, 1.25f);
    }
}

static void respawn_fish(Game *g, FishInst *f) {
    Vec3 spot;
    Vec3 anchor = g->player.pos;
    if (!find_water_spot(g, anchor, 150.0f, 1.6f, 26.0f, &spot)) {
        if (!find_water_spot(g, g->terrain.lm.boatStart, 200.0f, 1.6f, 26.0f, &spot)) return;
    }
    f->home = spot;
    f->pos = spot;
    f->alive = true;
    f->homeRadius = rng_range(&g->rng, 6.0f, 26.0f);
    f->yaw = rng_range(&g->rng, 0.0f, 2.0f * PI);
    f->species = FishRollSpecies(g, -spot.y, (int)g->env.timeOfDay, g->env.weather);
}

void FishUpdate(Game *g, float dt) {
    for (int i = 0; i < MAX_FISH; i++) {
        FishInst *f = &g->fish[i];
        if (!f->alive) {
            if (rng_f(&g->rng) < dt * 0.6f) respawn_fish(g, f);
            continue;
        }
        Vec3 toP = v3sub(g->player.pos, f->pos);
        float dist = v3len(toP);
        // Пугливость: лодка и игрок распугивают рыбу
        float scare = (g->player.mode == PM_BOAT) ? 9.5f : 5.5f;
        if (dist < scare) {
            f->fleeTimer = fmaxf(f->fleeTimer, 1.4f);
            Vec3 away = v3norm(v3(-toP.x, 0, -toP.z));
            f->vel = v3add(f->vel, v3mul(away, 6.0f * dt * (1.0f + (scare - dist) * 0.2f)));
        }
        f->fleeTimer = fmaxf(0.0f, f->fleeTimer - dt);

        // Блуждание вокруг дома
        Vec3 toHome = v3sub(f->home, f->pos);
        float homeDist = v3len(v3flat(toHome));
        if (homeDist > f->homeRadius) {
            f->yaw = angle_lerp(f->yaw, atan2f(toHome.x, toHome.z), 1.6f * dt);
        } else {
            f->yaw += sinf(g->totalTime * 0.7f + (float)i * 1.7f) * dt * 0.9f;
        }
        float speedTarget = (f->fleeTimer > 0.0f) ? 3.6f : 0.85f;
        Vec3 fwd = v3(sinf(f->yaw), 0, cosf(f->yaw));
        f->vel = v3lerp(f->vel, v3mul(fwd, speedTarget), saturatef(dt * 2.4f));
        f->pos = v3add(f->pos, v3mul(f->vel, dt));

        // Не выходить на мелководье/сушу
        float depth = WorldWaterDepth(g, f->pos.x, f->pos.z);
        if (depth < 1.1f) {
            Vec3 n = WorldNormal(g, f->pos.x, f->pos.z);
            Vec3 away = v3norm(v3(n.x, 0, n.z));
            f->pos = v3add(f->pos, v3mul(away, dt * (1.6f + (1.1f - depth) * 1.4f)));
            f->yaw = angle_lerp(f->yaw, atan2f(away.x, away.z), 3.0f * dt);
        }
        float targetY = -clampf(depth * 0.55f, 0.7f, 9.0f);
        f->pos.y = dampf(f->pos.y, targetY, 1.4f, dt);
        f->wiggle += dt * (3.4f + v3len(f->vel) * 2.2f);
        if (fabsf(f->pos.x) > WORLD_HALF - 8 || fabsf(f->pos.z) > WORLD_HALF - 8) {
            respawn_fish(g, f);
        } else if (dist > 340.0f && rng_f(&g->rng) < dt * 0.5f) {
            respawn_fish(g, f);
        }
    }
}
