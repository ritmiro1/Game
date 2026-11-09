// ============================================================================
//  models.c — процедурная библиотека 3D-моделей и материалов
//  Всё строится из скруглённых боксов, лофтов, труб и эллипсоидов:
//  лодка, удочка, деревья, рыбы, маяк, причал, персонажи, птицы, тюлени.
// ============================================================================
#include "models.h"
#include "mb.h"
#include "../platform/pak.h"

static Texture2D load_tex(const char *name) {
    Texture2D t = { 0 };
    unsigned char *data = NULL;
    unsigned int size = 0;
    if (!PakLoad(name, &data, &size) || !data) {
        TraceLog(LOG_WARNING, "MODEL: текстура не найдена: %s", name);
        return t;
    }
    Image img = LoadImageFromMemory(".png", data, (int)size);
    if (img.data) {
        t = LoadTextureFromImage(img);
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_4X);
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
        UnloadImage(img);
    }
    free(data);
    return t;
}

static Shader load_shader(const char *vsName, const char *fsName, bool *ok) {
    Shader sh = { 0 };
    *ok = false;
    unsigned char *vsData = NULL, *fsData = NULL;
    unsigned int vsSize = 0, fsSize = 0;
    bool hasVs = vsName && PakLoad(vsName, &vsData, &vsSize);
    bool hasFs = fsName && PakLoad(fsName, &fsData, &fsSize);
    if (!hasFs) { TraceLog(LOG_ERROR, "MODEL: нет шейдера %s", fsName); return sh; }
    sh = LoadShaderFromMemory(hasVs ? (const char *)vsData : NULL, (const char *)fsData);
    if (sh.id > 0 && IsShaderValid(sh)) *ok = true;
    else TraceLog(LOG_ERROR, "MODEL: шейдер не скомпилировался: %s", fsName);
    free(vsData);
    free(fsData);
    return sh;
}

static int uloc(Shader s, const char *name) {
    if (s.id == 0) return -1;
    return GetShaderLocation(s, name);
}

Material MakeLitMaterial(Assets *a, Texture2D tex) {
    Material m = LoadMaterialDefault();
    if (a->litOk) m.shader = a->shLit;
    m.maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    m.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    return m;
}

Model BuildModel(Mesh *meshes, Material *mats, int count) {
    Model m = { 0 };
    m.meshCount = count;
    m.materialCount = count;
    m.meshes = (Mesh *)malloc(sizeof(Mesh) * count);
    m.materials = (Material *)malloc(sizeof(Material) * count);
    memcpy(m.meshes, meshes, sizeof(Mesh) * count);
    memcpy(m.materials, mats, sizeof(Material) * count);
    m.transform = MatrixIdentity();
    m.meshMaterial = (int *)malloc(sizeof(int) * count);
    for (int i = 0; i < count; i++) m.meshMaterial[i] = i;
    return m;
}

static Color col3(float r, float g, float b, float a) {
    return (Color){ (unsigned char)(clampf(r, 0, 1) * 255.0f), (unsigned char)(clampf(g, 0, 1) * 255.0f),
                    (unsigned char)(clampf(b, 0, 1) * 255.0f), (unsigned char)(clampf(a, 0, 1) * 255.0f) };
}

static float hashf(float x, float z) {
    float h = sinf(x * 12.9898f + z * 78.233f) * 43758.5453f;
    return h - floorf(h);
}

// ------------------------------------------------------------------- прогресс
static AssetsProgressFn s_progress = NULL;
void AssetsSetProgressCallback(AssetsProgressFn fn) { s_progress = fn; }
static void prog(float f, const char *stage) {
    if (s_progress) s_progress(f, stage);
}

// -------------------------------------------------------------------- рельеф
static Color terrain_color(const Game *g, float x, float z, float h, float slope) {
    (void)g;
    float n = hashf(x * 0.7f, z * 0.7f);
    float n2 = hashf(x * 0.13f + 5.0f, z * 0.17f - 3.0f);
    Vec3 c;
    if (h < 0.0f) {
        float d = saturatef(-h / 22.0f);
        c = v3lerp(v3(0.66f, 0.60f, 0.45f), v3(0.30f, 0.36f, 0.31f), d);
        c = v3lerp(c, v3(0.22f, 0.30f, 0.28f), d * d * 0.7f);
    } else if (h < 1.75f) {
        float t = smoothstepf(0.0f, 1.75f, h);
        c = v3lerp(v3(0.80f, 0.74f, 0.57f), v3(0.62f, 0.60f, 0.40f), t);
        c = v3lerp(c, v3(0.34f, 0.45f, 0.22f), smoothstepf(0.55f, 1.05f, t));
    } else {
        float t = smoothstepf(1.75f, 4.0f, h);
        c = v3lerp(v3(0.36f, 0.47f, 0.24f), v3(0.26f, 0.40f, 0.19f), t);
        float rockT = smoothstepf(0.34f, 0.62f, slope);
        c = v3lerp(c, v3(0.44f, 0.43f, 0.40f), rockT);
        c = v3lerp(c, v3(0.52f, 0.51f, 0.49f), smoothstepf(18.0f, 34.0f, h) * 0.75f);
        c = v3lerp(c, v3(0.88f, 0.90f, 0.93f), smoothstepf(35.0f, 41.0f, h) * 0.85f);
    }
    float var = 0.90f + n * 0.18f + (n2 - 0.5f) * 0.10f;
    float ao = 0.82f + 0.18f * (1.0f - slope);
    if (h < 0.0f) ao *= 0.86f;
    c = v3mul(c, var);
    return col3(c.x, c.y, c.z, ao);
}

static void build_terrain_chunks(Assets *a, Game *g) {
    const Terrain *t = &g->terrain;
    const int cells = TERRAIN_RES - 1;
    const int chunkCells = cells / TERRAIN_CHUNKS;
    a->terrainCount = 0;
    for (int cz = 0; cz < TERRAIN_CHUNKS; cz++) {
        for (int cx = 0; cx < TERRAIN_CHUNKS; cx++) {
            int i0 = cx * chunkCells, j0 = cz * chunkCells;
            int verts = chunkCells + 1;
            MB mb;
            mb_init(&mb, verts * verts + 8, chunkCells * chunkCells * 6 + 8, "terrain");
            for (int j = 0; j < verts; j++) {
                for (int i = 0; i < verts; i++) {
                    int gi = i0 + i, gj = j0 + j;
                    int idx = gj * TERRAIN_RES + gi;
                    float x = -WORLD_HALF + (float)gi * TERRAIN_STEP;
                    float z = -WORLD_HALF + (float)gj * TERRAIN_STEP;
                    Vec3 n = v3(t->normalX[idx], t->normalY[idx], t->normalZ[idx]);
                    float slope = 1.0f - n.y;
                    Color c = terrain_color(g, x, z, t->height[idx], slope);
                    mb.tint = c;
                    mb_vert(&mb, (Vector3){ x, t->height[idx], z }, (Vector3){ n.x, n.y, n.z },
                            x * 0.09f, z * 0.09f);
                }
            }
            for (int j = 0; j < chunkCells; j++) {
                for (int i = 0; i < chunkCells; i++) {
                    int v0 = j * verts + i;
                    int v1 = v0 + 1;
                    int v2 = v0 + verts + 1;
                    int v3 = v0 + verts;
                    mb_tri_idx(&mb, v0, v3, v2);
                    mb_tri_idx(&mb, v0, v2, v1);
                }
            }
            Mesh mesh = mb_build(&mb);
            Material mat = a->matTerrain;
            a->terrain[a->terrainCount++] = BuildModel(&mesh, &mat, 1);
            mb_free(&mb);
            prog(0.04f + 0.24f * (float)a->terrainCount / 16.0f, "Рельеф острова...");
        }
    }
}

static void build_water(Assets *a, Game *g) {
    const int seg = 112;
    const float size = 900.0f;
    MB mb;
    mb_init(&mb, (seg + 1) * (seg + 1) + 16, seg * seg * 6 + 16, "water");
    for (int j = 0; j <= seg; j++) {
        for (int i = 0; i <= seg; i++) {
            float x = -size * 0.5f + size * (float)i / (float)seg;
            float z = -size * 0.5f + size * (float)j / (float)seg;
            mb_vert(&mb, (Vector3){ x, SEA_LEVEL, z }, (Vector3){ 0, 1, 0 },
                    x * 0.028f, z * 0.028f);
        }
    }
    for (int j = 0; j < seg; j++) {
        for (int i = 0; i < seg; i++) {
            int v0 = j * (seg + 1) + i;
            int v1 = v0 + 1;
            int v2 = v0 + (seg + 1) + 1;
            int v3 = v0 + (seg + 1);
            mb_tri_idx(&mb, v0, v3, v2);
            mb_tri_idx(&mb, v0, v2, v1);
        }
    }
    Mesh mesh = mb_build(&mb);
    prog(0.30f, "Вода и волны...");
    // Второй набор UV: глубина воды и близость берега для шейдера
    mesh.texcoords2 = (float *)malloc(sizeof(float) * 2 * mesh.vertexCount);
    for (int i = 0; i < mesh.vertexCount; i++) {
        float x = mesh.vertices[i * 3 + 0];
        float z = mesh.vertices[i * 3 + 2];
        float depth = -WorldHeight(g, x, z);
        if (depth < 0.0f) depth = 0.0f;
        float shore = 1.0f - smoothstepf(0.0f, 2.4f, depth);
        mesh.texcoords2[i * 2 + 0] = depth;
        mesh.texcoords2[i * 2 + 1] = shore;
    }
    UploadMesh(&mesh, false);
    Material mat = LoadMaterialDefault();
    if (a->waterOk) {
        mat.shader = a->shWater;
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = a->texWaterN;
        mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    } else {
        mat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){ 40, 90, 110, 220 };
    }
    a->water = BuildModel(&mesh, &mat, 1);
    mb_free(&mb);
}

