// ============================================================================
//  npc.c — жители залива: навигация A*, конечные автоматы, диалоги
// ============================================================================
#include "core.h"

// ------------------------------------------------------------------ сетка A*
static uint8_t navWalk[NAV_RES * NAV_RES];
static float   navCell = (2.0f * WORLD_HALF) / (float)NAV_RES;
static bool    navReady = false;

static inline int nav_ix(float world) {
    int i = (int)((world + WORLD_HALF) / navCell);
    return i < 0 ? 0 : (i >= NAV_RES ? NAV_RES - 1 : i);
}
static inline float nav_wx(int i) { return -WORLD_HALF + ((float)i + 0.5f) * navCell; }

static bool nav_free(int i, int j) {
    if (i < 0 || j < 0 || i >= NAV_RES || j >= NAV_RES) return false;
    return navWalk[j * NAV_RES + i] != 0;
}

static void nav_build(Game *g) {
    const Terrain *t = &g->terrain;
    float cell = TERRAIN_STEP;
    for (int j = 0; j < NAV_RES; j++) {
        for (int i = 0; i < NAV_RES; i++) {
            float x = nav_wx(i), z = nav_wx(j);
            float h = WorldHeight(g, x, z);
            Vec3 n = WorldNormal(g, x, z);
            float slope = 1.0f - n.y;
            bool ok = (h > 0.75f) && (slope < 0.52f) && (sqrtf(x * x + z * z) < 250.0f);
            navWalk[j * NAV_RES + i] = ok ? 1 : 0;
        }
    }
    (void)t; (void)cell;
    navReady = true;
}

// Простейший A* с бинарной пирамидой
typedef struct { int idx; float f; } HeapNode;
static HeapNode heap[NAV_RES * NAV_RES];
static int heapCount = 0;
static void heap_push(int idx, float f) {
    int i = heapCount++;
    heap[i].idx = idx; heap[i].f = f;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (heap[p].f <= heap[i].f) break;
        HeapNode tmp = heap[p]; heap[p] = heap[i]; heap[i] = tmp;
        i = p;
    }
}
static HeapNode heap_pop(void) {
    HeapNode top = heap[0];
    heapCount--;
    heap[0] = heap[heapCount];
    int i = 0;
    for (;;) {
        int l = i * 2 + 1, r = l + 1, s = i;
        if (l < heapCount && heap[l].f < heap[s].f) s = l;
        if (r < heapCount && heap[r].f < heap[s].f) s = r;
        if (s == i) break;
        HeapNode tmp = heap[s]; heap[s] = heap[i]; heap[i] = tmp;
        i = s;
    }
    return top;
}

static int gScore[NAV_RES * NAV_RES];
static int came[NAV_RES * NAV_RES];
static uint8_t closed[NAV_RES * NAV_RES];

