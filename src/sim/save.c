// ============================================================================
//  save.c — сохранение/загрузка полного состояния мира в JSON
// ============================================================================
#include "core.h"
#include <stdarg.h>

static char buf[262144];
static size_t blen = 0;

static void w(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + blen, sizeof(buf) - blen, fmt, ap);
    va_end(ap);
    if (n > 0) blen += (size_t)n;
    if (blen > sizeof(buf) - 64) blen = sizeof(buf) - 64;
}
#define JSEP() (blen > 0 ? "," : "")

void BuildSavePath(Game *g) {
    const char *dir = getenv("APPDATA");
    if (dir && dir[0]) {
        snprintf(g->savePath, sizeof(g->savePath), "%s\\TikhiyZaliv\\save.json", dir);
    } else {
        const char *home = getenv("HOME");
        if (home && home[0]) snprintf(g->savePath, sizeof(g->savePath), "%s/.tikhiy_zaliv_save.json", home);
        else snprintf(g->savePath, sizeof(g->savePath), "tikhiy_zaliv_save.json");
    }
}

bool SaveExists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz > 32;
}

bool SaveGame(Game *g, const char *path) {
    blen = 0;
    w("{");
    w("\"version\":1,\"title\":\"TikhiyZaliv\",");
    w("\"seed\":%u,", g->worldSeed);
    w("\"time\":{\"dayFraction\":%.6f,\"day\":%d,\"weather\":%d,\"weatherTimer\":%.2f,\"wind\":%.4f,\"windX\":%.4f,\"windZ\":%.4f,\"rain\":%.3f,\"fog\":%.3f,\"cloud\":%.3f},",
      g->env.dayFraction, g->env.day, (int)g->env.weather, g->env.weatherTimer,
      g->env.wind, g->env.windDir.x, g->env.windDir.z,
      g->env.rainAmount, g->env.fogAmount, g->env.cloudAmount);
    w("\"player\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"yaw\":%.4f,\"pitch\":%.4f,\"mode\":%d,\"stamina\":%.3f,\"health\":%.3f},",
      g->player.pos.x, g->player.pos.y, g->player.pos.z, g->player.yaw, g->player.pitch,
      (int)g->player.mode, g->player.stamina, g->player.health);
    w("\"boat\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"yaw\":%.4f},",
      g->boat.pos.x, g->boat.pos.y, g->boat.pos.z, g->boat.yaw);
    w("\"inv\":{\"coins\":%d,\"rodTier\":%d,\"lureTier\":%d,\"motorTier\":%d,\"bait\":%d,\"lantern\":%d,\"sonar\":%d,\"rodDur\":%.3f,\"items\":[",
      g->inv.coins, g->inv.rodTier, g->inv.lureTier, g->inv.motorTier, g->inv.bait,
      g->inv.hasLantern ? 1 : 0, g->inv.hasSonar ? 1 : 0, g->inv.rodDurability);
    for (int i = 0; i < g->inv.count; i++) {
        Item *it = &g->inv.items[i];
        w("%s[%d,%d,%d,%.3f]", i ? "," : "", (int)it->kind, it->tier, it->fishSpecies, it->weightKg);
    }
    w("]},");
    w("\"progress\":{\"playTime\":%.2f,\"fishCaught\":%d,\"biggestKg\":%.3f,\"nights\":%d,\"storms\":%d,\"lighthouse\":%d,\"npcsMet\":%d,\"coinsEarned\":%d,\"questsDone\":%d},",
      g->playTime, g->fishCaught, g->biggestKg, g->nightsFished, g->stormsFished,
      g->visitedLighthouse ? 1 : 0, g->npcsMet, g->coinsEarned, g->questsCompleted);
    w("\"species\":[");
    for (int i = 0; i < MAX_FISH_SPECIES; i++) w("%s%d", i ? "," : "", g->fishSpecies[i]);
    w("],");
    w("\"quests\":[");
    for (int i = 0; i < MAX_QUESTS; i++)
        w("%s[%d,%d]", i ? "," : "", (int)g->quests[i].state, g->quests[i].progress);
    w("],");
    w("\"ach\":[");
    for (int i = 0; i < MAX_ACHIEVEMENTS; i++)
        w("%s[%d,%.2f]", i ? "," : "", g->ach[i].unlocked ? 1 : 0, g->ach[i].target > 0 ? g->ach[i].progress : 0.0f);
    w("],");
    w("\"npcs\":[");
    for (int i = 0; i < MAX_NPC; i++) w("%s%d", i ? "," : "", g->npc[i].met ? 1 : 0);
    w("],");
    w("\"catches\":[");
    for (int i = 0; i < g->catchCount; i++)
        w("%s[%d,%.3f,%.2f,%d,%d]", i ? "," : "", g->catches[i].species, g->catches[i].weightKg,
          g->catches[i].hour, g->catches[i].day, g->catches[i].weather);
    w("],");
    w("\"settings\":{\"quality\":%d,\"master\":%.3f,\"music\":%.3f,\"sfx\":%.3f,\"ambient\":%.3f,\"sens\":%.3f,\"fov\":%.2f,\"invert\":%d,\"tiltshift\":%d,\"bloom\":%d,\"ssao\":%d,\"rainfx\":%d,\"headbob\":%d,\"shadow\":%d}",
      g->settings.quality, g->settings.masterVolume, g->settings.musicVolume, g->settings.sfxVolume,
      g->settings.ambientVolume, g->settings.sensitivity, g->settings.fov,
      g->settings.invertY ? 1 : 0, g->settings.tiltShift ? 1 : 0, g->settings.bloom ? 1 : 0,
      g->settings.ssao ? 1 : 0, g->settings.rainEffects ? 1 : 0, g->settings.headBob ? 1 : 0,
      g->settings.shadowQuality);
    w("}\n");

    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(buf, 1, blen, f);
    fclose(f);
    return written == blen;
}