static void build_sky(Assets *a) {
    MB mb;
    mb_init(&mb, 2600, 4200, "sky");
    mb_ellipsoid(&mb, (Vector3){ 0, 0, 0 }, (Vector3){ 1, 1, 1 }, 32, 20, 2.0f);
    Mesh mesh = mb_build(&mb);
    Material mat = LoadMaterialDefault();
    if (a->skyOk) {
        mat.shader = a->shSky;
        mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    } else {
        mat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){ 120, 160, 200, 255 };
    }
    a->sky = BuildModel(&mesh, &mat, 1);
    mb_free(&mb);
}

// --------------------------------------------------------------------- лодка
static void build_boat(Assets *a) {
    MB hull, wood, metal;
    mb_init(&hull, 4000, 6000, "hull");
    mb_init(&wood, 6000, 9000, "boatwood");
    mb_init(&metal, 2000, 3000, "boatmetal");

    const int NS = 14, RING = 14;
    Vector3 secPos[14], secScale[14];
    for (int i = 0; i < NS; i++) {
        float t = (float)i / (float)(NS - 1);
        float z = -2.25f + t * 4.5f;
        float w = 0.80f * (0.42f + 0.58f * sinf(PI * (0.20f + t * 0.80f)));
        if (t > 0.84f) w *= (1.0f - (t - 0.84f) * 3.9f);
        float d = 0.34f + 0.17f * sinf(PI * t);
        secPos[i] = (Vector3){ 0, -0.30f + 0.09f * t * t, z };
        secScale[i] = (Vector3){ w, d, 1.0f };
    }
    hull.tint = col3(0.30f, 0.44f, 0.39f, 1.0f);
    for (int i = 0; i < NS - 1; i++) {
        for (int s = 0; s < RING; s++) {
            float a0 = (float)s / (float)RING * 2.0f * PI;
            float a1 = (float)(s + 1) / (float)RING * 2.0f * PI;
            Vector3 p00 = { secPos[i].x + cosf(a0) * secScale[i].x, secPos[i].y + sinf(a0) * secScale[i].y, secPos[i].z };
            Vector3 p01 = { secPos[i].x + cosf(a1) * secScale[i].x, secPos[i].y + sinf(a1) * secScale[i].y, secPos[i].z };
            Vector3 p10 = { secPos[i + 1].x + cosf(a0) * secScale[i + 1].x, secPos[i + 1].y + sinf(a0) * secScale[i + 1].y, secPos[i + 1].z };
            Vector3 p11 = { secPos[i + 1].x + cosf(a1) * secScale[i + 1].x, secPos[i + 1].y + sinf(a1) * secScale[i + 1].y, secPos[i + 1].z };
            Vector3 n0 = Vector3Normalize((Vector3){ cosf(a0), sinf(a0) * 1.2f, 0.10f });
            Vector3 n1 = Vector3Normalize((Vector3){ cosf(a1), sinf(a1) * 1.2f, 0.10f });
            int i0 = mb_vert(&hull, p00, n0, (float)s / RING * 2.0f, (float)i * 0.42f);
            int i1 = mb_vert(&hull, p01, n1, (float)(s + 1) / RING * 2.0f, (float)i * 0.42f);
            int i2 = mb_vert(&hull, p11, n1, (float)(s + 1) / RING * 2.0f, (float)(i + 1) * 0.42f);
            int i3 = mb_vert(&hull, p10, n0, (float)s / RING * 2.0f, (float)(i + 1) * 0.42f);
            mb_tri_idx(&hull, i0, i1, i2);
            mb_tri_idx(&hull, i0, i2, i3);
        }
    }

    wood.tint = col3(0.63f, 0.47f, 0.30f, 1.0f);
    for (int i = 0; i < 11; i++) {
        float z = -2.05f + (float)i * 0.40f;
        float w = 0.86f;
        if (z > 1.15f) w = 0.86f * (1.0f - (z - 1.15f) * 0.60f);
        if (w < 0.10f) w = 0.10f;
        mb_box(&wood, (Vector3){ 0, 0.10f, z }, (Vector3){ w, 0.035f, 0.185f }, 1.8f, 0.02f);
    }
    for (int s = -1; s <= 1; s += 2) {
        for (int i = 0; i < 6; i++) {
            float t = (float)i / 5.0f;
            float z = -1.95f + t * 3.9f;
            float w = 0.84f * (1.0f - powf(smoothstepf(0.62f, 1.0f, t), 1.2f) * 0.78f);
            wood.tint = col3(0.55f, 0.40f, 0.25f, 1.0f);
            mb_box(&wood, (Vector3){ s * w, 0.21f, z }, (Vector3){ 0.055f, 0.055f, 0.34f }, 2.0f, 0.02f);
        }
    }
    wood.tint = col3(0.68f, 0.52f, 0.34f, 1.0f);
    mb_box(&wood, (Vector3){ 0, 0.29f, -0.55f }, (Vector3){ 0.72f, 0.045f, 0.16f }, 2.2f, 0.03f);
    mb_box(&wood, (Vector3){ 0, 0.29f, 0.85f }, (Vector3){ 0.50f, 0.045f, 0.16f }, 2.2f, 0.03f);

    metal.tint = col3(0.32f, 0.34f, 0.36f, 1.0f);
    mb_box(&metal, (Vector3){ 0, 0.34f, -2.30f }, (Vector3){ 0.20f, 0.24f, 0.16f }, 2.5f, 0.05f);
    mb_cylinder(&metal, (Vector3){ 0, 0.02f, -2.32f }, 0.045f, 0.045f, 0.52f, 10, 2.0f, true);
    mb_box(&metal, (Vector3){ 0, 0.58f, -2.32f }, (Vector3){ 0.23f, 0.06f, 0.17f }, 2.5f, 0.04f);
    mb_cylinder(&metal, (Vector3){ 0, 0.62f, -2.32f }, 0.12f, 0.10f, 0.14f, 12, 2.0f, true);
    mb_cylinder(&metal, (Vector3){ 0, 0.12f, 1.62f }, 0.05f, 0.04f, 0.55f, 8, 1.0f, true);

    wood.tint = col3(0.70f, 0.55f, 0.36f, 1.0f);
    Vector3 oar1[4] = { { -0.62f, 0.34f, -0.85f }, { -0.95f, 0.30f, -0.35f }, { -1.35f, 0.24f, 0.35f }, { -1.62f, 0.20f, 0.85f } };
    Vector3 oar2[4] = { { 0.62f, 0.34f, -0.85f }, { 0.95f, 0.30f, -0.35f }, { 1.35f, 0.24f, 0.35f }, { 1.62f, 0.20f, 0.85f } };
    mb_tube(&wood, oar1, 4, 0.035f, 6, true);
    mb_tube(&wood, oar2, 4, 0.035f, 6, true);

    Mesh meshes[3];
    Material mats[3] = { a->matMetal, a->matWood, a->matMetal };
    meshes[0] = mb_build(&hull);
    meshes[1] = mb_build(&wood);
    meshes[2] = mb_build(&metal);
    a->boat = BuildModel(meshes, mats, 3);
    mb_free(&hull); mb_free(&wood); mb_free(&metal);
}

// --------------------------------------------------------------------- причал
static void build_dock(Assets *a) {
    MB wood;
    mb_init(&wood, 12000, 18000, "dock");
    wood.tint = col3(0.56f, 0.42f, 0.27f, 1.0f);
    for (int i = 0; i < 34; i++) {
        float z = 0.2f + (float)i * 0.42f;
        mb_box(&wood, (Vector3){ 0, 1.34f, z }, (Vector3){ 1.65f, 0.055f, 0.18f }, 2.4f, 0.025f);
    }
    for (int i = 0; i < 6; i++) {
        float z = 0.6f + (float)i * 2.6f;
        for (int s = -1; s <= 1; s += 2) {
            mb_cylinder(&wood, (Vector3){ s * 1.5f, -3.4f, z }, 0.13f, 0.115f, 4.8f, 8, 1.2f, true);
        }
    }
    for (int s = -1; s <= 1; s += 2) {
        mb_cylinder(&wood, (Vector3){ s * 1.62f, 1.34f, 0.4f }, 0.06f, 0.06f, 0.55f, 6, 1.0f, true);
        mb_cylinder(&wood, (Vector3){ s * 1.62f, 1.34f, 13.4f }, 0.06f, 0.06f, 0.55f, 6, 1.0f, true);
        mb_box(&wood, (Vector3){ s * 1.62f, 1.88f, 6.9f }, (Vector3){ 0.05f, 0.05f, 6.5f }, 1.6f, 0.02f);
        mb_box(&wood, (Vector3){ s * 1.62f, 1.60f, 6.9f }, (Vector3){ 0.04f, 0.04f, 6.5f }, 1.6f, 0.02f);
    }
    // Сходни к берегу
    for (int i = 0; i < 5; i++) {
        mb_box(&wood, (Vector3){ 0, 1.34f - (float)i * 0.16f, -0.6f - (float)i * 0.42f },
               (Vector3){ 0.9f, 0.05f, 0.20f }, 2.0f, 0.02f);
    }
    Mesh mesh = mb_build(&wood);
    Material mat = a->matWood;
    a->dock = BuildModel(&mesh, &mat, 1);
    mb_free(&wood);
}

