// ============================================================================
//  audio.c — звуковой движок: 3D-панорама, слои эмбиента, динамическая музыка
// ============================================================================
#include "audio.h"
#include <stdlib.h>
#include "../platform/pak.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

#define LOOP_WAVES 0
#define LOOP_WIND 1
#define LOOP_RAIN 2
#define LOOP_NIGHT 3
#define LOOP_FIRE 4
#define LOOP_ENGINE 5
#define LOOP_RAIN_WOOD 6
#define LOOP_COUNT 7

typedef struct {
    Music music;
    bool loaded;
    float volume;
    float target;
} Loop;

typedef struct {
    Loop loops[LOOP_COUNT];
    unsigned char *musicData[LOOP_COUNT + MUS_COUNT];
    Music tracks[MUS_COUNT];
    bool trackLoaded[MUS_COUNT];
    float trackVol[MUS_COUNT];
    float trackTarget[MUS_COUNT];
    Sound sfx[SFX_COUNT];
    bool sfxLoaded[SFX_COUNT];
    AudioLevels levels;
    MusicId current;
    float duck;
    bool ready;
} AudioEngine;

static AudioEngine A;

static const char *SFX_FILES[SFX_COUNT] = {
    "sfx_splash_small.wav", "sfx_splash_big.wav", "sfx_cast.wav", "sfx_reel.wav", "sfx_bite.wav",
    "sfx_ui_click.wav", "sfx_ui_hover.wav", "sfx_achieve.wav", "sfx_coin.wav", "sfx_quest.wav",
    "sfx_foot_sand.wav", "sfx_foot_wood.wav", "sfx_foot_grass.wav", "sfx_thunder.wav", "sfx_gull.wav",
    "sfx_bell.wav", "sfx_line_break.wav", "sfx_flop.wav", "sfx_dog.wav", "sfx_seal.wav"
};
static const char *LOOP_FILES[LOOP_COUNT] = {
    "amb_waves.wav", "amb_wind.wav", "amb_rain.wav", "amb_night.wav",
    "amb_fire.wav", "sfx_engine.wav", "amb_rain_wood.wav"
};
static const char *MUSIC_FILES[MUS_COUNT] = {
    "mus_calm.wav", "mus_night.wav", "mus_tension.wav", "mus_win.wav"
};

static Sound load_sound(const char *name, bool *ok) {
    Sound s = { 0 };
    *ok = false;
    unsigned char *data = NULL;
    unsigned int size = 0;
    if (!PakLoad(name, &data, &size) || !data) {
        TraceLog(LOG_WARNING, "AUDIO: не найден %s", name);
        return s;
    }
    Wave w = LoadWaveFromMemory(".wav", data, (int)size);
    if (w.data && w.frameCount > 0) {
        s = LoadSoundFromWave(w);
        *ok = IsSoundValid(s);
    }
    UnloadWave(w);
    free(data);
    return s;
}

static Music load_music(const char *name, bool *ok, unsigned char **keepData) {
    Music m = { 0 };
    *ok = false;
    unsigned char *data = NULL;
    unsigned int size = 0;
    if (!PakLoad(name, &data, &size) || !data) {
        TraceLog(LOG_WARNING, "AUDIO: не найден %s", name);
        return m;
    }
    m = LoadMusicStreamFromMemory(".wav", data, (int)size);
    // ВАЖНО: drwav держит указатель на буфер — освобождать нельзя до выгрузки
    *keepData = data;
    if (IsMusicValid(m)) {
        m.looping = true;
        *ok = true;
    } else {
        free(data);
        *keepData = NULL;
    }
    return m;
}

static bool s_available = false;

void AudioSetAvailable(bool available) { s_available = available; }
bool AudioIsAvailable(void) { return s_available; }

