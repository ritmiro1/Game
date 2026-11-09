// ============================================================================
//  world.c — процедурный рельеф, вода, время суток и погода
// ============================================================================
#include "core.h"

// --------------------------------------------------------------- шум значений
static inline uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static inline float hash2f(int x, int y, uint32_t seed) {
    return (float)(hash2(x, y, seed) & 0xFFFFFF) * (1.0f / 16777215.0f);
}
static float vnoise(float x, float y, uint32_t seed) {
    float fx = floorf(x), fy = floorf(y);
    int ix = (int)fx, iy = (int)fy;
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float a = hash2f(ix, iy, seed),     b = hash2f(ix + 1, iy, seed);
    float c = hash2f(ix, iy + 1, seed), d = hash2f(ix + 1, iy + 1, seed);
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}
static float fbm(float x, float y, int oct, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < oct; i++) {
        sum += vnoise(x * freq, y * freq, seed + (uint32_t)i * 7919u) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / norm;
}

// ---------------------------------------------------------------- силуэт карты
// Возвращает высоту в точке до сглаживания сетки (аналитически).
static float terrain_field(float x, float z, uint32_t seed, float *shoreScalarOut) {
    float r = sqrtf(x * x + z * z);
    float ang = atan2f(z, x);
    // Неровная береговая линия: несколько гармоник
    float coast = 1.0f
        + 0.150f * sinf(ang * 3.0f + 1.15f)
        + 0.095f * sinf(ang * 5.0f - 2.30f)
        + 0.055f * sinf(ang * 9.0f + 0.70f)
        + 0.030f * sinf(ang * 14.0f - 1.10f);
    float d = r / (212.0f * coast);   // 1.0 = граница суши

    // Залив: вытянутая впадина суши на юго-востоке
    float bx = (x - 118.0f), bz = (z - 96.0f);
    float bay = expf(-(bx * bx * 0.000055f + bz * bz * 0.000020f));

    float hills = fbm(x * 0.0072f + 11.0f, z * 0.0072f - 5.0f, 5, seed);
    float ridge = fbm(x * 0.017f - 3.0f, z * 0.017f + 8.0f, 3, seed + 55u);
    float landH = 3.4f + hills * hills * 46.0f + ridge * 5.0f;
    // Горный хребет на западе
    float west = smoothstepf(0.45f, 1.0f, saturatef((-x + 40.0f) / 200.0f));
    landH += west * 16.0f * (0.4f + ridge);
    // Пляж: мягкий склон у берега
    float beach = smoothstepf(0.62f, 1.02f, d);
    landH = lerpf(landH, 1.15f + ridge * 0.9f, beach);

    float deep = -2.0f - 26.0f * smoothstepf(1.0f, 2.05f, d) - 4.0f * hills;
    float s = smoothstepf(0.70f, 1.045f, d);
    float h = lerpf(landH, deep, s);
    h -= bay * 16.0f * (1.0f - s);
    if (shoreScalarOut) *shoreScalarOut = d;
    return h;
}