// ---------------------------------------------------------------------- маяк
static void build_lighthouse(Assets *a) {
    MB stone, paint, glass, metal;
    mb_init(&stone, 2000, 3000, "lh_stone");
    mb_init(&paint, 4000, 6000, "lh_paint");
    mb_init(&glass, 600, 900, "lh_glass");
    mb_init(&metal, 1200, 1800, "lh_metal");

    stone.tint = col3(0.48f, 0.47f, 0.44f, 1.0f);
    mb_cylinder(&stone, (Vector3){ 0, -0.6f, 0 }, 3.5f, 3.0f, 1.6f, 16, 1.4f, true);
    for (int i = 0; i < 8; i++) {
        float ang = (float)i * PI * 0.25f;
        mb_box(&stone, (Vector3){ cosf(ang) * 3.1f, 0.35f, sinf(ang) * 3.1f },
               (Vector3){ 0.55f, 0.42f, 0.55f }, 1.2f, 0.14f);
    }
    const float H = 17.5f;
    const int BANDS = 6;
    for (int b = 0; b < BANDS; b++) {
        float y0 = 1.0f + (float)b * (H / (float)BANDS);
        float r0 = 2.55f - (float)b * 0.22f;
        float r1 = 2.55f - (float)(b + 1) * 0.22f;
        paint.tint = (b % 2 == 0) ? col3(0.86f, 0.85f, 0.82f, 1.0f) : col3(0.66f, 0.20f, 0.18f, 1.0f);
        mb_cylinder(&paint, (Vector3){ 0, y0, 0 }, r0, r1, H / (float)BANDS, 18, 1.0f, false);
    }
    // Галерея
    metal.tint = col3(0.30f, 0.31f, 0.33f, 1.0f);
    mb_cylinder(&metal, (Vector3){ 0, 1.0f + H, 0 }, 1.28f, 1.28f, 0.16f, 18, 1.2f, true);
    for (int i = 0; i < 16; i++) {
        float ang = (float)i / 16.0f * 2.0f * PI;
        mb_cylinder(&metal, (Vector3){ cosf(ang) * 1.18f, 1.16f + H, sinf(ang) * 1.18f },
                    0.03f, 0.03f, 0.95f, 5, 1.0f, false);
    }
    mb_cylinder(&metal, (Vector3){ 0, 2.08f + H, 0 }, 1.20f, 1.20f, 0.06f, 16, 1.0f, false);
    // Фонарная комната
    glass.tint = col3(0.72f, 0.82f, 0.88f, 1.0f);
    mb_cylinder(&glass, (Vector3){ 0, 1.16f + H, 0 }, 0.95f, 0.95f, 1.30f, 14, 1.0f, false);
    for (int i = 0; i < 8; i++) {
        float ang = (float)i / 8.0f * 2.0f * PI;
        metal.tint = col3(0.26f, 0.27f, 0.28f, 1.0f);
        mb_box(&metal, (Vector3){ cosf(ang) * 0.95f, 1.81f + H, sinf(ang) * 0.95f },
               (Vector3){ 0.045f, 0.65f, 0.045f }, 1.0f, 0.01f);
    }
    // Купол
    paint.tint = col3(0.30f, 0.26f, 0.24f, 1.0f);
    mb_cylinder(&paint, (Vector3){ 0, 2.46f + H, 0 }, 1.10f, 0.12f, 1.15f, 16, 1.0f, true);
    mb_cylinder(&paint, (Vector3){ 0, 3.55f + H, 0 }, 0.10f, 0.04f, 0.45f, 6, 1.0f, true);
    // Дверь
    paint.tint = col3(0.34f, 0.22f, 0.15f, 1.0f);
    mb_box(&paint, (Vector3){ 0, 1.55f, 2.42f }, (Vector3){ 0.42f, 0.95f, 0.10f }, 1.4f, 0.03f);

    Mesh meshes[4];
    Material mats[4] = { a->matRock, a->matMetal, a->matMetal, a->matGlow };
    meshes[0] = mb_build(&stone);
    meshes[1] = mb_build(&paint);
    meshes[2] = mb_build(&metal);
    meshes[3] = mb_build(&glass);
    a->lighthouse = BuildModel(meshes, mats, 4);
    mb_free(&stone); mb_free(&paint); mb_free(&glass); mb_free(&metal);
}

// --------------------------------------------------------------- деревья/камни
static void build_tree(Assets *a, int type) {
    MB trunk, leaves;
    mb_init(&trunk, 3000, 4000, "trunk");
    mb_init(&leaves, 6000, 9000, "leaves");
    trunk.tint = col3(0.34f, 0.26f, 0.18f, 1.0f);
    leaves.tint = col3(0.86f, 0.92f, 0.80f, 1.0f);

    if (type == 0) {
        // Ель: ствол + ярусы конусов
        mb_cylinder(&trunk, (Vector3){ 0, 0, 0 }, 0.34f, 0.16f, 7.6f, 9, 1.0f, true);
        for (int i = 0; i < 5; i++) {
            float y = 1.6f + (float)i * 1.42f;
            float r = 2.35f - (float)i * 0.40f;
            float h = 2.2f - (float)i * 0.14f;
            float shade = 0.85f + 0.15f * hashf((float)i, 3.0f);
            leaves.tint = col3(0.20f * shade, 0.36f * shade, 0.19f * shade, 1.0f);
            mb_cylinder(&leaves, (Vector3){ 0, y, 0 }, r, r * 0.18f, h, 12, 1.1f, true);
        }
    } else if (type == 1) {
        // Сосна: изогнутый ствол и шапка из нескольких эллипсоидов
        Vector3 pts[5] = { { 0, 0, 0 }, { 0.18f, 2.2f, 0.05f }, { -0.10f, 4.4f, 0.12f },
                           { 0.14f, 6.3f, -0.05f }, { 0.05f, 7.6f, 0.0f } };
        mb_tube(&trunk, pts, 5, 0.30f, 8, true);
        for (int i = 0; i < 6; i++) {
            float ang = (float)i / 6.0f * 2.0f * PI + 0.4f;
            float rad = 1.35f - (float)i * 0.10f;
            float shade = 0.82f + 0.18f * hashf((float)i * 2.0f, 7.0f);
            leaves.tint = col3(0.22f * shade, 0.38f * shade, 0.20f * shade, 1.0f);
            mb_ellipsoid(&leaves, (Vector3){ cosf(ang) * 1.05f, 6.4f + sinf(ang) * 0.55f, sinf(ang) * 1.05f },
                         (Vector3){ rad, 1.25f, rad }, 10, 7, 1.2f);
        }
        leaves.tint = col3(0.24f, 0.40f, 0.21f, 1.0f);
        mb_ellipsoid(&leaves, (Vector3){ 0.05f, 7.9f, 0.0f }, (Vector3){ 1.15f, 1.05f, 1.15f }, 12, 8, 1.2f);
    } else {
        // Куст
        mb_cylinder(&trunk, (Vector3){ 0, 0, 0 }, 0.14f, 0.09f, 1.0f, 6, 1.0f, true);
        for (int i = 0; i < 4; i++) {
            float ang = (float)i / 4.0f * 2.0f * PI;
            float shade = 0.85f + 0.15f * hashf((float)i, 11.0f);
            leaves.tint = col3(0.26f * shade, 0.42f * shade, 0.22f * shade, 1.0f);
            mb_ellipsoid(&leaves, (Vector3){ cosf(ang) * 0.55f, 0.95f + sinf(ang) * 0.25f, sinf(ang) * 0.55f },
                         (Vector3){ 0.72f, 0.66f, 0.72f }, 9, 6, 1.4f);
        }
    }
    Mesh meshes[2];
    Material mats[2] = { a->matBark, a->matLeaf };
    meshes[0] = mb_build(&trunk);
    meshes[1] = mb_build(&leaves);
    a->tree[type] = BuildModel(meshes, mats, 2);
    mb_free(&trunk); mb_free(&leaves);
}

static void build_rock(Assets *a) {
    MB mb;
    mb_init(&mb, 3000, 4000, "rock");
    mb.tint = col3(0.92f, 0.92f, 0.90f, 0.95f);
    // Неровный валун из деформированных эллипсоидов
    mb_ellipsoid(&mb, (Vector3){ 0, 0.25f, 0 }, (Vector3){ 1.0f, 0.66f, 0.86f }, 11, 8, 1.6f);
    mb_ellipsoid(&mb, (Vector3){ 0.42f, 0.05f, 0.30f }, (Vector3){ 0.55f, 0.42f, 0.50f }, 9, 6, 1.6f);
    mb_ellipsoid(&mb, (Vector3){ -0.48f, 0.02f, -0.24f }, (Vector3){ 0.48f, 0.36f, 0.44f }, 9, 6, 1.6f);
    Mesh mesh = mb_build(&mb);
    Material mat = a->matRock;
    a->rock = BuildModel(&mesh, &mat, 1);
    mb_free(&mb);
}

