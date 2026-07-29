#pragma once

#include "util/map.h"
#include "defs.h"

enum {
    SOUND_FLAG_NONE       = 0 << 0,
    SOUND_FLAG_NO_SLOW_MO = 1 << 0,
};

typedef void *Soloud;

typedef uint sound_id_t;

typedef struct {
    sound_id_t id;
    bool is_3d;
    v3 pos;
    v3 velocity;
    f32 speed;
    f32 volume;
    i64 start_time, last_valid;
    f32 length;
    bool stop;
    bool loop;
    u32 flags;
} playing_sound_t;

typedef struct sound_state {
    allocator_t arena;

    bool valid;

    Soloud *soloud;
    void *bus;
    void *collider, *collider_fp;

    // char* -> Wav*
    map_t sounds;

    // sound_id_t -> playing_sound_t
    map_t playing;

    // global volume
    f32 volume;
} sound_state_t;

// global sound state
extern sound_state_t *g_sound;

// init sound system
bool sound_init(const char **errmsg);

// clear all loaded sounds
void sound_clear();

void sound_update();

playing_sound_t *sound_play(const char *name);

void sound_stop_all();

void sound_stop(sound_id_t id);

playing_sound_t *sound_play_3d(const char *name, v3 pos);

playing_sound_t *sound_get(sound_id_t id);