// --------------------------------------------------------- генерация рельефа
void WorldGenerate(Game *g, uint32_t seed) {
    Terrain *t = &g->terrain;
    g->worldSeed = seed;
    const float step = TERRAIN_STEP;
    const float x0 = -WORLD_HALF;
    float minH = 1e9f, maxH = -1e9f;

    for (int j = 0; j < TERRAIN_RES; j++) {
        for (int i = 0; i < TERRAIN_RES; i++) {
            float x = x0 + (float)i * step;
            float z = x0 + (float)j * step;
            // Дополнительная мелкая детализация для правдоподобия
            float detail = (fbm(x * 0.09f, z * 0.09f, 3, seed + 991u) - 0.5f) * 0.55f;
            float h = terrain_field(x, z, seed, NULL) + detail;
            t->height[j * TERRAIN_RES + i] = h;
            if (h < minH) minH = h;
            if (h > maxH) maxH = h;
        }
    }
    // Сглаживающий проход (убирает «шум-пилу» на мелких сетках)
    static float tmp[TERRAIN_RES * TERRAIN_RES];
    memcpy(tmp, t->height, sizeof(tmp));
    for (int pass = 0; pass < 2; pass++) {
        for (int j = 1; j < TERRAIN_RES - 1; j++) {
            for (int i = 1; i < TERRAIN_RES - 1; i++) {
                int idx = j * TERRAIN_RES + i;
                float s = tmp[idx] * 4.0f
                        + tmp[idx - 1] + tmp[idx + 1]
                        + tmp[idx - TERRAIN_RES] + tmp[idx + TERRAIN_RES];
                t->height[idx] = s / 8.0f;
            }
        }
        memcpy(tmp, t->height, sizeof(tmp));
    }
    t->minH = minH; t->maxH = maxH;

    // Нормали и проходимость
    for (int j = 0; j < TERRAIN_RES; j++) {
        for (int i = 0; i < TERRAIN_RES; i++) {
            int idx = j * TERRAIN_RES + i;
            int il = i > 0 ? idx - 1 : idx;
            int ir = i < TERRAIN_RES - 1 ? idx + 1 : idx;
            int jd = j > 0 ? idx - TERRAIN_RES : idx;
            int ju = j < TERRAIN_RES - 1 ? idx + TERRAIN_RES : idx;
            float hl = t->height[il], hr = t->height[ir];
            float hd = t->height[jd], hu = t->height[ju];
            Vec3 n = v3norm(v3((hl - hr) * 0.5f, step, (hd - hu) * 0.5f));
            t->normalX[idx] = n.x; t->normalY[idx] = n.y; t->normalZ[idx] = n.z;
            float slope = 1.0f - n.y;
            t->walk[idx] = (t->height[idx] > 0.55f) ? smoothstepf(0.55f, 0.22f, slope) : 0.0f;
        }
    }
    // Камни, деревья, камыш
    WorldPlaceProps(g);
    // Ориентиры
    Landmarks *lm = &t->lm;
    lm->islandCenter = v3(0, 0, 0);

    // Самая высокая точка — под маяк
    float bestH = -1e9f; Vec3 best = v3(0, 0, 0);
    for (int j = 2; j < TERRAIN_RES - 2; j += 2) {
        for (int i = 2; i < TERRAIN_RES - 2; i += 2) {
            float h = t->height[j * TERRAIN_RES + i];
            float x = x0 + i * step, z = x0 + j * step;
            float r = sqrtf(x * x + z * z);
            if (r < 40.0f || r > 190.0f) continue;
            if (h > bestH) { bestH = h; best = v3(x, h, z); }
        }
    }
    lm->tallest = best;
    // Маяк ставим на достижимый прибрежный холм (8..22 м), а не на вершину
    Vec3 lightBest = best;
    float lightScore = -1e9f;
    for (int j = 2; j < TERRAIN_RES - 2; j += 2) {
        for (int i = 2; i < TERRAIN_RES - 2; i += 2) {
            int idx = j * TERRAIN_RES + i;
            float h = t->height[idx];
            float x = x0 + i * step, z = x0 + j * step;
            float r = sqrtf(x * x + z * z);
            if (h < 7.0f || h > 24.0f) continue;
            if (r < 130.0f || r > 205.0f) continue;
            float slope = 1.0f - t->normalY[idx];
            if (slope > 0.30f) continue;
            // ближе к северо-западу — «мысовой» маяк
            float dirScore = -(x * x + z * z) * 0.0000f;
            float sc = h * 1.6f - slope * 30.0f + dirScore + (z < 0.0f ? 6.0f : 0.0f);
            if (sc > lightScore) { lightScore = sc; lightBest = v3(x, h, z); }
        }
    }
    lm->lighthouse = lightBest;

    // Пристань: ищем берег, ближайший к желаемой точке на юге
    float wantX = 55.0f, wantZ = 165.0f;
    Vec3 bestShore = v3(0, 0, 0);
    float bestScore = 1e9f, bestOutX = 0, bestOutZ = 1;
    for (int k = 0; k < 240; k++) {
        float a = (float)k / 240.0f * 2.0f * PI;
        float dx = cosf(a), dz = sinf(a);
        float sh = WorldShoreAt(g, dx, dz, 300.0f);
        Vec3 p = v3(dx * sh, 0.25f, dz * sh);
        float sc = (p.x - wantX) * (p.x - wantX) + (p.z - wantZ) * (p.z - wantZ);
        if (sc < bestScore) { bestScore = sc; bestShore = p; bestOutX = dx; bestOutZ = dz; }
    }
    // Корень причала ставим на сухое место (не в воде)
    Vec3 dockRoot = bestShore;
    for (int k = 0; k < 40; k++) {
        if (WorldHeight(g, dockRoot.x - bestOutX * 0.5f, dockRoot.z - bestOutZ * 0.5f) > 0.45f) break;
        dockRoot.x -= bestOutX * 0.5f; dockRoot.z -= bestOutZ * 0.5f;
    }
    dockRoot.y = WorldHeight(g, dockRoot.x, dockRoot.z);
    bestShore = dockRoot;
    lm->dockPos = bestShore;
    lm->dockYaw = atan2f(bestOutX, bestOutZ);
    lm->dockEnd = v3(bestShore.x + bestOutX * 13.0f, 1.6f, bestShore.z + bestOutZ * 13.0f);
    lm->boatStart = v3(bestShore.x + bestOutX * 19.0f, 0.1f, bestShore.z + bestOutZ * 19.0f);
    lm->boatYaw = lm->dockYaw;
    Vec3 inward = v3(-bestOutX, 0, -bestOutZ);
    lm->campPos = v3(bestShore.x + inward.x * 9.0f, 0, bestShore.z + inward.z * 9.0f);
    lm->campPos.y = WorldHeight(g, lm->campPos.x, lm->campPos.z);
    lm->shopPos = v3(bestShore.x + inward.x * 15.0f + inward.z * 9.0f, 0,
                     bestShore.z + inward.z * 15.0f - inward.x * 9.0f);
    lm->shopPos.y = WorldHeight(g, lm->shopPos.x, lm->shopPos.z);
    lm->firePos = v3(lm->campPos.x + 2.0f, lm->campPos.y, lm->campPos.z + 1.5f);
    lm->spawnPos = v3(lm->dockPos.x + inward.x * 4.0f, lm->dockPos.y, lm->dockPos.z + inward.z * 4.0f);
    lm->spawnPos.y = WorldHeight(g, lm->spawnPos.x, lm->spawnPos.z) + 0.1f;
}

