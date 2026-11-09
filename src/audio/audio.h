#ifndef TB_AUDIO_H
#define TB_AUDIO_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
    SFX_SPLASH_SMALL = 0, SFX_SPLASH_BIG, SFX_CAST, SFX_REEL, SFX_BITE,
    SFX_UI_CLICK, SFX_UI_HOVER, SFX_ACHIEVE, SFX_COIN, SFX_QUEST,
    SFX_FOOT_SAND, SFX_FOOT_WOOD, SFX_FOOT_GRASS, SFX_THUNDER, SFX_GULL,
    SFX_BELL, SFX_LINE_BREAK, SFX_FLOP, SFX_DOG, SFX_SEAL,
    SFX_COUNT
} SfxId;

typedef enum { MUS_CALM = 0, MUS_NIGHT, MUS_TENSION, MUS_WIN, MUS_COUNT } MusicId;

typedef struct {
    float master, music, sfx, ambient;
} AudioLevels;

bool AudioInit(void);
void AudioSetAvailable(bool available);   // false — звуковая подсистема отключена
bool AudioIsAvailable(void);
void AudioShutdown(void);
void AudioApplyLevels(AudioLevels lv);

// Позиционный звук: расстояние + панорама относительно слушателя
void AudioPlay(SfxId id, Vector3 pos, float volume, float pitch);
void AudioPlay2D(SfxId id, float volume, float pitch);

// Слушатель и эмбиент обновляются каждый кадр
void AudioUpdate(float dt, Vector3 listenerPos, Vector3 listenerFwd,
                 float windStrength, float rainAmount, float nightAmount,
                 float waterNear, float fireNear, float engineLoad, bool indoorWood,
                 float insideBoat);

void AudioSetMusic(MusicId id);
MusicId AudioCurrentMusic(void);
void AudioDuckMusic(float amount);

#endif