bool AudioInit(void) {
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "AUDIO: аудиоустройство недоступно — звук отключён");
        s_available = false;
        return false;
    }
    s_available = true;
    memset(&A, 0, sizeof(A));
    A.levels.master = 0.9f;
    A.levels.music = 0.55f;
    A.levels.sfx = 0.85f;
    A.levels.ambient = 0.75f;
    A.current = MUS_CALM;

    for (int i = 0; i < SFX_COUNT; i++) A.sfx[i] = load_sound(SFX_FILES[i], &A.sfxLoaded[i]);
    for (int i = 0; i < LOOP_COUNT; i++) {
        A.loops[i].music = load_music(LOOP_FILES[i], &A.loops[i].loaded, &A.musicData[i]);
        A.loops[i].volume = 0.0f;
        A.loops[i].target = 0.0f;
        if (A.loops[i].loaded) PlayMusicStream(A.loops[i].music);
    }
    for (int i = 0; i < MUS_COUNT; i++) {
        A.tracks[i] = load_music(MUSIC_FILES[i], &A.trackLoaded[i], &A.musicData[LOOP_COUNT + i]);
        A.trackVol[i] = 0.0f;
        A.trackTarget[i] = 0.0f;
        if (A.trackLoaded[i]) PlayMusicStream(A.tracks[i]);
    }
    A.ready = true;
    int loaded = 0;
    for (int i = 0; i < SFX_COUNT; i++) if (A.sfxLoaded[i]) loaded++;
    for (int i = 0; i < LOOP_COUNT; i++) if (A.loops[i].loaded) loaded++;
    for (int i = 0; i < MUS_COUNT; i++) if (A.trackLoaded[i]) loaded++;
    TraceLog(LOG_INFO, "AUDIO: загружено %d звуковых потоков", loaded);
    if (loaded <= 0) { s_available = false; return false; }
    return true;
}

void AudioShutdown(void) {
    if (!A.ready) return;
    for (int i = 0; i < SFX_COUNT; i++) if (A.sfxLoaded[i]) UnloadSound(A.sfx[i]);
    for (int i = 0; i < LOOP_COUNT; i++) if (A.loops[i].loaded) UnloadMusicStream(A.loops[i].music);
    for (int i = 0; i < MUS_COUNT; i++) if (A.trackLoaded[i]) UnloadMusicStream(A.tracks[i]);
    for (int i = 0; i < LOOP_COUNT + MUS_COUNT; i++) if (A.musicData[i]) free(A.musicData[i]);
    memset(&A, 0, sizeof(A));
}

void AudioApplyLevels(AudioLevels lv) {
    if (!s_available) return;
    A.levels = lv;
    float m = lv.master;
    for (int i = 0; i < MUS_COUNT; i++) SetMusicVolume(A.tracks[i], 0.0001f);
    (void)m;
}

static float spatial(const Vector3 pos, Vector3 listener, Vector3 fwd, float *panOut) {
    Vector3 right = { fwd.z, 0.0f, -fwd.x };
    Vector3 d = { pos.x - listener.x, pos.y - listener.y, pos.z - listener.z };
    float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    float atten = 1.0f / (1.0f + dist * 0.10f);
    if (dist > 120.0f) atten = 0.0f;
    float side = 0.0f;
    if (dist > 0.01f) side = (d.x * right.x + d.z * right.z) / dist;
    *panOut = 0.5f + side * 0.45f;
    return atten;
}

static Vector3 g_listener = { 0, 0, 0 };
static Vector3 g_fwd = { 0, 0, 1 };

void AudioPlay(SfxId id, Vector3 pos, float volume, float pitch) {
    if (!s_available) return;
    if (!A.ready || id < 0 || id >= SFX_COUNT || !A.sfxLoaded[id]) return;
    float pan = 0.5f;
    float atten = spatial(pos, g_listener, g_fwd, &pan);
    float vol = volume * atten * A.levels.sfx * A.levels.master;
    if (vol < 0.01f) return;
    SetSoundVolume(A.sfx[id], vol);
    SetSoundPan(A.sfx[id], pan);
    SetSoundPitch(A.sfx[id], pitch);
    PlaySound(A.sfx[id]);
}

