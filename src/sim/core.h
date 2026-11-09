// ============================================================================
//  ТИХИЙ ЗАЛИВ (Calm Bay) — core.h
//  Общие типы, математика и состояние мира. Слой симуляции НЕ зависит от
//  графической библиотеки: он собирается и тестируется автономно.
// ============================================================================
#ifndef TB_CORE_H
#define TB_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TB_VERSION "1.0.0"
#define TB_TITLE   "ТИХИЙ ЗАЛИВ"

#ifndef PI
#define PI 3.14159265358979f
#endif
#ifndef DEG2RAD
#define DEG2RAD (PI / 180.0f)
#endif
#ifndef RAD2DEG
#define RAD2DEG (180.0f / PI)
#endif

// ---------------------------------------------------------------- математика
typedef struct { float x, y, z; } Vec3;
typedef struct { float x, z; }    Vec2;

static inline Vec3 v3(float x, float y, float z) { Vec3 r = { x, y, z }; return r; }
static inline Vec3 v3add(Vec3 a, Vec3 b)  { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 v3sub(Vec3 a, Vec3 b)  { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 v3mul(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline Vec3 v3mulv(Vec3 a, Vec3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline float v3dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 v3cross(Vec3 a, Vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3len(Vec3 a) { return sqrtf(v3dot(a, a)); }
static inline float v3len2(Vec3 a) { return v3dot(a, a); }
static inline Vec3 v3norm(Vec3 a) {
    float l = v3len(a);
    if (l < 1e-6f) return v3(0, 0, 0);
    return v3mul(a, 1.0f / l);
}
static inline Vec3 v3lerp(Vec3 a, Vec3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static inline Vec3 v3flat(Vec3 a) { return v3(a.x, 0, a.z); }
static inline float v2len(Vec2 a) { return sqrtf(a.x * a.x + a.z * a.z); }
static inline Vec2 v2sub(Vec2 a, Vec2 b) { Vec2 r = { a.x - b.x, a.z - b.z }; return r; }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float saturatef(float v) { return clampf(v, 0.0f, 1.0f); }
static inline float smoothstepf(float e0, float e1, float x) {
    float t = saturatef((x - e0) / (e1 - e0 + 1e-9f));
    return t * t * (3.0f - 2.0f * t);
}
static inline float move_toward(float cur, float target, float maxDelta) {
    float d = target - cur;
    if (fabsf(d) <= maxDelta) return target;
    return cur + (d > 0 ? maxDelta : -maxDelta);
}
static inline float angle_wrap(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
static inline float angle_lerp(float a, float b, float t) { return a + angle_wrap(b - a) * t; }
// Плавное приближение с экспоненциальным затуханием, независимое от FPS
static inline float dampf(float cur, float target, float lambda, float dt) {
    return lerpf(cur, target, 1.0f - expf(-lambda * dt));
}
// Направление по углу (yaw: 0 => +Z)
static inline Vec3 dir_from_yaw_pitch(float yaw, float pitch) {
    float cp = cosf(pitch);
    return v3(sinf(yaw) * cp, sinf(pitch), cosf(yaw) * cp);
}

// ------------------------------------------------------------------ генератор
typedef struct { uint32_t s0, s1, s2, s3; } Rng;
static inline void rng_seed(Rng *r, uint32_t seed) {
    uint32_t x = seed ? seed : 0x9E3779B9u;
    r->s0 = x ^ 0xA5A5A5A5u; r->s1 = x * 1812433253u + 1u;
    r->s2 = x * 69069u + 12345u; r->s3 = x ^ 0xDEADBEEFu;
    for (int i = 0; i < 16; i++) {
        uint32_t t = r->s0 ^ (r->s0 << 11);
        r->s0 = r->s1; r->s1 = r->s2; r->s2 = r->s3;
        r->s3 = r->s3 ^ (r->s3 >> 19) ^ (t ^ (t >> 8));
    }
}
static inline uint32_t rng_u32(Rng *r) {
    uint32_t t = r->s0 ^ (r->s0 << 11);
    r->s0 = r->s1; r->s1 = r->s2; r->s2 = r->s3;
    r->s3 = r->s3 ^ (r->s3 >> 19) ^ (t ^ (t >> 8));
    return r->s3;
}
static inline float rng_f(Rng *r) { return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
static inline float rng_range(Rng *r, float a, float b) { return a + (b - a) * rng_f(r); }
static inline int rng_int(Rng *r, int a, int b) { return a + (int)(rng_u32(r) % (uint32_t)(b - a + 1)); }
static inline float rng_sym(Rng *r) { return rng_f(r) * 2.0f - 1.0f; }

// ------------------------------------------------------------- мир: константы
#define WORLD_HALF     340.0f     // половина размера карты (м)
#define TERRAIN_RES    257        // узлов сетки рельефа (257x257)
#define TERRAIN_STEP   (2.0f * WORLD_HALF / (float)(TERRAIN_RES - 1))
#define SEA_LEVEL      0.0f
#define DAY_SECONDS    (22.0f * 60.0f)   // полный цикл суток в реальных секундах
#define MAX_FISH       96
#define MAX_NPC        6
#define MAX_TREES      520
#define MAX_ROCKS      240
#define MAX_REEDS      620
#define MAX_PARTICLES  768
#define MAX_ACHIEVEMENTS 14
#define MAX_ITEMS      64
#define MAX_FISH_SPECIES 13
#define MAX_QUESTS     6
#define MAX_DIALOG     6
#define NAV_RES        128          // сетка навигации для A*

// ------------------------------------------------------------- декор мира
typedef struct {
    Vec3  pos;
    float scale;
    float yaw;
    int   type;      // вид объекта: 0 ель, 1 сосна, 2 куст, 3 камыш, 4 валун, 5 трава
} Prop;

typedef struct {
    Vec3 dockPos;      float dockYaw;
    Vec3 dockEnd;      // конец причала (над водой)
    Vec3 boatStart;    float boatYaw;
    Vec3 lighthouse;   // основание маяка
    Vec3 campPos;      // лагерь игрока
    Vec3 spawnPos;
    Vec3 shopPos;      // торговка
    Vec3 firePos;
    Vec3 islandCenter;
    Vec3 tallest;
} Landmarks;

// ------------------------------------------------------------------- рельеф
typedef struct {
    float height[TERRAIN_RES * TERRAIN_RES];   // высота над уровнем моря
    float normalX[TERRAIN_RES * TERRAIN_RES];  // нормаль (для освещения/наклона)
    float normalY[TERRAIN_RES * TERRAIN_RES];
    float normalZ[TERRAIN_RES * TERRAIN_RES];
    float walk[TERRAIN_RES * TERRAIN_RES];     // 0..1 проходимость для A*
    float minH, maxH;
    Prop  trees[MAX_TREES];   int treeCount;
    Prop  rocks[MAX_ROCKS];   int rockCount;
    Prop  reeds[MAX_REEDS];   int reedCount;
    Prop  grass[MAX_REEDS];   int grassCount;
    Landmarks lm;
} Terrain;

// -------------------------------------------------------------------- время
typedef enum { W_CLEAR = 0, W_CLOUDY, W_RAIN, W_FOG, W_STORM, W_COUNT } WeatherKind;

typedef struct {
    float timeOfDay;      // 0..24 (часы)
    float dayFraction;    // 0..1
    int   day;            // номер дня
    WeatherKind weather;
    WeatherKind nextWeather;
    float weatherBlend;   // 0..1 переход к nextWeather
    float weatherTimer;   // до следующей смены
    float rainAmount;     // 0..1
    float fogAmount;      // 0..1
    float cloudAmount;    // 0..1
    float wind;           // сила ветра
    Vec2  windDir;
    float lightningTimer;
    float lightningFlash; // 0..1 (вспышка молнии)
    Vec3  sunDir;
    Vec3  sunColor;
    Vec3  skyTop, skyHorizon, ambient;
    float sunIntensity;
    float underwater;     // 0..1 насколько камера под водой
} Environment;

// --------------------------------------------------------------------- лодка
typedef struct {
    Vec3  pos;
    float yaw, pitch, roll;
    Vec3  vel;
    float angVelYaw;
    float throttle;       // -1..1 (реверс/вперёд)
    float steer;          // -1..1
    float enginePower;    // модификатор мотора
    float bobPhase;
    bool  anchored;
    bool  occupied;       // игрок на борту
    float wakeTimer;
} Boat;

// -------------------------------------------------------------------- игрок
typedef enum { PM_FOOT = 0, PM_BOAT, PM_SWIM } PlayerMode;

typedef struct {
    Vec3  pos;            // позиция ног
    float yaw, pitch;
    Vec3  vel;
    float stamina;        // 0..1
    float health;         // 0..1
    PlayerMode mode;
    bool  grounded;
    bool  crouch;
    float bob;
    float stepTimer;
    float interactCooldown;
    float reelStaminaDrain;
} Player;

// ---------------------------------------------------------------- виды рыб
typedef struct {
    const char *name;
    const char *latin;
    const char *desc;
    float minKg, maxKg;
    int   rarity;         // 0 обычная .. 4 легендарная
    float depthMin, depthMax;
    int   hourFrom, hourTo;      // окно активности
    float weatherBonus[W_COUNT]; // бонус к клёву по погоде
    float strength;       // сопротивление при вываживании
    int   pricePerKg;
    Vec3  colorA, colorB;
    float bodyLen;        // длина модели (м)
    float bodyHeight;     // относительная высота тела
    int   finStyle;       // 0 обычные, 1 длинные, 2 веер
} FishSpecies;

// ------------------------------------------------------------------ рыба-ИИ
typedef enum { FS_WANDER = 0, FS_FLEE, FS_BITE_READY } FishState;

typedef struct {
    bool  alive;
    int   species;
    Vec3  pos;
    Vec3  vel;
    float yaw;
    float wiggle;
    float fleeTimer;
    float sizeScale;
    float homeRadius;
    Vec3  home;
} FishInst;

// ---------------------------------------------------------------------- NPC
typedef enum { NS_IDLE = 0, NS_PATROL, NS_TALK, NS_ALERT } NpcState;

typedef struct {
    const char *name;
    const char *role;
    Vec3  pos;
    float yaw;
    float speed;
    NpcState state;
    int   waypointCount;
    Vec3  waypoints[4];
    int   waypointIndex;
    float stateTimer;
    float animPhase;
    float alertTimer;
    bool  met;              // игрок уже разговаривал
    int   questId;          // какой квест выдаёт (-1 нет)
    int   dialogNode;
    Vec3  path[48];
    int   pathLen, pathIdx;
    float repathTimer;
    float headBob;
    bool  isMerchant;
    int   kind;             // 0 человек, 1 тюлень, 2 пёс
    float visionRange;      // сенсорный конус
    float visionAngle;
    float homeTimer;
    Vec3  home;
} Npc;

// ------------------------------------------------------------------ предметы
typedef enum {
    IT_NONE = 0, IT_ROD, IT_LURE, IT_BAIT, IT_LANTERN, IT_MOTOR, IT_SONAR, IT_FISH, IT_COIN
} ItemKind;

typedef struct {
    ItemKind kind;
    int      tier;        // уровень/качество
    float    durability;  // 0..1
    int      count;
    int      fishSpecies; // для IT_FISH
    float    weightKg;
} Item;

typedef struct {
    Item  items[MAX_ITEMS];
    int   count;
    int   coins;
    int   rodTier;        // 1..3
    int   lureTier;       // 1..3
    int   motorTier;      // 1..2
    bool  hasLantern;
    bool  hasSonar;
    int   bait;           // количество наживки
    int   selectedHotbar; // 0 удочка, 1 приманка, 2 фонарь, 3 сонар
    float rodDurability;
} Inventory;

// -------------------------------------------------------------------- квесты
typedef enum { QS_LOCKED = 0, QS_ACTIVE, QS_DONE } QuestState;

typedef struct {
    const char *title;
    const char *desc;
    const char *hint;
    int   targetCount;
    int   progress;
    QuestState state;
    int   rewardCoins;
    int   giverNpc;
    int   kind;            // 0 рыба всего, 1 конкретный вид, 2 ночью, 3 локация, 4 вес
    int   param;
    float paramF;
} Quest;

// ------------------------------------------------------------------- улов
typedef struct {
    int   species;
    float weightKg;
    float hour;
    int   day;
    int   weather;
} CatchRecord;

// ------------------------------------------------------------- достижения
typedef struct {
    const char *title;
    const char *desc;
    bool  unlocked;
    float progress;
    float target;
    float popupTimer;
} Achievement;

// ------------------------------------------------------------------ частицы
typedef enum { PT_SPLASH = 0, PT_FOAM, PT_SPRAY, PT_SPARKLE, PT_LEAF, PT_RAIN_DROP, PT_SMOKE } ParticleKind;
typedef struct {
    Vec3  pos, vel;
    float life, maxLife, size;
    ParticleKind kind;
    Vec3  color;
} Particle;

// ------------------------------------------------------------------ настройки
typedef struct {
    int   quality;        // 0 низкие, 1 средние, 2 высокие, 3 ультра
    float masterVolume, musicVolume, sfxVolume, ambientVolume;
    float sensitivity;
    float fov;
    bool  invertY;
    bool  tiltShift;      // пост-обработка DOF
    bool  bloom;
    bool  ssao;
    bool  rainEffects;
    bool  headBob;
    int   shadowQuality;
    int   language;       // 0 = русский (единственный доступный)
} Settings;

// ------------------------------------------------------------- состояние игры
typedef enum {
    GS_BOOT = 0, GS_TITLE, GS_PLAY, GS_PAUSE, GS_JOURNAL, GS_MAP,
    GS_ACHIEVEMENTS, GS_SETTINGS, GS_SHOP, GS_DIALOG, GS_IDEAS, GS_CREDITS
} GameScreen;

typedef enum { FISH_IDLE = 0, FISH_CHARGE, FISH_CAST, FISH_FLOAT, FISH_HOOKED, FISH_LANDED } FishPhase;

typedef struct {
    FishPhase phase;
    float charge;          // сила заброса 0..1
    Vec3  bobberPos;
    Vec3  bobberVel;
    bool  bobberInWater;
    float castDistance;
    float floatTimer;
    float biteTimer;       // до поклёвки
    float nibbleTimer;     // предпоклёвка
    int   bitingSpecies;
    float tension;         // 0..1 натяжение лески
    float reelProgress;    // 0..1 вываживание
    float lineLength;
    float rodBend;         // 0..1 изгиб удилища
    float fishPull;        // текущая тяга рыбы
    float hookedWeight;
    int   hookedSpecies;
    float hookedTimer;
    float escapeTimer;
    float lineBreakFlash;
    float lastCatchWeight;
    float lastCatchTime;
    int   lastCatchSpecies;
    bool  biteIndicator;
    float depthAtBobber;
    float drag;           // настройка фрикциона 0.5..1.6
    float biteWindow;     // окно подсечки
    float fightTime;
    float lastNibble;
    int   reeling;
} Fishing;

typedef struct {
    // мир
    Terrain     terrain;
    Environment env;
    Rng         rng;
    uint32_t    worldSeed;
    bool        underwaterCam;
    int         prevInput;
    int         shopSelected;
    int         journalTab;
    char        interactPrompt[128];
    float       interactProgress;
    int         interactNpc;
    int         dialogLineCount;
    Boat        boat;
    Player      player;
    FishInst    fish[MAX_FISH];
    Npc         npc[MAX_NPC];
    Inventory   inv;
    Quest       quests[MAX_QUESTS];
    Achievement ach[MAX_ACHIEVEMENTS];
    CatchRecord catches[128];
    int         catchCount;
    Particle    particles[MAX_PARTICLES];
    Fishing     fish_rod;
    Settings    settings;
    GameScreen  screen;
    GameScreen  prevScreen;

    // прогресс
    float playTime;
    int   fishCaught;
    int   fishSpecies[ MAX_FISH_SPECIES ];
    float biggestKg;
    int   nightsFished;
    int   stormsFished;
    bool  visitedLighthouse;
    int   npcsMet;
    int   coinsEarned;
    int   questsCompleted;
    int   mapMarkers[8];
    int   mapMarkerCount;

    // камера/интерфейс
    Vec3  camPos, camTarget;
    float camShake;
    float toastTimer;
    char  toast[192];
    float hintTimer;
    char  hint[192];
    char  dialogText[6][512];
    int   dialogLine;
    bool  dialogActive;
    int   dialogNpc;
    int   dialogChoice;
    char  savePath[512];
    bool  paused;
    float dt;
    float totalTime;
    // флаги для рендера
    bool  lanternOn;
    bool  sonarOn;
    float lanternFlicker;
    float waterTime;
    Vec3  lastSplashPos;
    float splashAmount;
    bool  caughtPopup;
    float caughtPopupTimer;
    int   caughtPopupSpecies;
    float caughtPopupWeight;
    int   pendingPopupAch;
    char  pendingAchText[128];
} Game;

// ------------------------------------------------------------------- функции
// world.c
void  WorldGenerate(Game *g, uint32_t seed);
void  WorldPlaceProps(Game *g);
float WorldShoreAt(const Game *g, float dirX, float dirZ, float maxDist);
float WorldHeight(const Game *g, float x, float z);
Vec3  WorldNormal(const Game *g, float x, float z);
float WorldWaterDepth(const Game *g, float x, float z);           // >0 под водой
float WaveHeight(Vec2 p, float t, float wind);
Vec3  WaveNormalApprox(Vec2 p, float t, float wind);
void  EnvUpdate(Game *g, float dt);
void  EnvComputeSky(Environment *e);
float EnvSunElevation(const Environment *e);
bool  EnvIsNight(const Environment *e);
void  WeatherRoll(Game *g);
int   WorldClampToMap(const Game *g, Vec3 *p, float margin);

// player_boat.c
void  PlayerInit(Game *g, Vec3 at);
void  PlayerUpdate(Game *g, float dt, Vec3 moveInput, bool jump, bool sprint, bool crouch);
void  BoatInit(Game *g, Vec3 at, float yaw);
void  BoatUpdate(Game *g, float dt);
void  BoatBoard(Game *g);
void  BoatLeave(Game *g);
Vec3  BoatDeckPoint(const Boat *b, float lx, float lz);

// fish.c
void  FishDBInit(void);
const FishSpecies *FishGet(int id);
int   FishSpeciesCount(void);
void  FishSpawnAll(Game *g);
void  FishUpdate(Game *g, float dt);
int   FishRollSpecies(Game *g, float depth, int hour, WeatherKind weather);

// fishing.c
void  FishingInit(Fishing *f);
void  FishingUpdate(Game *g, float dt);
void  FishingSetReeling(Game *g, bool reeling);
bool  FishingTryHook(Game *g);
void  FishingStartCharge(Game *g);
void  FishingReleaseCast(Game *g);
void  FishingHoldReel(Game *g, float dt);
void  FishingCancel(Game *g);

// npc.c
void  NpcInitAll(Game *g);
void  NpcUpdate(Game *g, float dt);
int   NpcNearest(Game *g, float maxDist);
void  NpcTalkBegin(Game *g, int npcIndex);
void  NpcTalkAdvance(Game *g);
void  NpcTalkEnd(Game *g);

// progress.c
void  ProgressInit(Game *g);
void  InventoryAddFish(Game *g, int species, float weight);
int   InventoryFishValue(const Game *g);
int   ShopItemCount(void);
const char *ShopItemName(int i);
const char *ShopItemDesc(int i);
int   ShopItemPrice(int i);
bool  ShopItemOwned(const Game *g, int i);
bool  ShopBuy(Game *g, int i);
void  InventorySellAllFish(Game *g);
void  AchievementAdd(Game *g, float amount, int species, float weight);
void  ProgressTick(Game *g, float dt);
void  QuestAccept(Game *g, int id);
void  QuestNotifyCatch(Game *g, int species, float weight);
void  Toast(Game *g, const char *fmt, ...);
void  ParticleSpawn(Game *g, Vec3 pos, Vec3 vel, float life, float size, ParticleKind kind, Vec3 color);
Vec3  FishingRodTip(const Game *g);
void  AddSplash(Game *g, Vec3 at, float strength);
void  Hint(Game *g, const char *fmt, ...);

// save.c
bool  SaveGame(Game *g, const char *path);
bool  LoadGame(Game *g, const char *path);
bool  SaveExists(const char *path);
unsigned int SavePeekSeed(const char *path);
void  BuildSavePath(Game *g);

// sim.c
void  SimInit(Game *g, uint32_t seed);
void  SimUpdate(Game *g, float dt, int inputFlags, Vec3 moveInput, Vec3 lookInput);
void  SimUpdateIdle(Game *g, float dt);
void  SimNewGame(Game *g, uint32_t seed);
void  SimRespawnPlayer(Game *g);

// input flags
#define IN_JUMP     (1 << 0)
#define IN_SPRINT   (1 << 1)
#define IN_CROUCH   (1 << 2)
#define IN_USE      (1 << 3)   // E — взаимодействие
#define IN_FISH     (1 << 4)   // ЛКМ — заброс/подсечка/подмотка
#define IN_ALT      (1 << 5)   // ПКМ — отмена/тормоз
#define IN_BOARD    (1 << 6)   // F — сесть/выйти из лодки
#define IN_LANTERN  (1 << 7)
#define IN_SONAR    (1 << 8)
#define IN_MOUSEDN  (1 << 9)

#endif // TB_CORE_H