static int find_path(Game *g, Vec3 from, Vec3 to, Vec3 *out, int maxOut) {
    (void)g;
    if (!navReady) return 0;
    int si = nav_ix(from.x), sj = nav_ix(from.z);
    int ti = nav_ix(to.x), tj = nav_ix(to.z);
    // Если целевая клетка непроходима — ищем ближайшую проходимую
    if (!nav_free(ti, tj)) {
        int best = 1 << 20, bi = ti, bj = tj;
        for (int dj = -4; dj <= 4; dj++) {
            for (int di = -4; di <= 4; di++) {
                int ni = ti + di, nj = tj + dj;
                if (!nav_free(ni, nj)) continue;
                int d = di * di + dj * dj;
                if (d < best) { best = d; bi = ni; bj = nj; }
            }
        }
        if (best >= (1 << 20)) return 0;
        ti = bi; tj = bj;
    }
    if (!nav_free(si, sj)) {
        // Сдвигаем старт к ближайшей проходимой клетке
        int best = 1 << 20, bi = si, bj = sj;
        for (int dj = -3; dj <= 3; dj++) {
            for (int di = -3; di <= 3; di++) {
                int ni = si + di, nj = sj + dj;
                if (!nav_free(ni, nj)) continue;
                int d = di * di + dj * dj;
                if (d < best) { best = d; bi = ni; bj = nj; }
            }
        }
        if (best >= (1 << 20)) return 0;
        si = bi; sj = bj;
    }
    if (si == ti && sj == tj) return 0;

    heapCount = 0;
    int n = NAV_RES * NAV_RES;
    for (int i = 0; i < n; i++) { gScore[i] = 1 << 30; came[i] = -1; closed[i] = 0; }
    int start = sj * NAV_RES + si, target = tj * NAV_RES + ti;
    gScore[start] = 0;
    heap_push(start, 0.0f);
    int found = 0;
    int guard = 0;
    while (heapCount > 0 && guard++ < 20000) {
        HeapNode hn = heap_pop();
        int cur = hn.idx;
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == target) { found = 1; break; }
        int ci = cur % NAV_RES, cj = cur / NAV_RES;
        for (int dj = -1; dj <= 1; dj++) {
            for (int di = -1; di <= 1; di++) {
                if (di == 0 && dj == 0) continue;
                int ni = ci + di, nj = cj + dj;
                if (!nav_free(ni, nj)) continue;
                if (di != 0 && dj != 0) {
                    if (!nav_free(ci + di, cj) || !nav_free(ci, cj + dj)) continue; // без срезания углов
                }
                int nidx = nj * NAV_RES + ni;
                if (closed[nidx]) continue;
                int cost = gScore[cur] + ((di != 0 && dj != 0) ? 14 : 10);
                if (cost < gScore[nidx]) {
                    gScore[nidx] = cost;
                    came[nidx] = cur;
                    int dx = abs(ni - ti), dy = abs(nj - tj);
                    float hcost = (float)(dx + dy) * 10.0f;
                    heap_push(nidx, (float)cost + hcost);
                }
            }
        }
    }
    if (!found) return 0;
    // Раскручиваем путь
    int tmp[512]; int cnt = 0;
    int cur = target;
    while (cur != -1 && cnt < 512) { tmp[cnt++] = cur; cur = came[cur]; }
    int outN = 0;
    // Прореживаем: берём каждую 2-ю клетку, для плавности
    for (int k = cnt - 1; k >= 0 && outN < maxOut; k -= 2) {
        int idx = tmp[k];
        out[outN++] = v3(nav_wx(idx % NAV_RES), 0, nav_wx(idx / NAV_RES));
    }
    return outN;
}

// ------------------------------------------------------------------ создание
static void npc_setup(Game *g, int i, const char *name, const char *role, int kind,
                      Vec3 home, float vision, int questId, bool merchant) {
    Npc *n = &g->npc[i];
    memset(n, 0, sizeof(*n));
    n->name = name; n->role = role; n->kind = kind;
    n->home = home;
    n->pos = home;
    n->pos.y = WorldHeight(g, home.x, home.z);
    if (n->pos.y < 0.4f) n->pos.y = 0.4f;
    n->yaw = atan2f(-home.x, -home.z);
    n->speed = (kind == 1) ? 2.2f : (kind == 2 ? 3.4f : 1.15f);
    n->state = NS_PATROL;
    n->visionRange = vision;
    n->visionAngle = 0.9f;
    n->questId = questId;
    n->isMerchant = merchant;
    n->waypointCount = 0;
}