// -------------------------------- размещение декора (детерминировано по сиду)
void WorldPlaceProps(Game *g) {
    Terrain *t = &g->terrain;
    Rng r; rng_seed(&r, g->worldSeed ^ 0x51ED270Bu);
    t->treeCount = 0; t->rockCount = 0; t->reedCount = 0; t->grassCount = 0;
    const float step = 6.4f;
    for (float z = -300.0f; z < 300.0f; z += step) {
        for (float x = -300.0f; x < 300.0f; x += step) {
            float jx = x + rng_range(&r, -3.2f, 3.2f);
            float jz = z + rng_range(&r, -3.2f, 3.2f);
            float h = WorldHeight(g, jx, jz);
            Vec3 n = WorldNormal(g, jx, jz);
            float slope = 1.0f - n.y;
            float forest = fbm(jx * 0.011f + 4.0f, jz * 0.011f - 9.0f, 3, g->worldSeed + 31u);
            float rDist = sqrtf(jx * jx + jz * jz);

            if (h > 1.6f && h < 52.0f && slope < 0.42f && rDist < 255.0f) {
                if (forest > 0.47f && rng_f(&r) < 0.34f && t->treeCount < MAX_TREES) {
                    Prop *p = &t->trees[t->treeCount++];
                    p->pos = v3(jx, h, jz);
                    p->scale = rng_range(&r, 0.72f, 1.5f);
                    p->yaw = rng_range(&r, 0.0f, 2.0f * PI);
                    p->type = (h > 9.0f || forest > 0.62f) ? 0 : (rng_f(&r) < 0.7f ? 1 : 2);
                } else if (rng_f(&r) < 0.05f && t->grassCount < MAX_REEDS) {
                    Prop *p = &t->grass[t->grassCount++];
                    p->pos = v3(jx, h, jz);
                    p->scale = rng_range(&r, 0.6f, 1.35f);
                    p->yaw = rng_range(&r, 0.0f, 2.0f * PI);
                    p->type = 5;
                }
            }
            // Камыш у самой воды
            if (h > -0.35f && h < 0.85f && rng_f(&r) < 0.62f && t->reedCount < MAX_REEDS) {
                Prop *p = &t->reeds[t->reedCount++];
                p->pos = v3(jx, h, jz);
                p->scale = rng_range(&r, 0.55f, 1.25f);
                p->yaw = rng_range(&r, 0.0f, 2.0f * PI);
                p->type = 3;
            }
            // Валуны: пляж и склоны
            if (rng_f(&r) < 0.16f && t->rockCount < MAX_ROCKS) {
                if ((h > -1.2f && h < 2.4f) || (slope > 0.3f && h > 2.0f)) {
                    if (rDist < 265.0f) {
                        Prop *p = &t->rocks[t->rockCount++];
                        p->pos = v3(jx, h - 0.15f * rng_f(&r), jz);
                        p->scale = rng_range(&r, 0.5f, 2.1f);
                        p->yaw = rng_range(&r, 0.0f, 2.0f * PI);
                        p->type = 4;
                    }
                }
            }
        }
    }
}

