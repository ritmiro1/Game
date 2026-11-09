// ============================================================================
//  player_boat.c — контроллер игрока и физика плавучести лодки
// ============================================================================
#include "core.h"

// Физические константы лодки
#define HULL_HALF_LEN   2.15f
#define HULL_HALF_WID   0.78f
#define BOAT_DECK_Y     0.46f
#define BOAT_DRAFT      0.42f

void PlayerInit(Game *g, Vec3 at) {
    g->player.pos = at;
    g->player.vel = v3(0, 0, 0);
    g->player.yaw = atan2f(-at.x, -at.z);   // смотрим к центру острова
    g->player.pitch = -0.06f;
    g->player.stamina = 1.0f;
    g->player.health = 1.0f;
    g->player.mode = PM_FOOT;
    g->player.grounded = true;
    g->player.bob = 0.0f;
    g->player.stepTimer = 0.0f;
}

void BoatInit(Game *g, Vec3 at, float yaw) {
    Boat *b = &g->boat;
    memset(b, 0, sizeof(*b));
    b->pos = at;
    b->pos.y = WaveHeight((Vec2){ at.x, at.z }, 0.0f, g->env.wind);
    b->yaw = yaw;
    b->enginePower = 1.0f;
}

static Vec3 boat_forward(const Boat *b) { return v3(sinf(b->yaw), 0.0f, cosf(b->yaw)); }
static Vec3 boat_right(const Boat *b)   { return v3(cosf(b->yaw), 0.0f, -sinf(b->yaw)); }

Vec3 BoatDeckPoint(const Boat *b, float lx, float lz) {
    Vec3 f = boat_forward(b), r = boat_right(b);
    Vec3 p = v3add(b->pos, v3add(v3mul(f, lz), v3mul(r, lx)));
    p.y = b->pos.y + BOAT_DECK_Y + lz * sinf(b->pitch) * -1.0f + lx * sinf(b->roll);
    return p;
}