void NpcInitAll(Game *g) {
    nav_build(g);
    Landmarks *lm = &g->terrain.lm;

    Vec3 n0 = v3(lm->dockPos.x + 3.0f, 0, lm->dockPos.z - 2.0f);
    npc_setup(g, 0, "Дедушка Никита", "старый рыбак", 0, n0, 16.0f, 0, false);
    g->npc[0].waypointCount = 3;
    g->npc[0].waypoints[0] = n0;
    g->npc[0].waypoints[1] = v3(lm->dockEnd.x, 0, lm->dockEnd.z - 1.0f);
    g->npc[0].waypoints[2] = v3(lm->campPos.x + 2.0f, 0, lm->campPos.z + 2.0f);

    Vec3 n1 = v3(lm->lighthouse.x + 6.0f, 0, lm->lighthouse.z + 6.0f);
    n1.y = WorldHeight(g, n1.x, n1.z);
    npc_setup(g, 1, "Смотритель Илья", "смотритель маяка", 0, n1, 20.0f, 3, false);
    g->npc[1].waypointCount = 2;
    g->npc[1].waypoints[0] = n1;
    g->npc[1].waypoints[1] = v3(lm->lighthouse.x - 5.0f, 0, lm->lighthouse.z + 8.0f);

    Vec3 n2 = lm->shopPos;
    n2.y = WorldHeight(g, n2.x, n2.z);
    npc_setup(g, 2, "Торговка Аня", "скупщица рыбы", 0, n2, 18.0f, 2, true);
    g->npc[2].waypointCount = 1;
    g->npc[2].waypoints[0] = n2;

    // Тюлени на камнях у мыса
    for (int s = 0; s < 2; s++) {
        float a = lm->dockYaw + 1.2f + (float)s * 0.9f;
        float d = 60.0f;
        Vec3 p = v3(lm->boatStart.x + cosf(a) * d, 0, lm->boatStart.z + sinf(a) * d);
        npc_setup(g, 3 + s, "Тюлень", "морской житель", 1, p, 12.0f, -1, false);
        g->npc[3 + s].home = p;
    }
    // Пёс Барс — бегает по берегу и гоняет чаек
    Vec3 dogPos = v3(lm->campPos.x + 4.0f, 0, lm->campPos.z - 3.0f);
    npc_setup(g, 5, "Пёс Барс", "верный спутник", 2, dogPos, 22.0f, -1, false);
    g->npc[5].waypointCount = 2;
    g->npc[5].waypoints[0] = dogPos;
    g->npc[5].waypoints[1] = v3(lm->dockEnd.x - 2.0f, 0, lm->dockEnd.z - 6.0f);
}

// ------------------------------------------------------------------ автоматы
static bool npc_sees(const Npc *n, Vec3 target) {
    Vec3 d = v3sub(target, n->pos);
    float dist = v3len(v3flat(d));
    if (dist > n->visionRange) return false;
    Vec3 fwd = v3(sinf(n->yaw), 0, cosf(n->yaw));
    float dot = v3dot(v3norm(v3flat(d)), fwd);
    return dot > cosf(n->visionAngle) || dist < 3.0f;
}

static void npc_move_towards(Game *g, Npc *n, Vec3 target, float dt) {
    Vec3 d = v3flat(v3sub(target, n->pos));
    float dist = v3len(d);
    if (dist < 0.6f) { n->animPhase += dt * 1.5f; return; }
    Vec3 dirn = v3mul(d, 1.0f / dist);
    float spd = n->speed;
    if (n->kind == 2 && n->state == NS_ALERT) spd *= 2.0f;
    Vec3 next = v3add(n->pos, v3mul(dirn, spd * dt));
    float gh = WorldHeight(g, next.x, next.z);
    float curH = WorldHeight(g, n->pos.x, n->pos.z);
    if (gh < -0.2f) return;                       // в воду не заходим
    if (gh - curH > 1.2f) return;                  // слишком круто
    next.y = gh;
    n->pos = next;
    n->yaw = angle_lerp(n->yaw, atan2f(dirn.x, dirn.z), saturatef(dt * 4.0f));
    n->animPhase += dt * spd * 2.6f;
}

