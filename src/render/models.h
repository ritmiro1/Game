#ifndef TB_MODELS_H
#define TB_MODELS_H

#include "raylib.h"
#include "../sim/core.h"

#define TERRAIN_CHUNKS 4
#define TREE_GROUPS 9
#define MAX_SHADERS 6

typedef struct {
    Shader shLit, shWater, shSky, shBlur, shComposite;
    bool litOk, waterOk, skyOk, blurOk, compositeOk;

    Texture2D texSand, texRock, texGrass, texBark, texWood, texMetal, texWaterN;
    Texture2D texCaustics, texFoam, texRain, texGlow, texScales, texLeaf, texCloth, texDetail;

    Material matTerrain, matBark, matWood, matMetal, matRock, matLeaf, matCloth, matScales, matGlow, matSand;

    Model propGroups[72];
    Vec3  propGroupCenter[72];   // для отсечения по расстоянию
    int propGroupCount;
    unsigned int builtSeed;   // сид, под который построен рельеф
    Model terrain[TERRAIN_CHUNKS * TERRAIN_CHUNKS];
    int terrainCount;
    Model water;
    Model sky;
    Model boat;
    Model dock;
    Model lighthouse;
    Model campfire;
    Model tree[3];
    Model rock;
    Model reed;
    Model grass;
    Model fish[MAX_FISH_SPECIES];
    int fishModelCount;
    Model humanParts[6];
    Model humanHat[2];
    Model dog;
    Model seal;
    Model gull;
    Model bobber;
    Model rodSegment[4];
    Model lantern;
    Model crate;
    Model barrel;
    Model signPost;
    Model tent;
    Model quad;

    // Индексы юниформов (для быстрой установки)
    int uMode, uEmissive, uGloss, uTint, uTime;
    int uSunDir, uSunColor, uAmbient, uCamPos, uFogColor, uFogDensity, uWaterLevel;
    int wTime, wWind, wSunDir, wSunColor, wSkyColor, wCamPos, wFogColor, wFogDensity;
    int wShallow, wDeep, wFoamC, wFoamAmt;
    int sSunDir, sSunColor, sSkyTop, sSkyHorizon, sMoonDir, sCloud, sNight, sTime;
    int bThreshold, bDir;
    int cBloom, cBlur, cRes, cDof, cVig, cSat, cCon, cTime, cUnder, cFlash, cTint;
} Assets;

typedef void (*AssetsProgressFn)(float fraction, const char *stage);

bool AssetsInit(Assets *a);
void AssetsSetProgressCallback(AssetsProgressFn fn);
void AssetsBuildWorld(Assets *a, Game *g);
void AssetsBuildPropGroups(Assets *a, Game *g);
void AssetsFree(Assets *a);

// Служебные функции
Material MakeLitMaterial(Assets *a, Texture2D tex);
Model BuildModel(Mesh *meshes, Material *mats, int count);
void SetLitParams(Assets *a, Game *g);

#endif