// ---------------------------------------------------------------- обновление
void BoatUpdate(Game *g, float dt) {
    Boat *b = &g->boat;
    Environment *e = &g->env;
    Vec3 fwd = boat_forward(b), right = boat_right(b);

    // --- плавучесть: 4 точки корпуса
    float wl = 0, wr = 0, wb = 0, ws = 0;
    Vec3 bowP = v3add(v3add(b->pos, v3mul(fwd, HULL_HALF_LEN)), v3mul(right, HULL_HALF_WID * 0.7f));
    Vec3 stP  = v3sub(v3add(b->pos, v3mul(fwd, -HULL_HALF_LEN)), v3mul(right, HULL_HALF_WID * 0.7f));
    Vec3 porP = v3add(b->pos, v3mul(right, HULL_HALF_WID));
    Vec3 stbP = v3sub(b->pos, v3mul(right, HULL_HALF_WID));
    wb = WaveHeight((Vec2){ bowP.x, bowP.z }, g->waterTime, e->wind);
    ws = WaveHeight((Vec2){ stP.x, stP.z }, g->waterTime, e->wind);
    wl = WaveHeight((Vec2){ porP.x, porP.z }, g->waterTime, e->wind);
    wr = WaveHeight((Vec2){ stbP.x, stbP.z }, g->waterTime, e->wind);

    float targetY = (wb + ws + wl + wr) * 0.25f;
    float targetPitch = atan2f(ws - wb, HULL_HALF_LEN * 2.0f) * 0.85f;
    float targetRoll  = atan2f(wl - wr, HULL_HALF_WID * 2.0f) * 0.85f;

    // Скорость по воде
    float speed = v3len(b->vel);
    float load = b->occupied ? 1.0f : 0.92f;

    // Разгон от мотора
    float motor = 1.0f + (float)(g->inv.motorTier - 1) * 0.55f;
    float accel = b->throttle * 3.1f * motor * load;
    if (!b->occupied) accel = 0.0f;              // без рулевого лодка просто дрейфует
    Vec3 force = v3mul(fwd, accel);
    // Сопротивление воды (квадратичное) + ветер
    float dragK = 0.62f;
    force = v3sub(force, v3mul(b->vel, dragK * (0.6f + speed * 0.16f)));
    float windPush = e->wind * 0.55f;
    Vec2 wd = e->windDir;
    force = v3add(force, v3(wd.x * windPush, 0, wd.z * windPush));

    b->vel = v3add(b->vel, v3mul(force, dt));
    if (speed > 9.0f) b->vel = v3mul(v3norm(b->vel), 9.0f);
    b->vel.y = 0.0f;

    // Поворот: руль работает тем сильнее, чем больше ход
    float steerAuthority = 0.25f + saturatef(v3len(b->vel) / 3.5f) * 0.95f;
    float targetYawRate = b->steer * 1.05f * steerAuthority * (b->throttle < -0.1f ? -1.0f : 1.0f);
    b->angVelYaw = dampf(b->angVelYaw, targetYawRate, 6.0f, dt);
    b->yaw += b->angVelYaw * dt;

    Vec3 newPos = v3add(b->pos, v3mul(b->vel, dt));

    // Столкновение с берегом/мелью: выталкивание по градиенту глубины
    float depth = WorldWaterDepth(g, newPos.x, newPos.z);
    if (depth < BOAT_DRAFT + 0.55f) {
        Vec3 n = WorldNormal(g, newPos.x, newPos.z);
        Vec3 push = v3norm(v3(n.x, 0, n.z));
        float need = (BOAT_DRAFT + 0.55f) - depth;
        newPos = v3add(newPos, v3mul(push, fminf(need * 1.8f + 0.05f, 1.6f)));
        b->vel = v3mul(b->vel, 0.55f);
        b->angVelYaw *= 0.6f;
        if (v3len(b->vel) > 1.6f) {
            g->camShake = fmaxf(g->camShake, 0.10f);
            ParticleSpawn(g, v3(newPos.x, 0.1f, newPos.z), v3(0, 1.2f, 0),
                          0.8f, 0.5f, PT_FOAM, v3(0.85f, 0.92f, 0.95f));
        }
    }
    if (WorldClampToMap(g, &newPos, 6.0f)) b->vel = v3mul(b->vel, 0.4f);

    b->pos.x = newPos.x;
    b->pos.z = newPos.z;
    b->pos.y = dampf(b->pos.y, targetY, 5.5f, dt);
    b->pitch = dampf(b->pitch, targetPitch, 6.0f, dt);
    b->roll  = dampf(b->roll,  targetRoll,  5.0f, dt);
    b->bobPhase += dt * (1.2f + e->wind * 2.0f);

    // Кильватерный след
    b->wakeTimer -= dt;
    float spd = v3len(b->vel);
    if (spd > 1.1f && b->wakeTimer <= 0.0f) {
        b->wakeTimer = 0.10f;
        Vec3 stern = v3sub(b->pos, v3mul(fwd, HULL_HALF_LEN));
        for (int i = 0; i < 2; i++) {
            float sw = (i == 0 ? -1.0f : 1.0f) * HULL_HALF_WID;
            Vec3 p = v3add(stern, v3mul(right, sw));
            p.y = WaveHeight((Vec2){ p.x, p.z }, g->waterTime, e->wind) + 0.05f;
            Vec3 v = v3add(v3mul(b->vel, -0.28f), v3(rng_sym(&g->rng) * 0.5f, 1.0f + rng_f(&g->rng) * 0.9f, rng_sym(&g->rng) * 0.5f));
            ParticleSpawn(g, p, v, 1.4f + rng_f(&g->rng) * 1.0f, 0.55f + spd * 0.09f, PT_FOAM,
                          v3(0.92f, 0.96f, 0.98f));
        }
        g->splashAmount = fmaxf(g->splashAmount, saturatef(spd / 6.0f) * 0.4f);
    }

    // Игрок на борту: позиция ног = палуба
    if (b->occupied && g->player.mode == PM_BOAT) {
        Vec3 deck = BoatDeckPoint(b, 0.0f, -0.35f);
        g->player.pos = deck;
        g->player.vel = b->vel;
    }
}