void NpcUpdate(Game *g, float dt) {
    if (!navReady) nav_build(g);
    for (int i = 0; i < MAX_NPC; i++) {
        Npc *n = &g->npc[i];
        if (n->name == NULL) continue;
        n->stateTimer -= dt;
        n->repathTimer -= dt;
        n->headBob = sinf(n->animPhase) * 0.035f;
        float distPlayer = v3len(v3flat(v3sub(g->player.pos, n->pos)));
        bool sees = npc_sees(n, g->player.pos);

        if (n->kind == 0) {
            // --- люди: патруль и реакция на игрока
            if (sees && distPlayer < 3.4f) {
                if (n->state != NS_TALK) { n->state = NS_TALK; }
                // Поворачиваемся к игроку
                Vec3 d = v3flat(v3sub(g->player.pos, n->pos));
                n->yaw = angle_lerp(n->yaw, atan2f(d.x, d.z), saturatef(dt * 5.0f));
                n->animPhase += dt;
            } else if (n->state == NS_TALK && distPlayer > 5.0f) {
                n->state = NS_PATROL;
            } else if (n->state == NS_PATROL) {
                if (n->waypointCount > 0) {
                    Vec3 wp = n->waypoints[n->waypointIndex];
                    wp.y = WorldHeight(g, wp.x, wp.z);
                    if (v3len(v3flat(v3sub(wp, n->pos))) < 1.4f) {
                        n->state = NS_IDLE;
                        n->stateTimer = rng_range(&g->rng, 3.0f, 9.0f);
                        n->waypointIndex = (n->waypointIndex + 1) % n->waypointCount;
                    } else {
                        if (n->pathLen == 0 && n->repathTimer <= 0.0f) {
                            n->pathLen = find_path(g, n->pos, wp, n->path, 48);
                            n->pathIdx = 0;
                            n->repathTimer = 2.5f;
                            if (n->pathLen == 0) n->stateTimer = 2.0f;
                        }
                        if (n->pathLen > 0 && n->pathIdx < n->pathLen) {
                            npc_move_towards(g, n, n->path[n->pathIdx], dt);
                            if (v3len(v3flat(v3sub(n->path[n->pathIdx], n->pos))) < 1.0f) n->pathIdx++;
                            if (n->pathIdx >= n->pathLen) { n->pathLen = 0; }
                        } else {
                            npc_move_towards(g, n, wp, dt);
                        }
                    }
                } else {
                    n->state = NS_IDLE;
                }
            } else if (n->state == NS_IDLE) {
                n->animPhase += dt * 0.8f;
                if (n->stateTimer <= 0.0f) { n->state = NS_PATROL; n->pathLen = 0; }
            } else if (n->state == NS_ALERT) {
                Vec3 d = v3flat(v3sub(g->player.pos, n->pos));
                n->yaw = angle_lerp(n->yaw, atan2f(d.x, d.z), saturatef(dt * 3.0f));
                if (n->stateTimer <= 0.0f) n->state = NS_PATROL;
            }
        } else if (n->kind == 1) {
            // --- тюлень: греется, при приближении уходит в воду
            if (sees && distPlayer < 11.0f) {
                n->state = NS_ALERT;
                n->stateTimer = 5.0f;
            }
            if (n->state == NS_ALERT) {
                Vec3 away = v3norm(v3flat(v3sub(n->pos, g->player.pos)));
                Vec3 next = v3add(n->pos, v3mul(away, n->speed * dt));
                float dep = WorldWaterDepth(g, next.x, next.z);
                if (dep > 0.35f) {
                    n->pos = next;
                    n->pos.y = lerpf(n->pos.y, SEA_LEVEL - 0.25f, saturatef(dt * 2.0f));
                } else {
                    n->pos = v3add(n->pos, v3mul(away, 0.4f * dt));
                }
                n->yaw = angle_lerp(n->yaw, atan2f(away.x, away.z), saturatef(dt * 4.0f));
                n->animPhase += dt * 6.0f;
                if (n->stateTimer <= 0.0f) { n->state = NS_PATROL; }
            } else {
                // Возвращается на камень
                Vec3 home = n->home;
                float hd = v3len(v3flat(v3sub(home, n->pos)));
                if (hd > 1.5f) {
                    Vec3 dirn = v3norm(v3flat(v3sub(home, n->pos)));
                    n->pos = v3add(n->pos, v3mul(dirn, n->speed * 0.5f * dt));
                    n->pos.y = dampf(n->pos.y, SEA_LEVEL - 0.15f, 1.6f, dt);
                    n->yaw = angle_lerp(n->yaw, atan2f(dirn.x, dirn.z), saturatef(dt * 2.0f));
                    n->animPhase += dt * 2.0f;
                } else {
                    n->pos.y = dampf(n->pos.y, WorldHeight(g, n->pos.x, n->pos.z) + 0.15f, 1.2f, dt);
                    n->animPhase += dt * 0.5f;
                }
            }
        } else if (n->kind == 2) {
            // --- пёс: бегает по берегу, «гонится» за целью игрока
            float chaseRange = 26.0f;
            if (distPlayer < chaseRange && distPlayer > 3.2f) {
                n->state = NS_ALERT;
                if (n->repathTimer <= 0.0f) {
                    n->pathLen = find_path(g, n->pos, g->player.pos, n->path, 48);
                    n->pathIdx = 0;
                    n->repathTimer = 1.1f;
                }
                if (n->pathLen > 0 && n->pathIdx < n->pathLen) {
                    npc_move_towards(g, n, n->path[n->pathIdx], dt);
                    if (v3len(v3flat(v3sub(n->path[n->pathIdx], n->pos))) < 1.2f) n->pathIdx++;
                    if (n->pathIdx >= n->pathLen) n->pathLen = 0;
                } else {
                    npc_move_towards(g, n, g->player.pos, dt);
                }
            } else {
                if (n->state != NS_PATROL) { n->state = NS_PATROL; n->pathLen = 0; }
                if (n->waypointCount > 0) {
                    Vec3 wp = n->waypoints[n->waypointIndex];
                    if (v3len(v3flat(v3sub(wp, n->pos))) < 2.0f) {
                        n->waypointIndex = (n->waypointIndex + 1) % n->waypointCount;
                    } else {
                        npc_move_towards(g, n, wp, dt);
                    }
                }
            }
        }
    }
}