static void build_reed(Assets *a) {
    MB mb;
    mb_init(&mb, 3000, 4000, "reed");
    mb.tint = col3(0.80f, 0.85f, 0.72f, 1.0f);
    for (int i = 0; i < 11; i++) {
        float ang = (float)i * 2.4f;
        float rad = 0.05f + hashf((float)i, 1.0f) * 0.35f;
        float h = 0.9f + hashf((float)i, 2.0f) * 0.9f;
        Vector3 base = { cosf(ang) * rad, 0.0f, sinf(ang) * rad };
        Vector3 tip = { base.x + cosf(ang) * 0.15f, h, base.z + sinf(ang) * 0.15f };
        Vector3 pts[3] = { base, (Vector3){ (base.x + tip.x) * 0.5f, h * 0.55f, (base.z + tip.z) * 0.5f }, tip };
        mb_tube(&mb, pts, 3, 0.028f, 4, true);
        mb_ellipsoid(&mb, (Vector3){ tip.x, h + 0.05f, tip.z }, (Vector3){ 0.035f, 0.11f, 0.035f }, 6, 4, 1.0f);
    }
    Mesh mesh = mb_build(&mb);
    Material mat = a->matLeaf;
    a->reed = BuildModel(&mesh, &mat, 1);
    mb_free(&mb);
}

static void build_grass(Assets *a) {
    MB mb;
    mb_init(&mb, 1200, 1600, "grass");
    mb.tint = col3(0.82f, 0.90f, 0.70f, 1.0f);
    for (int i = 0; i < 14; i++) {
        float ang = (float)i * 1.7f;
        float rad = 0.02f + hashf((float)i, 5.0f) * 0.28f;
        float h = 0.22f + hashf((float)i, 6.0f) * 0.30f;
        Vector3 base = { cosf(ang) * rad, 0, sinf(ang) * rad };
        Vector3 tip = { base.x + cosf(ang) * 0.10f, h, base.z + sinf(ang) * 0.10f };
        Vector3 pts[2] = { base, tip };
        mb_tube(&mb, pts, 2, 0.022f, 3, true);
    }
    Mesh mesh = mb_build(&mb);
    Material mat = a->matLeaf;
    a->grass = BuildModel(&mesh, &mat, 1);
    mb_free(&mb);
}