// -------------------------------------------------------------- запрос высоты
float WorldHeight(const Game *g, float x, float z) {
    const Terrain *t = &g->terrain;
    float fx = (x + WORLD_HALF) / TERRAIN_STEP;
    float fz = (z + WORLD_HALF) / TERRAIN_STEP;
    if (fx < 0) fx = 0;
    if (fz < 0) fz = 0;
    if (fx > TERRAIN_RES - 1.001f) fx = TERRAIN_RES - 1.001f;
    if (fz > TERRAIN_RES - 1.001f) fz = TERRAIN_RES - 1.001f;
    int i = (int)fx, j = (int)fz;
    float tx = fx - i, tz = fz - j;
    int idx = j * TERRAIN_RES + i;
    float h00 = t->height[idx], h10 = t->height[idx + 1];
    float h01 = t->height[idx + TERRAIN_RES], h11 = t->height[idx + TERRAIN_RES + 1];
    return lerpf(lerpf(h00, h10, tx), lerpf(h01, h11, tx), tz);
}

Vec3 WorldNormal(const Game *g, float x, float z) {
    const Terrain *t = &g->terrain;
    float fx = clampf((x + WORLD_HALF) / TERRAIN_STEP, 0, TERRAIN_RES - 1.001f);
    float fz = clampf((z + WORLD_HALF) / TERRAIN_STEP, 0, TERRAIN_RES - 1.001f);
    int i = (int)fx, j = (int)fz;
    float tx = fx - i, tz = fz - j;
    int idx = j * TERRAIN_RES + i;
    float nx = lerpf(lerpf(t->normalX[idx], t->normalX[idx + 1], tx),
                     lerpf(t->normalX[idx + TERRAIN_RES], t->normalX[idx + TERRAIN_RES + 1], tx), tz);
    float ny = lerpf(lerpf(t->normalY[idx], t->normalY[idx + 1], tx),
                     lerpf(t->normalY[idx + TERRAIN_RES], t->normalY[idx + TERRAIN_RES + 1], tx), tz);
    float nz = lerpf(lerpf(t->normalZ[idx], t->normalZ[idx + 1], tx),
                     lerpf(t->normalZ[idx + TERRAIN_RES], t->normalZ[idx + TERRAIN_RES + 1], tx), tz);
    return v3norm(v3(nx, ny, nz));
}

float WorldWaterDepth(const Game *g, float x, float z) { return SEA_LEVEL - WorldHeight(g, x, z); }

// ---------------------------------------------------------------- волны (CPU)
// ВАЖНО: эта формула полностью повторяется в water.vs (шейдер)!
float WaveHeight(Vec2 p, float t, float wind) {
    float w = 0.35f + wind * 0.85f;
    float h = 0.0f;
    h += 0.210f * w * sinf(p.x * 0.0820f + t * 0.92f);
    h += 0.160f * w * sinf(p.z * 0.1010f - t * 1.13f + 1.37f);
    h += 0.115f * w * sinf((p.x + p.z) * 0.0560f + t * 0.71f + 2.11f);
    h += 0.062f * w * sinf((p.x * 0.92f - p.z * 0.62f) * 0.1310f + t * 1.87f + 4.02f);
    h += 0.034f * w * sinf((p.x * 0.4f + p.z * 0.9f) * 0.2210f - t * 2.55f + 0.83f);
    return h;
}