// ------------------------------------------------------------------- разбор
static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > (long)sizeof(buf) - 1) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    return buf;
}

static const char *find_key(const char *doc, const char *key) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(doc, pat);
    if (!p) return NULL;
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ') p++;
    return p;
}
static float jf(const char *doc, const char *key, float def) {
    const char *p = find_key(doc, key);
    if (!p) return def;
    return (float)atof(p);
}
static int ji(const char *doc, const char *key, int def) {
    const char *p = find_key(doc, key);
    if (!p) return def;
    return atoi(p);
}
static bool jb(const char *doc, const char *key, bool def) {
    const char *p = find_key(doc, key);
    if (!p) return def;
    return atoi(p) != 0;
}
// Последовательный разбор массива массивов чисел: [[a,b],[c,d]] при key
static void parse_pairs(const char *doc, const char *key, float *out, int maxPairs, int stride) {
    const char *p = find_key(doc, key);
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;
    int idx = 0;
    while (*p && idx < maxPairs) {
        while (*p && *p != '[' && *p != ']') p++;
        if (!*p || *p == ']') break;
        p++;
        for (int s = 0; s < stride; s++) {
            while (*p && (*p == ' ' || *p == ',')) p++;
            out[idx * stride + s] = (float)atof(p);
            while (*p && *p != ',' && *p != ']') p++;
            if (*p == ',') p++;
        }
        while (*p && *p != ']') p++;
        if (*p == ']') p++;
        idx++;
    }
}
static void parse_ints(const char *doc, const char *key, int *out, int maxN) {
    const char *p = find_key(doc, key);
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;
    for (int i = 0; i < maxN; i++) {
        while (*p && (*p == ' ' || *p == ',')) p++;
        if (!*p || *p == ']') break;
        out[i] = atoi(p);
        while (*p && *p != ',' && *p != ']') p++;
    }
}

unsigned int SavePeekSeed(const char *path) {
    char *doc = read_all(path);
    if (!doc) return 0;
    unsigned int seed = 0;
    if (ji(doc, "version", 0) == 1) seed = (unsigned int)ji(doc, "seed", 0);
    free(doc);
    return seed;
}