// --------------------------------------------------------------- игрок пешком
void PlayerUpdate(Game *g, float dt, Vec3 moveInput, bool jump, bool sprint, bool crouch) {
    Player *p = &g->player;
    if (p->mode == PM_BOAT) {
        p->bob = sinf(g->boat.bobPhase) * 0.02f;
        return;
    }

    float groundY = WorldHeight(g, p->pos.x, p->pos.z);
    float speedBase = 3.05f;
    if (crouch) speedBase = 1.45f;

    float staminaCost = 0.0f;
    if (sprint && p->stamina > 0.02f && !crouch) {
        speedBase = 5.35f;
        staminaCost = 0.135f;
    }
    if (p->mode == PM_SWIM) {
        speedBase = (sprint && p->stamina > 0.02f) ? 2.6f : 1.75f;
        staminaCost = sprint ? 0.075f : 0.030f;
    }

    // Ускорение/торможение по горизонтали
    Vec3 wish = v3mul(v3norm(moveInput), 1.0f);
    if (v3len2(moveInput) > 0.0001f) wish = v3norm(moveInput);
    else wish = v3(0, 0, 0);
    float accel = (p->grounded || p->mode == PM_SWIM) ? 13.0f : 3.2f;
    Vec3 targetVel = v3mul(wish, speedBase);
    p->vel.x = dampf(p->vel.x, targetVel.x, accel, dt);
    p->vel.z = dampf(p->vel.z, targetVel.z, accel, dt);

    // Прыжок/гравитация
    if (p->mode == PM_SWIM) {
        float surface = SEA_LEVEL - 0.35f;
        p->vel.y = dampf(p->vel.y, (surface - p->pos.y) * 4.0f, 6.0f, dt);
    } else {
        p->vel.y -= 19.0f * dt;
        if (jump && p->grounded) { p->vel.y = 5.35f; p->grounded = false; }
    }

    Vec3 cand = p->pos;
    cand.x += p->vel.x * dt;
    cand.z += p->vel.z * dt;
    cand.y += p->vel.y * dt;

    // Запрет лазать по крутым склонам
    float candGround = WorldHeight(g, cand.x, cand.z);
    float stepUp = candGround - groundY;
    float horiz = sqrtf((cand.x - p->pos.x) * (cand.x - p->pos.x) + (cand.z - p->pos.z) * (cand.z - p->pos.z));
    if (p->mode == PM_FOOT && stepUp > 0.9f && stepUp > horiz * 1.35f) {
        cand.x = p->pos.x;
        cand.z = p->pos.z;
        p->vel.x *= 0.2f;
        p->vel.z *= 0.2f;
        candGround = groundY;
    }
    WorldClampToMap(g, &cand, 4.0f);

    // Прилипание к земле
    if (p->mode == PM_FOOT) {
        if (cand.y <= candGround + 0.03f) {
            cand.y = candGround;
            if (p->vel.y < 0.0f) p->vel.y = 0.0f;
            p->grounded = true;
        } else {
            p->grounded = false;
        }
        // Вход в глубокую воду => плавание
        if (candGround < SEA_LEVEL - 1.15f) {
            p->mode = PM_SWIM;
            p->grounded = false;
            Hint(g, "Вы вошли в воду. Плывите назад к берегу или вернитесь к лодке (F)");
        }
    } else if (p->mode == PM_SWIM) {
        cand.y = clampf(cand.y, SEA_LEVEL - 0.55f, SEA_LEVEL + 0.25f);
        if (candGround > SEA_LEVEL - 0.95f) {
            p->mode = PM_FOOT;
            p->grounded = true;
            p->vel.y = 0.0f;
        }
        if (p->stamina <= 0.001f) {
            p->health -= dt * 0.085f;
            if (p->health <= 0.0f) {
                SimRespawnPlayer(g);
                Toast(g, "Вы выбились из сил... Вас вынесло на берег");
                return;
            }
        }
    }

    p->pos = cand;

    // Стамина
    if (staminaCost > 0.0f) p->stamina = saturatef(p->stamina - staminaCost * dt);
    else p->stamina = saturatef(p->stamina + (p->mode == PM_SWIM ? 0.02f : 0.115f) * dt);
    if (p->mode == PM_FOOT) p->health = saturatef(p->health + dt * 0.02f);

    // Анимация шага/покачивания
    float hspeed = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
    if (p->grounded && hspeed > 0.35f) {
        p->stepTimer += dt * hspeed;
        p->bob += dt * hspeed * 1.75f;
    } else {
        p->bob += dt * 0.6f;
    }
    if (p->stepTimer > 2.4f) {
        p->stepTimer = 0.0f;
        // звук шага определяется поверхностью в app-слое по g->player.pos
        g->camShake = fmaxf(g->camShake, 0.006f);
    }
}

// ------------------------------------------------------------- посадка в лодку
void BoatBoard(Game *g) {
    if (g->player.mode == PM_BOAT) return;
    g->player.mode = PM_BOAT;
    g->boat.occupied = true;
    g->player.pos = BoatDeckPoint(&g->boat, 0.0f, -0.35f);
    g->player.stamina = saturatef(g->player.stamina + 0.35f);
    Toast(g, "Вы в лодке. W/S — тяга, A/D — руль, F — сойти на берег");
}

void BoatLeave(Game *g) {
    if (g->player.mode != PM_BOAT) return;
    Boat *b = &g->boat;
    b->occupied = false;
    b->throttle = 0.0f;
    b->steer = 0.0f;
    Vec3 right = boat_right(b);
    Vec3 side = v3add(b->pos, v3mul(right, 1.9f));
    float depth = WorldWaterDepth(g, side.x, side.z);
    if (depth > 1.0f) side = v3sub(b->pos, v3mul(right, 1.9f));
    depth = WorldWaterDepth(g, side.x, side.z);
    g->player.pos = side;
    if (depth < 0.9f) {
        g->player.pos.y = WorldHeight(g, side.x, side.z);
        g->player.mode = PM_FOOT;
    } else {
        g->player.pos.y = SEA_LEVEL - 0.3f;
        g->player.mode = PM_SWIM;
        Hint(g, "Вы в воде. Плывите к берегу (Shift — быстрее)");
    }
    g->player.vel = v3(0, 0, 0);
}