Vec3 WaveNormalApprox(Vec2 p, float t, float wind) {
    const float e = 1.1f;
    float hL = WaveHeight((Vec2){ p.x - e, p.z }, t, wind);
    float hR = WaveHeight((Vec2){ p.x + e, p.z }, t, wind);
    float hD = WaveHeight((Vec2){ p.x, p.z - e }, t, wind);
    float hU = WaveHeight((Vec2){ p.x, p.z + e }, t, wind);
    return v3norm(v3((hL - hR) / (2.0f * e), 1.0f, (hD - hU) / (2.0f * e)));
}

// --------------------------------------------------------------- берег/вода
float WorldShoreAt(const Game *g, float dirX, float dirZ, float maxDist) {
    float lastLand = 0.0f;
    for (float d = 8.0f; d < maxDist; d += 1.5f) {
        float h = WorldHeight(g, dirX * d, dirZ * d);
        if (h > 0.15f) lastLand = d;
        else if (lastLand > 0.0f && h < -0.6f) break;
    }
    return lastLand + 1.0f;
}

// -------------------------------------------------------------- время/погода
static void weather_targets(WeatherKind k, float *cloud, float *rain, float *fog, float *wind) {
    switch (k) {
        case W_CLEAR:  *cloud = 0.16f; *rain = 0.0f;  *fog = 0.10f; *wind = 0.22f; break;
        case W_CLOUDY: *cloud = 0.62f; *rain = 0.0f;  *fog = 0.26f; *wind = 0.42f; break;
        case W_RAIN:   *cloud = 0.86f; *rain = 0.75f; *fog = 0.42f; *wind = 0.62f; break;
        case W_FOG:    *cloud = 0.55f; *rain = 0.05f; *fog = 0.92f; *wind = 0.12f; break;
        case W_STORM:  *cloud = 1.0f;  *rain = 1.0f;  *fog = 0.58f; *wind = 1.0f;  break;
        default: break;
    }
}

void WeatherRoll(Game *g) {
    Environment *e = &g->env;
    Rng *r = &g->rng;
    float roll = rng_f(r);
    WeatherKind nw = W_CLEAR;
    if (roll < 0.34f)      nw = W_CLEAR;
    else if (roll < 0.63f) nw = W_CLOUDY;
    else if (roll < 0.80f) nw = W_RAIN;
    else if (roll < 0.92f) nw = W_FOG;
    else                   nw = W_STORM;
    if (nw == e->weather && rng_f(r) < 0.5f) nw = (nw + 1) % W_COUNT;
    e->weather = nw;
    e->weatherTimer = rng_range(r, 150.0f, 330.0f);
    float a = rng_range(r, 0.0f, 2.0f * PI);
    e->windDir = (Vec2){ cosf(a), sinf(a) };
}