void AudioPlay2D(SfxId id, float volume, float pitch) {
    if (!s_available) return;
    if (!A.ready || id < 0 || id >= SFX_COUNT || !A.sfxLoaded[id]) return;
    SetSoundVolume(A.sfx[id], volume * A.levels.sfx * A.levels.master);
    SetSoundPan(A.sfx[id], 0.5f);
    SetSoundPitch(A.sfx[id], pitch);
    PlaySound(A.sfx[id]);
}

void AudioSetMusic(MusicId id) {
    if (!s_available) return;
    if (id < 0 || id >= MUS_COUNT) return;
    A.current = id;
}

MusicId AudioCurrentMusic(void) { return A.current; }
void AudioDuckMusic(float amount) { A.duck = amount; }

void AudioUpdate(float dt, Vector3 listenerPos, Vector3 listenerFwd,
                 float windStrength, float rainAmount, float nightAmount,
                 float waterNear, float fireNear, float engineLoad, bool indoorWood,
                 float insideBoat) {
    if (!A.ready) return;
    g_listener = listenerPos;
    g_fwd = listenerFwd;

    for (int i = 0; i < LOOP_COUNT; i++) {
        if (A.loops[i].loaded) UpdateMusicStream(A.loops[i].music);
    }
    for (int i = 0; i < MUS_COUNT; i++) {
        if (A.trackLoaded[i]) UpdateMusicStream(A.tracks[i]);
    }

    float master = A.levels.master;
    float amb = A.levels.ambient * master;
    // Целевые громкости слоёв эмбиента
    A.loops[LOOP_WAVES].target = (0.14f + 0.34f * waterNear) * amb;
    A.loops[LOOP_WIND].target = (0.05f + 0.40f * windStrength) * amb;
    A.loops[LOOP_RAIN].target = rainAmount * 0.55f * amb;
    A.loops[LOOP_NIGHT].target = nightAmount * 0.30f * amb;
    A.loops[LOOP_FIRE].target = fireNear * 0.45f * amb;
    A.loops[LOOP_ENGINE].target = engineLoad * 0.34f * A.levels.sfx * master;
    A.loops[LOOP_RAIN_WOOD].target = (indoorWood ? rainAmount * 0.30f : 0.0f) * amb * (0.4f + 0.6f * insideBoat);

    for (int i = 0; i < LOOP_COUNT; i++) {
        if (!A.loops[i].loaded) continue;
        A.loops[i].volume += (A.loops[i].target - A.loops[i].volume) * fminf(1.0f, dt * 1.6f);
        SetMusicVolume(A.loops[i].music, fmaxf(A.loops[i].volume, 0.0001f));
        if (i == LOOP_ENGINE) {
            float spd = fabsf(engineLoad);
            SetMusicPitch(A.loops[i].music, 0.82f + spd * 0.55f);
        }
    }

    // Музыкальные слои: кроссфейд по состоянию
    for (int i = 0; i < MUS_COUNT; i++) A.trackTarget[i] = 0.0f;
    float musicGain = A.levels.music * master * (1.0f - A.duck * 0.6f);
    switch (A.current) {
        case MUS_CALM: A.trackTarget[MUS_CALM] = musicGain; break;
        case MUS_NIGHT: A.trackTarget[MUS_NIGHT] = musicGain; break;
        case MUS_TENSION: A.trackTarget[MUS_TENSION] = musicGain * 0.9f; break;
        case MUS_WIN: A.trackTarget[MUS_WIN] = musicGain * 0.8f; break;
        default: break;
    }
    for (int i = 0; i < MUS_COUNT; i++) {
        if (!A.trackLoaded[i]) continue;
        A.trackVol[i] += (A.trackTarget[i] - A.trackVol[i]) * fminf(1.0f, dt * 0.7f);
        SetMusicVolume(A.tracks[i], fmaxf(A.trackVol[i], 0.0001f));
    }
    A.duck = fmaxf(0.0f, A.duck - dt * 0.35f);
}