bool LoadGame(Game *g, const char *path) {
    char *doc = read_all(path);
    if (!doc) return false;
    int total = ji(doc, "version", 0);
    if (total != 1) return false;

    uint32_t seed = (uint32_t)ji(doc, "seed", 20251109);
    // Мир генерируется заново по сиду — детерминированно
    SimNewGame(g, seed);

    g->env.dayFraction = jf(doc, "dayFraction", 0.32f);
    g->env.timeOfDay = g->env.dayFraction * 24.0f;
    g->env.day = ji(doc, "day", 1);
    g->env.weather = (WeatherKind)ji(doc, "weather", W_CLEAR);
    g->env.weatherTimer = jf(doc, "weatherTimer", 200.0f);
    g->env.wind = jf(doc, "wind", 0.3f);
    g->env.windDir = (Vec2){ jf(doc, "windX", 0.7f), jf(doc, "windZ", 0.7f) };
    g->env.rainAmount = jf(doc, "rain", 0.0f);
    g->env.fogAmount = jf(doc, "fog", 0.1f);
    g->env.cloudAmount = jf(doc, "cloud", 0.2f);
    EnvComputeSky(&g->env);

    g->player.pos = v3(jf(doc, "x", 0), jf(doc, "y", 1.0f), jf(doc, "z", 0));
    g->player.yaw = jf(doc, "yaw", 0);
    g->player.pitch = jf(doc, "pitch", 0);
    g->player.mode = (PlayerMode)ji(doc, "mode", PM_FOOT);
    g->player.stamina = jf(doc, "stamina", 1.0f);
    g->player.health = jf(doc, "health", 1.0f);
    g->boat.pos = v3(jf(doc, "boatx", g->terrain.lm.boatStart.x),
                     jf(doc, "boaty", 0.0f),
                     jf(doc, "boatz", g->terrain.lm.boatStart.z));
    // boat.* использует те же ключи x/y/z — читаем из блока boat отдельно
    {
        const char *b = find_key(doc, "boat");
        if (b) {
            const char *e = strchr(b, '}');
            if (e) {
                char sub[256];
                size_t n = (size_t)(e - b);
                if (n > sizeof(sub) - 1) n = sizeof(sub) - 1;
                memcpy(sub, b, n);
                sub[n] = 0;
                g->boat.pos = v3(jf(sub, "x", 0.0f), jf(sub, "y", 0.0f), jf(sub, "z", 0.0f));
                g->boat.yaw = jf(sub, "yaw", 0.0f);
            }
        }
    }
    g->boat.occupied = (g->player.mode == PM_BOAT);

    // Инвентарь
    {
        const char *inv = find_key(doc, "inv");
        if (inv) {
            const char *e = strstr(inv, "\"progress\"");
            if (!e) e = inv + strlen(inv);
            char sub[8192];
            size_t n = (size_t)(e - inv);
            if (n > sizeof(sub) - 1) n = sizeof(sub) - 1;
            memcpy(sub, inv, n);
            sub[n] = 0;
            g->inv.coins = ji(sub, "coins", 60);
            g->inv.rodTier = ji(sub, "rodTier", 1);
            g->inv.lureTier = ji(sub, "lureTier", 1);
            g->inv.motorTier = ji(sub, "motorTier", 1);
            g->inv.bait = ji(sub, "bait", 10);
            g->inv.hasLantern = jb(sub, "lantern", false);
            g->inv.hasSonar = jb(sub, "sonar", false);
            g->inv.rodDurability = jf(sub, "rodDur", 1.0f);
            float items[MAX_ITEMS * 4];
            memset(items, 0, sizeof(items));
            parse_pairs(sub, "items", items, MAX_ITEMS, 4);
            g->inv.count = 0;
            const char *p = find_key(sub, "items");
            int nItems = 0;
            if (p) {
                p = strchr(p, '[');
                if (p) {
                    const char *q = p + 1;
                    while (*q && *q != ']' && nItems < MAX_ITEMS) {
                        if (*q == '[') {
                            nItems++;
                            while (*q && *q != ']') q++;
                        }
                        if (*q) q++;
                    }
                }
            }
            for (int i = 0; i < nItems && i < MAX_ITEMS; i++) {
                Item *it = &g->inv.items[i];
                it->kind = (ItemKind)(int)items[i * 4 + 0];
                it->tier = (int)items[i * 4 + 1];
                it->fishSpecies = (int)items[i * 4 + 2];
                it->weightKg = items[i * 4 + 3];
                it->count = 1;
                it->durability = 1.0f;
            }
            g->inv.count = nItems;
        }
    }

    g->playTime = jf(doc, "playTime", 0.0f);
    g->fishCaught = ji(doc, "fishCaught", 0);
    g->biggestKg = jf(doc, "biggestKg", 0.0f);
    g->nightsFished = ji(doc, "nights", 0);
    g->stormsFished = ji(doc, "storms", 0);
    g->visitedLighthouse = jb(doc, "lighthouse", false);
    g->npcsMet = ji(doc, "npcsMet", 0);
    g->coinsEarned = ji(doc, "coinsEarned", 0);
    g->questsCompleted = ji(doc, "questsDone", 0);

    int sp[MAX_FISH_SPECIES];
    memset(sp, 0, sizeof(sp));
    parse_ints(doc, "species", sp, MAX_FISH_SPECIES);
    for (int i = 0; i < MAX_FISH_SPECIES; i++) g->fishSpecies[i] = sp[i];

    float qd[MAX_QUESTS * 2];
    memset(qd, 0, sizeof(qd));
    parse_pairs(doc, "quests", qd, MAX_QUESTS, 2);
    for (int i = 0; i < MAX_QUESTS; i++) {
        g->quests[i].state = (QuestState)(int)qd[i * 2];
        g->quests[i].progress = (int)qd[i * 2 + 1];
    }
    float ad[MAX_ACHIEVEMENTS * 2];
    memset(ad, 0, sizeof(ad));
    parse_pairs(doc, "ach", ad, MAX_ACHIEVEMENTS, 2);
    for (int i = 0; i < MAX_ACHIEVEMENTS; i++) {
        g->ach[i].unlocked = ad[i * 2] > 0.5f;
        g->ach[i].progress = ad[i * 2 + 1];
    }
    int np[MAX_NPC];
    memset(np, 0, sizeof(np));
    parse_ints(doc, "npcs", np, MAX_NPC);
    for (int i = 0; i < MAX_NPC; i++) g->npc[i].met = np[i] != 0;

    float cd[128 * 5];
    memset(cd, 0, sizeof(cd));
    parse_pairs(doc, "catches", cd, 128, 5);
    g->catchCount = 0;
    for (int i = 0; i < 128; i++) {
        if (cd[i * 5] == 0.0f && cd[i * 5 + 1] == 0.0f) continue;
        CatchRecord *c = &g->catches[g->catchCount++];
        c->species = (int)cd[i * 5];
        c->weightKg = cd[i * 5 + 1];
        c->hour = cd[i * 5 + 2];
        c->day = (int)cd[i * 5 + 3];
        c->weather = (int)cd[i * 5 + 4];
    }

    g->settings.quality = ji(doc, "quality", 2);
    g->settings.masterVolume = jf(doc, "master", 0.9f);
    g->settings.musicVolume = jf(doc, "music", 0.55f);
    g->settings.sfxVolume = jf(doc, "sfx", 0.85f);
    g->settings.ambientVolume = jf(doc, "ambient", 0.75f);
    g->settings.sensitivity = jf(doc, "sens", 1.0f);
    g->settings.fov = jf(doc, "fov", 68.0f);
    g->settings.invertY = jb(doc, "invert", false);
    g->settings.tiltShift = jb(doc, "tiltshift", true);
    g->settings.bloom = jb(doc, "bloom", true);
    g->settings.ssao = jb(doc, "ssao", true);
    g->settings.rainEffects = jb(doc, "rainfx", true);
    g->settings.headBob = jb(doc, "headbob", true);
    g->settings.shadowQuality = ji(doc, "shadow", 2);
    return true;
}