int NpcNearest(Game *g, float maxDist) {
    int best = -1;
    float bd = maxDist;
    for (int i = 0; i < MAX_NPC; i++) {
        Npc *n = &g->npc[i];
        if (n->name == NULL || n->kind != 0) continue;
        float d = v3len(v3flat(v3sub(g->player.pos, n->pos)));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

// ------------------------------------------------------------------- диалоги
static void add_line(Game *g, int *count, const char *text) {
    if (*count >= MAX_DIALOG) return;
    snprintf(g->dialogText[*count], 512, "%s", text);
    (*count)++;
}

void NpcTalkBegin(Game *g, int npcIndex) {
    if (npcIndex < 0 || npcIndex >= MAX_NPC) return;
    Npc *n = &g->npc[npcIndex];
    if (n->kind != 0) return;
    memset(g->dialogText, 0, sizeof(g->dialogText));
    g->dialogLine = 0;
    g->dialogActive = true;
    g->dialogNpc = npcIndex;
    n->met = true;
    g->npcsMet = 0;
    for (int i = 0; i < MAX_NPC; i++) if (g->npc[i].name && g->npc[i].kind == 0 && g->npc[i].met) g->npcsMet++;

    int c = 0;
    char buf[512];
    bool night = EnvIsNight(&g->env);

    if (npcIndex == 0) {
        if (g->quests[0].state == QS_LOCKED) {
            add_line(g, &c, "Никита: «А-а, новенький. Я тут тридцать лет сети ставлю. Залив наш тихий, да рыба в нём — с характером.»");
            add_line(g, &c, "Никита: «Возьми-ка удочку да поймай три рыбы. Только не торопись: тут главное — тишина и терпение.»");
            add_line(g, &c, "Никита: «Клюёт — не зевай. Поклёвка короткая, подсекай сразу. А вываживай плавно, иначе леска не выдержит.»");
            g->dialogChoice = 10;   // выдать квест 0
        } else if (g->quests[0].state == QS_ACTIVE) {
            snprintf(buf, sizeof(buf), "Никита: «Ну как, рыбак? Поймал %d из %d. Не спеши, залив ждать умеет.»",
                     g->quests[0].progress, g->quests[0].targetCount);
            add_line(g, &c, buf);
            if (night) add_line(g, &c, "Никита: «Ночью тут особенно тихо. Судак выходит, а иногда и Лунная рыба...»");
            else add_line(g, &c, "Никита: «Червя не жалей, но и не ведро сразу. Рыба жадность чует.»");
        } else {
            add_line(g, &c, "Никита: «Вот это улов, внучок! Так и до легенды недалеко.»");
            if (g->quests[1].state == QS_LOCKED) {
                add_line(g, &c, "Никита: «Слушай, будь другом: Ане нужна плотва к ужину. Две штуки, да окуня в придачу.»");
                g->dialogChoice = 11;
            } else if (g->quests[1].state == QS_ACTIVE) {
                snprintf(buf, sizeof(buf), "Никита: «Плотва с окунем для Ани — %d из %d. Она ждёт.»",
                         g->quests[1].progress, g->quests[1].targetCount);
                add_line(g, &c, buf);
            } else {
                add_line(g, &c, "Никита: «Аня довольна. Говорит, уха вышла знатная.»");
            }
        }
        add_line(g, &c, "Никита: «Иди с богом. Ветер сегодня с моря — стало быть, к вечеру разгуляется.»");
    } else if (npcIndex == 1) {
        if (!g->visitedLighthouse) {
            add_line(g, &c, "Илья: «Дошёл всё-таки. Маяк мой видно с любой точки залива, а тропа одна — по северному мысу.»");
            g->visitedLighthouse = true;
        } else {
            add_line(g, &c, "Илья: «Снова ты. Хорошо. Свет маяка держит лодки подальше от рифов.»");
        }
        if (g->quests[3].state == QS_LOCKED) {
            add_line(g, &c, "Илья: «Служба у меня простая: ночью зажечь, утром погасить. Но рыба ночная — отдельная история.»");
            add_line(g, &c, "Илья: «Поймай двух рыб после заката. Тогда узнаешь, каков залив без солнца.»");
            g->dialogChoice = 13;
        } else if (g->quests[3].state == QS_ACTIVE) {
            snprintf(buf, sizeof(buf), "Илья: «Ночной улов: %d из %d. Фонарь держи ближе к воде.»",
                     g->quests[3].progress, g->quests[3].targetCount);
            add_line(g, &c, buf);
        } else {
            add_line(g, &c, "Илья: «Ночью тут красиво, да. Только не забудь: туман приходит быстро.»");
        }
        if (g->quests[4].state == QS_LOCKED && g->quests[3].state == QS_DONE) {
            add_line(g, &c, "Илья: «Есть тут одна... Щука-легенда. Больше пяти килограммов. Возьмёшь такую — поставлю тебе свечу в маяке.»");
            g->dialogChoice = 14;
        }
    } else if (npcIndex == 2) {
        add_line(g, &c, "Аня: «Свежая рыба? Давай сюда, взвесим честно. Мелкую не беру, а трофейную — по двойной цене.»");
        if (g->quests[2].state == QS_LOCKED) {
            add_line(g, &c, "Аня: «И вот что: принеси мне две плотвы и окуня. Заплачу сверху — мне для ухи на весь берег.»");
            g->dialogChoice = 12;
        } else if (g->quests[2].state == QS_ACTIVE) {
            snprintf(buf, sizeof(buf), "Аня: «Заказ мой: %d из %d. И не тяни, уха сама себя не сварит.»",
                     g->quests[2].progress, g->quests[2].targetCount);
            add_line(g, &c, buf);
        } else {
            add_line(g, &c, "Аня: «Уха удалась! Заходи, у меня и наживка, и новые снасти.»");
        }
        add_line(g, &c, "Аня: «Смотри товар: удилища, приманки, мотор для лодки. Всё честное, с моря.»");
        add_line(g, &c, "Аня: «Рыбу сдавать — клавиша E у прилавка. Открыть магазин — тоже E»");
        g->dialogChoice = (g->quests[2].state == QS_LOCKED) ? 12 : 20;
    }
    g->screen = g->screen; // экран диалога ставит app-слой
}

void NpcTalkAdvance(Game *g) {
    if (!g->dialogActive) return;
    g->dialogLine++;
    int used = 0;
    for (int i = 0; i < MAX_DIALOG; i++) if (g->dialogText[i][0]) used = i + 1;
    if (g->dialogLine >= used) NpcTalkEnd(g);
}

void NpcTalkEnd(Game *g) {
    if (!g->dialogActive) return;
    g->dialogActive = false;
    // Применяем выбор диалога
    switch (g->dialogChoice) {
        case 10: if (g->quests[0].state == QS_LOCKED) QuestAccept(g, 0); break;
        case 11: if (g->quests[1].state == QS_LOCKED) QuestAccept(g, 1); break;
        case 12: if (g->quests[2].state == QS_LOCKED) QuestAccept(g, 2); break;
        case 13: if (g->quests[3].state == QS_LOCKED) QuestAccept(g, 3); break;
        case 14: if (g->quests[4].state == QS_LOCKED) QuestAccept(g, 4); break;
        case 20: g->screen = GS_SHOP; break;
        default: break;
    }
    g->dialogChoice = 0;
}