// ------------------------------------------------------------------- предметы
static void build_props(Assets *a) {
    // Ящик
    {
        MB wood, metal;
        mb_init(&wood, 900, 1200, "crate");
        mb_init(&metal, 400, 600, "crateband");
        wood.tint = col3(0.60f, 0.45f, 0.28f, 1.0f);
        mb_box(&wood, (Vector3){ 0, 0.30f, 0 }, (Vector3){ 0.42f, 0.30f, 0.34f }, 2.0f, 0.04f);
        metal.tint = col3(0.42f, 0.40f, 0.36f, 1.0f);
        mb_box(&metal, (Vector3){ 0, 0.30f, 0 }, (Vector3){ 0.44f, 0.045f, 0.36f }, 2.0f, 0.02f);
        Mesh meshes[2];
        Material mats[2] = { a->matWood, a->matMetal };
        meshes[0] = mb_build(&wood);
        meshes[1] = mb_build(&metal);
        a->crate = BuildModel(meshes, mats, 2);
        mb_free(&wood); mb_free(&metal);
    }
    // Бочка
    {
        MB wood, metal;
        mb_init(&wood, 900, 1200, "barrel");
        mb_init(&metal, 400, 600, "barrelband");
        wood.tint = col3(0.52f, 0.38f, 0.24f, 1.0f);
        mb_cylinder(&wood, (Vector3){ 0, 0, 0 }, 0.34f, 0.30f, 0.30f, 14, 1.0f, true);
        mb_cylinder(&wood, (Vector3){ 0, 0.30f, 0 }, 0.38f, 0.38f, 0.34f, 14, 1.0f, true);
        mb_cylinder(&wood, (Vector3){ 0, 0.64f, 0 }, 0.30f, 0.34f, 0.28f, 14, 1.0f, true);
        metal.tint = col3(0.40f, 0.38f, 0.35f, 1.0f);
        mb_cylinder(&metal, (Vector3){ 0, 0.14f, 0 }, 0.36f, 0.36f, 0.05f, 14, 1.0f, false);
        mb_cylinder(&metal, (Vector3){ 0, 0.60f, 0 }, 0.36f, 0.36f, 0.05f, 14, 1.0f, false);
        Mesh meshes[2];
        Material mats[2] = { a->matWood, a->matMetal };
        meshes[0] = mb_build(&wood);
        meshes[1] = mb_build(&metal);
        a->barrel = BuildModel(meshes, mats, 2);
        mb_free(&wood); mb_free(&metal);
    }
    // Костровище
    {
        MB rock, wood;
        mb_init(&rock, 1600, 2200, "firering");
        mb_init(&wood, 1600, 2200, "firewood");
        rock.tint = col3(0.44f, 0.43f, 0.41f, 1.0f);
        for (int i = 0; i < 9; i++) {
            float ang = (float)i / 9.0f * 2.0f * PI;
            mb_ellipsoid(&rock, (Vector3){ cosf(ang) * 0.72f, 0.10f, sinf(ang) * 0.72f },
                         (Vector3){ 0.20f, 0.14f, 0.18f }, 8, 5, 1.0f);
        }
        wood.tint = col3(0.30f, 0.22f, 0.15f, 1.0f);
        for (int i = 0; i < 4; i++) {
            float ang = (float)i / 4.0f * PI;
            Vector3 a0 = { cosf(ang) * 0.42f, 0.06f, sinf(ang) * 0.42f };
            Vector3 a1 = { -cosf(ang) * 0.42f, 0.55f, -sinf(ang) * 0.42f };
            Vector3 pts[2] = { a0, a1 };
            mb_tube(&wood, pts, 2, 0.075f, 6, true);
        }
        Mesh meshes[2];
        Material mats[2] = { a->matRock, a->matBark };
        meshes[0] = mb_build(&rock);
        meshes[1] = mb_build(&wood);
        a->campfire = BuildModel(meshes, mats, 2);
        mb_free(&rock); mb_free(&wood);
    }
    // Указатель
    {
        MB wood;
        mb_init(&wood, 700, 900, "sign");
        wood.tint = col3(0.58f, 0.43f, 0.27f, 1.0f);
        mb_cylinder(&wood, (Vector3){ 0, 0, 0 }, 0.075f, 0.065f, 1.55f, 7, 1.0f, true);
        mb_box(&wood, (Vector3){ 0.22f, 1.35f, 0 }, (Vector3){ 0.42f, 0.16f, 0.035f }, 2.0f, 0.02f);
        Mesh mesh = mb_build(&wood);
        Material mat = a->matWood;
        a->signPost = BuildModel(&mesh, &mat, 1);
        mb_free(&wood);
    }
    // Палатка
    {
        MB cloth, metal;
        mb_init(&cloth, 900, 1200, "tent");
        mb_init(&metal, 300, 400, "tentpole");
        cloth.tint = col3(0.72f, 0.52f, 0.28f, 1.0f);
        // Двускатная палатка
        Vector3 A = { -1.0f, 0.05f, -1.1f }, B = { 1.0f, 0.05f, -1.1f };
        Vector3 C = { 1.0f, 0.05f, 1.1f }, D = { -1.0f, 0.05f, 1.1f };
        Vector3 R1 = { -1.0f, 1.15f, 0.0f }, R2 = { 1.0f, 1.15f, 0.0f };
        Vector3 R3 = { -0.0f, 1.25f, -1.1f }, R4 = { 0.0f, 1.25f, 1.1f };
        mb_quad_uv(&cloth, A, D, R4, R3, 0, 0, 2, 0, 2, 1.5f, 0, 1.5f);
        mb_quad_uv(&cloth, B, C, R4, R3, 0, 0, 2, 0, 2, 1.5f, 0, 1.5f);
        mb_quad_uv(&cloth, R1, R2, R4, R3, 0, 0, 1, 0, 1, 1, 0, 1);
        metal.tint = col3(0.35f, 0.34f, 0.32f, 1.0f);
        mb_cylinder(&metal, (Vector3){ -1.0f, 0.0f, -1.1f }, 0.02f, 0.02f, 1.2f, 5, 1.0f, false);
        mb_cylinder(&metal, (Vector3){ 1.0f, 0.0f, -1.1f }, 0.02f, 0.02f, 1.2f, 5, 1.0f, false);
        Mesh meshes[2];
        Material mats[2] = { a->matCloth, a->matMetal };
        meshes[0] = mb_build(&cloth);
        meshes[1] = mb_build(&metal);
        a->tent = BuildModel(meshes, mats, 2);
        mb_free(&cloth); mb_free(&metal);
    }
    // Фонарь
    {
        MB metal, glass;
        mb_init(&metal, 500, 700, "lantern");
        mb_init(&glass, 200, 300, "lanternglass");
        metal.tint = col3(0.30f, 0.29f, 0.27f, 1.0f);
        mb_cylinder(&metal, (Vector3){ 0, 0, 0 }, 0.11f, 0.11f, 0.05f, 10, 1.0f, true);
        mb_cylinder(&metal, (Vector3){ 0, 0.26f, 0 }, 0.10f, 0.05f, 0.06f, 10, 1.0f, true);
        for (int i = 0; i < 4; i++) {
            float ang = (float)i / 4.0f * 2.0f * PI;
            mb_box(&metal, (Vector3){ cosf(ang) * 0.10f, 0.15f, sinf(ang) * 0.10f },
                   (Vector3){ 0.012f, 0.11f, 0.012f }, 1.0f, 0.004f);
        }
        glass.tint = col3(1.0f, 0.92f, 0.66f, 1.0f);
        mb_cylinder(&glass, (Vector3){ 0, 0.05f, 0 }, 0.085f, 0.085f, 0.21f, 10, 1.0f, false);
        Mesh meshes[2];
        Material mats[2] = { a->matMetal, a->matGlow };
        meshes[0] = mb_build(&metal);
        meshes[1] = mb_build(&glass);
        a->lantern = BuildModel(meshes, mats, 2);
        mb_free(&metal); mb_free(&glass);
    }
    // Поплавок
    {
        MB mb;
        mb_init(&mb, 600, 800, "bobber");
        mb.tint = col3(0.92f, 0.24f, 0.18f, 1.0f);
        mb_ellipsoid(&mb, (Vector3){ 0, 0, 0 }, (Vector3){ 0.045f, 0.055f, 0.045f }, 10, 7, 1.0f);
        mb.tint = col3(0.96f, 0.96f, 0.94f, 1.0f);
        mb_ellipsoid(&mb, (Vector3){ 0, 0.045f, 0 }, (Vector3){ 0.032f, 0.030f, 0.032f }, 8, 6, 1.0f);
        mb.tint = col3(0.90f, 0.90f, 0.88f, 1.0f);
        mb_cylinder(&mb, (Vector3){ 0, 0.06f, 0 }, 0.008f, 0.006f, 0.12f, 5, 1.0f, false);
        Mesh mesh = mb_build(&mb);
        Material mat = MakeLitMaterial(a, a->texMetal);
        a->bobber = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
    // Колена удилища (телескоп): 4 сегмента, рисуются с изгибом
    for (int i = 0; i < 4; i++) {
        MB mb;
        mb_init(&mb, 400, 500, "rodseg");
        float r0 = 0.016f - (float)i * 0.0032f;
        float len = 0.62f - (float)i * 0.06f;
        mb.tint = (i == 0) ? col3(0.28f, 0.20f, 0.14f, 1.0f) : col3(0.18f, 0.20f, 0.20f, 1.0f);
        mb_cylinder(&mb, (Vector3){ 0, 0, 0 }, r0, r0 * 0.86f, len, 7, 1.0f, true);
        if (i == 0) {
            mb.tint = col3(0.32f, 0.34f, 0.33f, 1.0f);
            mb_cylinder(&mb, (Vector3){ 0.055f, 0.10f, 0 }, 0.042f, 0.042f, 0.05f, 12, 1.0f, true);
            mb_cylinder(&mb, (Vector3){ 0.055f, 0.10f, 0 }, 0.030f, 0.030f, 0.07f, 10, 1.0f, true);
        }
        Mesh mesh = mb_build(&mb);
        Material mat = (i == 0) ? MakeLitMaterial(a, a->texBark) : MakeLitMaterial(a, a->texMetal);
        a->rodSegment[i] = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
}

// ----------------------------------------------------------------------- рыбы
static void build_fish(Assets *a, int id) {
    const FishSpecies *sp = FishGet(id);
    MB body, fin;
    mb_init(&body, 4000, 5000, "fishbody");
    mb_init(&fin, 1500, 2000, "fishfin");
    float L = sp->bodyLen;
    float H = sp->bodyLen * sp->bodyHeight;
    float W = H * 0.42f;
    const int NS = 12, RING = 10;

    Color ca = col3(sp->colorA.x, sp->colorA.y, sp->colorA.z, 1.0f);
    for (int i = 0; i < NS - 1; i++) {
        float t0 = (float)i / (float)(NS - 1);
        float t1 = (float)(i + 1) / (float)(NS - 1);
        float z0 = -L * 0.5f + t0 * L;
        float z1 = -L * 0.5f + t1 * L;
        // Профиль тела: раздувается к центру, сужается к хвосту
        float p0 = sinf(PI * powf(t0, 0.85f));
        float p1 = sinf(PI * powf(t1, 0.85f));
        float h0 = H * (0.22f + 0.78f * p0);
        float h1 = H * (0.22f + 0.78f * p1);
        float w0 = W * (0.25f + 0.75f * p0);
        float w1 = W * (0.25f + 0.75f * p1);
        for (int s = 0; s < RING; s++) {
            float a0 = (float)s / (float)RING * 2.0f * PI;
            float a1 = (float)(s + 1) / (float)RING * 2.0f * PI;
            Vector3 q00 = { cosf(a0) * w0, sinf(a0) * h0, z0 };
            Vector3 q01 = { cosf(a1) * w0, sinf(a1) * h0, z0 };
            Vector3 q10 = { cosf(a0) * w1, sinf(a0) * h1, z1 };
            Vector3 q11 = { cosf(a1) * w1, sinf(a1) * h1, z1 };
            // Плавный переход цвета спина/брюхо
            float backT = saturatef(sinf(a0) * 0.5f + 0.5f);
            body.tint = col3(lerpf(sp->colorB.x, sp->colorA.x, backT),
                             lerpf(sp->colorB.y, sp->colorA.y, backT),
                             lerpf(sp->colorB.z, sp->colorA.z, backT), 1.0f);
            Vector3 n0 = Vector3Normalize((Vector3){ cosf(a0), sinf(a0) * 1.4f, 0.05f });
            Vector3 n1 = Vector3Normalize((Vector3){ cosf(a1), sinf(a1) * 1.4f, 0.05f });
            int i0 = mb_vert(&body, q00, n0, (float)s / RING * 2.0f, t0 * 2.0f);
            int i1 = mb_vert(&body, q01, n1, (float)(s + 1) / RING * 2.0f, t0 * 2.0f);
            int i2 = mb_vert(&body, q11, n1, (float)(s + 1) / RING * 2.0f, t1 * 2.0f);
            int i3 = mb_vert(&body, q10, n0, (float)s / RING * 2.0f, t1 * 2.0f);
            mb_tri_idx(&body, i0, i1, i2);
            mb_tri_idx(&body, i0, i2, i3);
        }
    }
    body.tint = ca;
    // Хвостовой плавник
    fin.tint = col3(sp->colorA.x * 0.9f, sp->colorA.y * 0.9f, sp->colorA.z * 0.9f, 1.0f);
    float tailH = H * (sp->finStyle == 2 ? 1.25f : 0.85f);
    Vector3 tA = { 0, 0, -L * 0.5f };
    Vector3 tB = { 0, tailH * 0.95f, -L * 0.5f - L * (sp->finStyle == 1 ? 0.34f : 0.22f) };
    Vector3 tC = { 0, 0, -L * 0.5f - L * 0.30f };
    Vector3 tD = { 0, -tailH * 0.95f, -L * 0.5f - L * (sp->finStyle == 1 ? 0.34f : 0.22f) };
    mb_quad(&fin, tA, tB, tC, tC, 1.0f);
    mb_quad(&fin, tC, tC, tD, tA, 1.0f);
    // Спинной плавник
    Vector3 dA = { 0, H * 0.92f, L * 0.06f };
    Vector3 dB = { 0, H * 1.55f, -L * 0.10f };
    Vector3 dC = { 0, H * 0.92f, -L * 0.20f };
    mb_quad(&fin, dA, dB, dC, dC, 1.0f);
    // Грудные плавники
    for (int s = -1; s <= 1; s += 2) {
        Vector3 pA = { s * W * 0.75f, -H * 0.10f, L * 0.16f };
        Vector3 pB = { s * W * 2.0f, -H * 0.55f, L * 0.02f };
        Vector3 pC = { s * W * 1.1f, -H * 0.35f, -L * 0.05f };
        mb_quad(&fin, pA, pB, pC, pC, 1.0f);
    }
    Mesh meshes[2];
    Material mats[2] = { MakeLitMaterial(a, a->texScales), MakeLitMaterial(a, a->texScales) };
    meshes[0] = mb_build(&body);
    meshes[1] = mb_build(&fin);
    a->fish[id] = BuildModel(meshes, mats, 2);
    mb_free(&body); mb_free(&fin);
}

// ------------------------------------------------------------------ персонажи
static void build_human(Assets *a) {
    // 0: левая нога, 1: правая нога, 2: торс, 3: голова, 4: левая рука, 5: правая рука
    for (int s = 0; s < 2; s++) {
        MB mb;
        mb_init(&mb, 1500, 2000, "leg");
        mb.tint = col3(0.30f, 0.32f, 0.38f, 1.0f);
        mb_cylinder(&mb, (Vector3){ 0, 0.06f, 0 }, 0.085f, 0.078f, 0.74f, 8, 1.4f, true);
        mb_box(&mb, (Vector3){ 0, 0.02f, 0.05f }, (Vector3){ 0.09f, 0.05f, 0.14f }, 2.0f, 0.03f);
        Mesh mesh = mb_build(&mb);
        Material mat = MakeLitMaterial(a, a->texCloth);
        a->humanParts[s] = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
    {
        MB mb;
        mb_init(&mb, 2500, 3200, "torso");
        mb.tint = col3(0.34f, 0.38f, 0.44f, 1.0f);
        mb_box(&mb, (Vector3){ 0, 0.20f, 0 }, (Vector3){ 0.23f, 0.34f, 0.15f }, 2.2f, 0.07f);
        mb.tint = col3(0.28f, 0.32f, 0.38f, 1.0f);
        mb_cylinder(&mb, (Vector3){ 0, 0.50f, 0 }, 0.10f, 0.09f, 0.12f, 8, 1.0f, true);
        Mesh mesh = mb_build(&mb);
        Material mat = MakeLitMaterial(a, a->texCloth);
        a->humanParts[2] = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
    {
        MB mb;
        mb_init(&mb, 1200, 1600, "head");
        mb.tint = col3(0.86f, 0.70f, 0.58f, 1.0f);
        mb_ellipsoid(&mb, (Vector3){ 0, 0, 0 }, (Vector3){ 0.115f, 0.135f, 0.125f }, 12, 9, 1.4f);
        mb.tint = col3(0.72f, 0.56f, 0.44f, 1.0f);
        mb_box(&mb, (Vector3){ 0, -0.02f, 0.10f }, (Vector3){ 0.035f, 0.03f, 0.035f }, 2.0f, 0.02f);
        Mesh mesh = mb_build(&mb);
        Material mat = MakeLitMaterial(a, a->texCloth);
        a->humanParts[3] = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
    for (int s = 0; s < 2; s++) {
        MB mb;
        mb_init(&mb, 1200, 1600, "arm");
        mb.tint = col3(0.32f, 0.36f, 0.42f, 1.0f);
        mb_cylinder(&mb, (Vector3){ 0, -0.50f, 0 }, 0.062f, 0.052f, 0.50f, 7, 1.4f, true);
        mb.tint = col3(0.84f, 0.68f, 0.56f, 1.0f);
        mb_ellipsoid(&mb, (Vector3){ 0, -0.54f, 0 }, (Vector3){ 0.055f, 0.06f, 0.05f }, 8, 6, 1.0f);
        Mesh mesh = mb_build(&mb);
        Material mat = MakeLitMaterial(a, a->texCloth);
        a->humanParts[4 + s] = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
    {
        MB mb;
        mb_init(&mb, 900, 1200, "hat0");
        mb.tint = col3(0.30f, 0.28f, 0.24f, 1.0f);
        mb_cylinder(&mb, (Vector3){ 0, 0.04f, 0 }, 0.10f, 0.13f, 0.16f, 12, 1.0f, true);
        mb_cylinder(&mb, (Vector3){ 0, 0.0f, 0 }, 0.22f, 0.22f, 0.03f, 14, 1.0f, true);
        Mesh mesh = mb_build(&mb);
        Material mat = MakeLitMaterial(a, a->texCloth);
        a->humanHat[0] = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
    {
        MB mb;
        mb_init(&mb, 900, 1200, "hat1");
        mb.tint = col3(0.16f, 0.22f, 0.34f, 1.0f);
        mb_cylinder(&mb, (Vector3){ 0, 0.06f, 0 }, 0.13f, 0.12f, 0.10f, 12, 1.0f, true);
        mb_box(&mb, (Vector3){ 0, 0.06f, 0.11f }, (Vector3){ 0.13f, 0.012f, 0.08f }, 1.0f, 0.01f);
        Mesh mesh = mb_build(&mb);
        Material mat = MakeLitMaterial(a, a->texCloth);
        a->humanHat[1] = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
}

static void build_animals(Assets *a) {
    // Пёс
    {
        MB fur, dark;
        mb_init(&fur, 3000, 4000, "dog_fur");
        mb_init(&dark, 1200, 1600, "dog_dark");
        fur.tint = col3(0.52f, 0.40f, 0.26f, 1.0f);
        mb_ellipsoid(&fur, (Vector3){ 0, 0.42f, 0 }, (Vector3){ 0.20f, 0.21f, 0.44f }, 12, 8, 1.6f);
        mb_ellipsoid(&fur, (Vector3){ 0, 0.52f, 0.46f }, (Vector3){ 0.13f, 0.13f, 0.15f }, 10, 7, 1.6f);
        mb_box(&fur, (Vector3){ 0, 0.48f, 0.60f }, (Vector3){ 0.06f, 0.05f, 0.09f }, 2.0f, 0.02f);
        dark.tint = col3(0.34f, 0.26f, 0.17f, 1.0f);
        for (int s = -1; s <= 1; s += 2) {
            for (int f = -1; f <= 1; f += 2) {
                mb_cylinder(&dark, (Vector3){ s * 0.13f, 0.02f, f * 0.30f }, 0.045f, 0.040f, 0.30f, 6, 1.2f, true);
            }
        }
        Vector3 tail[3] = { { 0, 0.52f, -0.44f }, { 0, 0.66f, -0.56f }, { 0, 0.78f, -0.60f } };
        mb_tube(&dark, tail, 3, 0.035f, 5, true);
        for (int s = -1; s <= 1; s += 2) {
            mb_box(&dark, (Vector3){ s * 0.08f, 0.64f, 0.44f }, (Vector3){ 0.04f, 0.07f, 0.02f }, 1.0f, 0.01f);
        }
        Mesh meshes[2];
        Material mats[2] = { MakeLitMaterial(a, a->texBark), MakeLitMaterial(a, a->texBark) };
        meshes[0] = mb_build(&fur);
        meshes[1] = mb_build(&dark);
        a->dog = BuildModel(meshes, mats, 2);
        mb_free(&fur); mb_free(&dark);
    }
    // Тюлень
    {
        MB mb;
        mb_init(&mb, 4000, 5200, "seal");
        mb.tint = col3(0.40f, 0.44f, 0.46f, 1.0f);
        mb_ellipsoid(&mb, (Vector3){ 0, 0.34f, 0 }, (Vector3){ 0.42f, 0.36f, 0.95f }, 14, 10, 1.8f);
        mb_ellipsoid(&mb, (Vector3){ 0, 0.50f, 0.85f }, (Vector3){ 0.24f, 0.24f, 0.28f }, 11, 8, 1.8f);
        mb.tint = col3(0.30f, 0.34f, 0.36f, 1.0f);
        for (int s = -1; s <= 1; s += 2) {
            mb_box(&mb, (Vector3){ s * 0.38f, 0.20f, 0.35f }, (Vector3){ 0.16f, 0.05f, 0.26f }, 1.4f, 0.03f);
            mb_box(&mb, (Vector3){ s * 0.18f, 0.18f, -0.92f }, (Vector3){ 0.12f, 0.045f, 0.20f }, 1.4f, 0.03f);
        }
        Mesh mesh = mb_build(&mb);
        Material mat = MakeLitMaterial(a, a->texScales);
        a->seal = BuildModel(&mesh, &mat, 1);
        mb_free(&mb);
    }
    // Чайка
    {
        MB body, wing;
        mb_init(&body, 900, 1200, "gull");
        mb_init(&wing, 900, 1200, "gullwing");
        body.tint = col3(0.94f, 0.94f, 0.92f, 1.0f);
        mb_ellipsoid(&body, (Vector3){ 0, 0, 0 }, (Vector3){ 0.10f, 0.10f, 0.28f }, 10, 7, 1.6f);
        mb_ellipsoid(&body, (Vector3){ 0, 0.08f, 0.26f }, (Vector3){ 0.075f, 0.075f, 0.09f }, 9, 6, 1.6f);
        body.tint = col3(0.94f, 0.72f, 0.24f, 1.0f);
        mb_box(&body, (Vector3){ 0, 0.07f, 0.36f }, (Vector3){ 0.022f, 0.02f, 0.06f }, 1.0f, 0.005f);
        wing.tint = col3(0.86f, 0.88f, 0.90f, 1.0f);
        for (int s = -1; s <= 1; s += 2) {
            Vector3 a0 = { 0, 0.02f, 0.06f };
            Vector3 a1 = { s * 0.34f, 0.06f, 0.02f };
            Vector3 a2 = { s * 0.52f, 0.02f, -0.10f };
            Vector3 a3 = { s * 0.10f, 0.01f, -0.10f };
            mb_quad(&wing, a0, a1, a2, a3, 1.0f);
        }
        Mesh meshes[2];
        Material mats[2] = { MakeLitMaterial(a, a->texCloth), MakeLitMaterial(a, a->texCloth) };
        meshes[0] = mb_build(&body);
        meshes[1] = mb_build(&wing);
        a->gull = BuildModel(meshes, mats, 2);
        mb_free(&body); mb_free(&wing);
    }
}

// -------------------------------------------------------------- инициализация
bool AssetsInit(Assets *a) {
    memset(a, 0, sizeof(*a));
    a->texSand = load_tex("tex_sand.png");
    a->texRock = load_tex("tex_rock.png");
    a->texGrass = load_tex("tex_grass.png");
    a->texBark = load_tex("tex_bark.png");
    a->texWood = load_tex("tex_wood.png");
    a->texMetal = load_tex("tex_metal.png");
    a->texWaterN = load_tex("tex_water_n.png");
    a->texCaustics = load_tex("tex_caustics.png");
    a->texFoam = load_tex("tex_foam.png");
    a->texRain = load_tex("tex_rain.png");
    a->texGlow = load_tex("tex_glow.png");
    a->texScales = load_tex("tex_scales.png");
    a->texLeaf = load_tex("tex_leaf.png");
    a->texCloth = load_tex("tex_cloth.png");
    a->texDetail = load_tex("tex_detail.png");

    a->shLit = load_shader("lit.vs", "lit.fs", &a->litOk);
    a->shWater = load_shader("water.vs", "water.fs", &a->waterOk);
    a->shSky = load_shader("sky.vs", "sky.fs", &a->skyOk);
    a->shBlur = load_shader(NULL, "blur.fs", &a->blurOk);
    a->shComposite = load_shader(NULL, "composite.fs", &a->compositeOk);

    a->matTerrain = MakeLitMaterial(a, a->texDetail);
    a->matBark = MakeLitMaterial(a, a->texBark);
    a->matWood = MakeLitMaterial(a, a->texWood);
    a->matMetal = MakeLitMaterial(a, a->texMetal);
    a->matRock = MakeLitMaterial(a, a->texRock);
    a->matLeaf = MakeLitMaterial(a, a->texLeaf);
    a->matCloth = MakeLitMaterial(a, a->texCloth);
    a->matScales = MakeLitMaterial(a, a->texScales);
    a->matGlow = MakeLitMaterial(a, a->texGlow);
    a->matSand = MakeLitMaterial(a, a->texSand);

    prog(0.55f, "Материалы и освещение...");
    Shader s = a->shLit;
    a->uMode = uloc(s, "uMode");
    a->uEmissive = uloc(s, "uEmissive");
    a->uGloss = uloc(s, "uGloss");
    a->uTint = uloc(s, "uTint");
    a->uTime = uloc(s, "uTime");
    a->uSunDir = uloc(s, "sunDir");
    a->uSunColor = uloc(s, "sunColor");
    a->uAmbient = uloc(s, "ambientColor");
    a->uCamPos = uloc(s, "cameraPos");
    a->uFogColor = uloc(s, "fogColor");
    a->uFogDensity = uloc(s, "fogDensity");
    a->uWaterLevel = uloc(s, "waterLevel");

    Shader w = a->shWater;
    a->wTime = uloc(w, "uTime");
    a->wWind = uloc(w, "uWind");
    a->wSunDir = uloc(w, "sunDir");
    a->wSunColor = uloc(w, "sunColor");
    a->wSkyColor = uloc(w, "skyColor");
    a->wCamPos = uloc(w, "cameraPos");
    a->wFogColor = uloc(w, "fogColor");
    a->wFogDensity = uloc(w, "fogDensity");
    a->wShallow = uloc(w, "shallowColor");
    a->wDeep = uloc(w, "deepColor");
    a->wFoamC = uloc(w, "foamColor");
    a->wFoamAmt = uloc(w, "uFoam");

    Shader sk = a->shSky;
    a->sSunDir = uloc(sk, "sunDir");
    a->sSunColor = uloc(sk, "sunColor");
    a->sSkyTop = uloc(sk, "skyTop");
    a->sSkyHorizon = uloc(sk, "skyHorizon");
    a->sMoonDir = uloc(sk, "moonDir");
    a->sCloud = uloc(sk, "cloudAmount");
    a->sNight = uloc(sk, "nightAmount");
    a->sTime = uloc(sk, "uTime");

    a->bThreshold = uloc(a->shBlur, "uThreshold");
    a->bDir = uloc(a->shBlur, "uDir");

    a->cBloom = uloc(a->shComposite, "uBloom");
    a->cBlur = uloc(a->shComposite, "textureBlur");
    a->cRes = uloc(a->shComposite, "uResolution");
    a->cDof = uloc(a->shComposite, "uDof");
    a->cVig = uloc(a->shComposite, "uVignette");
    a->cSat = uloc(a->shComposite, "uSaturation");
    a->cCon = uloc(a->shComposite, "uContrast");
    a->cTime = uloc(a->shComposite, "uTime");
    a->cUnder = uloc(a->shComposite, "uUnderwater");
    a->cFlash = uloc(a->shComposite, "uFlash");
    a->cTint = uloc(a->shComposite, "uTint");

    TraceLog(LOG_INFO, "ASSETS: lit=%d water=%d sky=%d blur=%d composite=%d",
             a->litOk, a->waterOk, a->skyOk, a->blurOk, a->compositeOk);
    return a->litOk || true;
}

void AssetsBuildWorld(Assets *a, Game *g) {
    a->builtSeed = g->worldSeed;
    build_terrain_chunks(a, g);
    build_water(a, g);
    build_sky(a);
    build_boat(a);
    build_dock(a);
    build_lighthouse(a);
    AssetsBuildPropGroups(a, g);
    build_props(a);
    build_human(a);
    build_animals(a);
    prog(0.86f, "Модели рыб...");
    a->fishModelCount = FishSpeciesCount();
    for (int i = 0; i < a->fishModelCount; i++) {
        build_fish(a, i);
        prog(0.86f + 0.10f * (float)(i + 1) / (float)a->fishModelCount, "Модели рыб...");
    }
    prog(0.97f, "Жители и животные...");
    TraceLog(LOG_INFO, "ASSETS: мир построен, рыб-моделей %d", a->fishModelCount);
}

void AssetsFree(Assets *a) {
    for (int i = 0; i < a->terrainCount; i++) UnloadModel(a->terrain[i]);
    UnloadModel(a->water);
    UnloadModel(a->sky);
    UnloadModel(a->boat);
    UnloadModel(a->dock);
    UnloadModel(a->lighthouse);
    UnloadModel(a->campfire);
    for (int i = 0; i < 3; i++) UnloadModel(a->tree[i]);
    UnloadModel(a->rock);
    UnloadModel(a->reed);
    UnloadModel(a->grass);
    for (int i = 0; i < a->fishModelCount; i++) UnloadModel(a->fish[i]);
    for (int i = 0; i < 6; i++) UnloadModel(a->humanParts[i]);
    for (int i = 0; i < 2; i++) UnloadModel(a->humanHat[i]);
    UnloadModel(a->dog);
    UnloadModel(a->seal);
    UnloadModel(a->gull);
    UnloadModel(a->bobber);
    UnloadModel(a->lantern);
    UnloadModel(a->crate);
    UnloadModel(a->barrel);
    UnloadModel(a->signPost);
    UnloadModel(a->tent);
    for (int i = 0; i < 4; i++) UnloadModel(a->rodSegment[i]);
    Texture2D texs[] = { a->texSand, a->texRock, a->texGrass, a->texBark, a->texWood, a->texMetal,
                         a->texWaterN, a->texCaustics, a->texFoam, a->texRain, a->texGlow,
                         a->texScales, a->texLeaf, a->texCloth, a->texDetail };
    for (int i = 0; i < 15; i++) if (texs[i].id > 0) UnloadTexture(texs[i]);
}

// ------------------------------------------------------------------- освещение
void SetLitParams(Assets *a, Game *g) {
    if (!a->litOk) return;
    Shader s = a->shLit;
    Environment *e = &g->env;
    float sunDir[3] = { e->sunDir.x, e->sunDir.y, e->sunDir.z };
    float sunCol[3] = { e->sunColor.x * e->sunIntensity, e->sunColor.y * e->sunIntensity, e->sunColor.z * e->sunIntensity };
    float amb[3] = { e->ambient.x, e->ambient.y, e->ambient.z };
    float cam[3] = { g->camPos.x, g->camPos.y, g->camPos.z };
    float fogCol[3] = { e->skyHorizon.x * 0.95f, e->skyHorizon.y * 0.95f, e->skyHorizon.z * 0.95f };
    float fogDensity = 0.0016f + e->fogAmount * 0.012f;
    float wl = SEA_LEVEL;
    float t = g->waterTime;
    float mode = 0.0f, em = 0.0f, gloss = 0.18f;
    float tint[3] = { 1, 1, 1 };

    SetShaderValue(s, a->uSunDir, sunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, a->uSunColor, sunCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, a->uAmbient, amb, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, a->uCamPos, cam, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, a->uFogColor, fogCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, a->uFogDensity, &fogDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, a->uWaterLevel, &wl, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, a->uTime, &t, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, a->uMode, &mode, SHADER_UNIFORM_INT);
    SetShaderValue(s, a->uEmissive, &em, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, a->uGloss, &gloss, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, a->uTint, tint, SHADER_UNIFORM_VEC3);
}

// ============================================================================
//  Батчинг декора: деревья/камни/камыш/трава собираются в группы по клеткам,
//  чтобы вся растительность рисовалась десятками вызовов, а не тысячами.
// ============================================================================
static void emit_tree(MB *trunk, MB *leaves, int type) {
    trunk->tint = col3(0.34f, 0.26f, 0.18f, 1.0f);
    if (type == 0) {
        mb_cylinder(trunk, (Vector3){ 0, 0, 0 }, 0.34f, 0.16f, 7.6f, 8, 1.0f, true);
        for (int i = 0; i < 5; i++) {
            float y = 1.6f + (float)i * 1.42f;
            float r = 2.35f - (float)i * 0.40f;
            float h = 2.2f - (float)i * 0.14f;
            float shade = 0.85f + 0.15f * hashf((float)i, 3.0f);
            leaves->tint = col3(0.20f * shade, 0.36f * shade, 0.19f * shade, 1.0f);
            mb_cylinder(leaves, (Vector3){ 0, y, 0 }, r, r * 0.18f, h, 10, 1.1f, true);
        }
    } else if (type == 1) {
        Vector3 pts[5] = { { 0, 0, 0 }, { 0.18f, 2.2f, 0.05f }, { -0.10f, 4.4f, 0.12f },
                           { 0.14f, 6.3f, -0.05f }, { 0.05f, 7.6f, 0.0f } };
        mb_tube(trunk, pts, 5, 0.30f, 6, true);
        for (int i = 0; i < 5; i++) {
            float ang = (float)i / 5.0f * 2.0f * PI + 0.4f;
            float shade = 0.82f + 0.18f * hashf((float)i * 2.0f, 7.0f);
            leaves->tint = col3(0.22f * shade, 0.38f * shade, 0.20f * shade, 1.0f);
            mb_ellipsoid(leaves, (Vector3){ cosf(ang) * 1.05f, 6.4f + sinf(ang) * 0.55f, sinf(ang) * 1.05f },
                         (Vector3){ 1.25f, 1.2f, 1.25f }, 8, 6, 1.2f);
        }
        leaves->tint = col3(0.24f, 0.40f, 0.21f, 1.0f);
        mb_ellipsoid(leaves, (Vector3){ 0.05f, 7.9f, 0.0f }, (Vector3){ 1.15f, 1.05f, 1.15f }, 10, 7, 1.2f);
    } else {
        mb_cylinder(trunk, (Vector3){ 0, 0, 0 }, 0.14f, 0.09f, 1.0f, 5, 1.0f, true);
        for (int i = 0; i < 3; i++) {
            float ang = (float)i / 3.0f * 2.0f * PI;
            float shade = 0.85f + 0.15f * hashf((float)i, 11.0f);
            leaves->tint = col3(0.26f * shade, 0.42f * shade, 0.22f * shade, 1.0f);
            mb_ellipsoid(leaves, (Vector3){ cosf(ang) * 0.55f, 0.95f + sinf(ang) * 0.25f, sinf(ang) * 0.55f },
                         (Vector3){ 0.72f, 0.66f, 0.72f }, 7, 5, 1.4f);
        }
    }
}

static void emit_rock(MB *mb) {
    mb->tint = col3(0.92f, 0.92f, 0.90f, 0.95f);
    mb_ellipsoid(mb, (Vector3){ 0, 0.25f, 0 }, (Vector3){ 1.0f, 0.66f, 0.86f }, 8, 6, 1.6f);
    mb_ellipsoid(mb, (Vector3){ 0.42f, 0.05f, 0.30f }, (Vector3){ 0.55f, 0.42f, 0.50f }, 7, 5, 1.6f);
}

static void emit_reed(MB *mb) {
    mb->tint = col3(0.80f, 0.85f, 0.72f, 1.0f);
    for (int i = 0; i < 7; i++) {
        float ang = (float)i * 2.4f;
        float rad = 0.05f + hashf((float)i, 1.0f) * 0.30f;
        float h = 0.9f + hashf((float)i, 2.0f) * 0.9f;
        Vector3 base = { cosf(ang) * rad, 0.0f, sinf(ang) * rad };
        Vector3 mid = { base.x * 0.7f, h * 0.55f, base.z * 0.7f };
        Vector3 tip = { base.x + cosf(ang) * 0.15f, h, base.z + sinf(ang) * 0.15f };
        Vector3 pts[3] = { base, mid, tip };
        mb_tube(mb, pts, 3, 0.028f, 3, true);
    }
}

static void emit_grass(MB *mb) {
    mb->tint = col3(0.82f, 0.90f, 0.70f, 1.0f);
    for (int i = 0; i < 9; i++) {
        float ang = (float)i * 1.7f;
        float rad = 0.02f + hashf((float)i, 5.0f) * 0.26f;
        float h = 0.22f + hashf((float)i, 6.0f) * 0.30f;
        Vector3 base = { cosf(ang) * rad, 0, sinf(ang) * rad };
        Vector3 tip = { base.x + cosf(ang) * 0.10f, h, base.z + sinf(ang) * 0.10f };
        Vector3 pts[2] = { base, tip };
        mb_tube(mb, pts, 2, 0.022f, 3, true);
    }
}

void AssetsBuildPropGroups(Assets *a, Game *g) {
    const int GRID = 6;
    const float cellSize = (2.0f * WORLD_HALF) / (float)GRID;
    const Terrain *t = &g->terrain;
    // При повторной сборке (новая игра) освобождаем предыдущие батчи
    for (int i = 0; i < a->propGroupCount && i < 72; i++) {
        if (a->propGroups[i].meshCount > 0) UnloadModel(a->propGroups[i]);
        memset(&a->propGroups[i], 0, sizeof(Model));
    }
    a->propGroupCount = 0;

    for (int cz = 0; cz < GRID; cz++) {
        for (int cx = 0; cx < GRID; cx++) {
            float x0 = -WORLD_HALF + (float)cx * cellSize;
            float z0 = -WORLD_HALF + (float)cz * cellSize;
            float x1 = x0 + cellSize, z1 = z0 + cellSize;

            MB trunk, leaf, rock, reed, grass;
            mb_init(&trunk, 40000, 60000, "gtrunk");
            mb_init(&leaf, 40000, 60000, "gleaf");
            mb_init(&rock, 20000, 30000, "grock");
            mb_init(&reed, 16000, 24000, "greed");
            mb_init(&grass, 12000, 18000, "ggrass");
            int trees = 0, rocks = 0, reeds = 0, grasses = 0;

            for (int i = 0; i < t->treeCount; i++) {
                Vec3 p = t->trees[i].pos;
                if (p.x < x0 || p.x >= x1 || p.z < z0 || p.z >= z1) continue;
                Matrix xf = MatrixMultiply(MatrixMultiply(MatrixScale(t->trees[i].scale, t->trees[i].scale, t->trees[i].scale),
                                                         MatrixRotateY(t->trees[i].yaw)),
                                          MatrixTranslate(p.x, p.y, p.z));
                trunk.xf = xf;
                leaf.xf = xf;
                emit_tree(&trunk, &leaf, t->trees[i].type);
                trees++;
            }
            for (int i = 0; i < t->rockCount; i++) {
                Vec3 p = t->rocks[i].pos;
                if (p.x < x0 || p.x >= x1 || p.z < z0 || p.z >= z1) continue;
                rock.xf = MatrixMultiply(MatrixMultiply(MatrixScale(t->rocks[i].scale, t->rocks[i].scale * 0.9f, t->rocks[i].scale),
                                                        MatrixRotateY(t->rocks[i].yaw)),
                                         MatrixTranslate(p.x, p.y, p.z));
                emit_rock(&rock);
                rocks++;
            }
            for (int i = 0; i < t->reedCount; i++) {
                Vec3 p = t->reeds[i].pos;
                if (p.x < x0 || p.x >= x1 || p.z < z0 || p.z >= z1) continue;
                reed.xf = MatrixMultiply(MatrixScale(t->reeds[i].scale, t->reeds[i].scale, t->reeds[i].scale),
                                         MatrixTranslate(p.x, p.y, p.z));
                emit_reed(&reed);
                reeds++;
            }
            for (int i = 0; i < t->grassCount; i++) {
                Vec3 p = t->grass[i].pos;
                if (p.x < x0 || p.x >= x1 || p.z < z0 || p.z >= z1) continue;
                grass.xf = MatrixMultiply(MatrixScale(t->grass[i].scale, t->grass[i].scale, t->grass[i].scale),
                                          MatrixTranslate(p.x, p.y, p.z));
                emit_grass(&grass);
                grasses++;
            }

            if (trees + rocks + reeds + grasses > 0) {
                Mesh meshes[5];
                Material mats[5];
                int n = 0;
                if (trees > 0) {
                    meshes[n] = mb_build(&trunk); mats[n] = a->matBark; n++;
                    meshes[n] = mb_build(&leaf); mats[n] = a->matLeaf; n++;
                }
                if (rocks > 0) { meshes[n] = mb_build(&rock); mats[n] = a->matRock; n++; }
                if (reeds + grasses > 0) {
                    if (reeds > 0) { meshes[n] = mb_build(&reed); mats[n] = a->matLeaf; n++; }
                    if (grasses > 0) { meshes[n] = mb_build(&grass); mats[n] = a->matLeaf; n++; }
                }
                if (n > 0 && a->propGroupCount < (int)(sizeof(a->propGroups) / sizeof(a->propGroups[0]))) {
                    a->propGroups[a->propGroupCount++] = BuildModel(meshes, mats, n);
                }
            }
            mb_free(&trunk); mb_free(&leaf); mb_free(&rock); mb_free(&reed); mb_free(&grass);
            prog(0.45f + 0.35f * (float)(cz * GRID + cx + 1) / (float)(GRID * GRID), "Растительность и камни...");
        }
    }
    TraceLog(LOG_INFO, "ASSETS: групп декора %d (деревьев %d, камней %d, камыша %d)",
             a->propGroupCount, t->treeCount, t->rockCount, t->reedCount);
}