void EnvComputeSky(Environment *e) {
    float hour = e->timeOfDay;
    float a = (hour - 6.0f) / 12.0f * PI;
    Vec3 sun = v3(cosf(a), sinf(a), 0.38f);
    sun = v3norm(sun);
    e->sunDir = sun;
    float elev = sun.y;                       // -1..1 (синус высоты)
    float day = smoothstepf(-0.10f, 0.22f, elev);
    float twilight = smoothstepf(-0.22f, 0.05f, elev) * (1.0f - smoothstepf(0.10f, 0.42f, elev));

    // Ночные оттенки
    Vec3 nTop = v3(0.016f, 0.026f, 0.062f);
    Vec3 nHor = v3(0.045f, 0.075f, 0.135f);
    Vec3 dTop = v3(0.255f, 0.470f, 0.815f);
    Vec3 dHor = v3(0.680f, 0.805f, 0.905f);
    Vec3 tTop = v3(0.190f, 0.255f, 0.470f);
    Vec3 tHor = v3(0.960f, 0.560f, 0.330f);

    Vec3 top = v3lerp(nTop, dTop, day);
    Vec3 hor = v3lerp(nHor, dHor, day);
    top = v3lerp(top, tTop, twilight * 0.85f);
    hor = v3lerp(hor, tHor, twilight);

    // Пасмурность выбеливает небо
    float cloud = e->cloudAmount;
    hor = v3lerp(hor, v3(hor.x * 0.86f + 0.09f, hor.y * 0.88f + 0.09f, hor.z * 0.90f + 0.10f), cloud * 0.75f);
    top = v3lerp(top, v3(top.x * 0.85f + 0.10f, top.y * 0.86f + 0.10f, top.z * 0.90f + 0.12f), cloud * 0.60f);

    e->skyTop = top;
    e->skyHorizon = hor;

    // Солнце
    float sunUp = saturatef(elev * 3.0f);
    Vec3 warm = v3(1.00f, 0.84f, 0.62f);
    Vec3 noon = v3(1.00f, 0.96f, 0.90f);
    e->sunColor = v3lerp(warm, noon, sunUp);
    e->sunIntensity = sunUp * (1.0f - cloud * 0.55f);
    if (elev < 0.0f) {   // Луна
        e->sunColor = v3lerp(v3(0.52f, 0.60f, 0.86f), v3(0.72f, 0.78f, 0.95f), saturatef(-elev * 2.0f));
        e->sunIntensity = saturatef(-elev * 1.6f) * 0.30f * (1.0f - cloud * 0.5f);
        e->sunDir = v3mul(sun, -1.0f);
        e->sunDir.y = fabsf(e->sunDir.y) * 0.85f + 0.12f;
        e->sunDir = v3norm(e->sunDir);
    }
    // Рассеянный свет
    Vec3 amb = v3(hor.x * 0.55f, hor.y * 0.60f, hor.z * 0.70f);
    amb = v3add(amb, v3(0.055f, 0.060f, 0.075f));
    amb = v3add(amb, v3mul(e->sunColor, e->sunIntensity * 0.22f));
    e->ambient = amb;
    e->fogAmount = e->fogAmount;
}

static void apply_weather(Game *g, float dt) {
    Environment *e = &g->env;
    float tc = 0.5f, tr = 0.5f, tf = 0.5f, tw = 0.5f;
    weather_targets(e->weather, &tc, &tr, &tf, &tw);
    float k = 1.0f - expf(-dt * 0.22f);
    e->cloudAmount = lerpf(e->cloudAmount, tc, k);
    e->rainAmount  = lerpf(e->rainAmount, tr, k);
    e->fogAmount   = lerpf(e->fogAmount, tf, k);
    e->wind        = lerpf(e->wind, tw, k);

    if (e->weather == W_STORM) {
        e->lightningTimer -= dt;
        if (e->lightningTimer <= 0.0f) {
            e->lightningTimer = rng_range(&g->rng, 4.0f, 13.0f);
            e->lightningFlash = 1.0f;
        }
    }
    e->lightningFlash = fmaxf(0.0f, e->lightningFlash - dt * 2.4f);

    e->weatherTimer -= dt;
    if (e->weatherTimer <= 0.0f) WeatherRoll(g);
}

void EnvUpdate(Game *g, float dt) {
    Environment *e = &g->env;
    e->dayFraction += dt / DAY_SECONDS;
    if (e->dayFraction >= 1.0f) { e->dayFraction -= 1.0f; e->day++; }
    e->timeOfDay = e->dayFraction * 24.0f;
    apply_weather(g, dt);
    EnvComputeSky(e);
    g->waterTime += dt * (0.65f + e->wind * 0.85f);
}

bool EnvIsNight(const Environment *e) {
    return e->timeOfDay < 5.4f || e->timeOfDay > 20.2f;
}
float EnvSunElevation(const Environment *e) { return e->sunDir.y; }

int WorldClampToMap(const Game *g, Vec3 *p, float margin) {
    (void)g;
    int hit = 0;
    float lim = WORLD_HALF - margin;
    if (p->x < -lim) { p->x = -lim; hit = 1; }
    if (p->x > lim)  { p->x = lim;  hit = 1; }
    if (p->z < -lim) { p->z = -lim; hit = 1; }
    if (p->z > lim)  { p->z = lim;  hit = 1; }
    return hit;
}
