#include "../../lib/soloud/include/soloud_c.h"
#include "sound/sound.h"
#include "level/level_types.h"
#include "util/hooks.h"
#include "util/time.h"
#include "util/file.h"
#include "game.h"

sound_state_t *g_sound;
RELOAD_STATIC_GLOBAL(g_sound)

RELOAD_VISIBLE void free_wav(map_t*, void *p) {
    Wav_destroy(*((Wav**) p));
}

RELOAD_VISIBLE float collide_f(
        int handle,
        float pos_x,
        float pos_y,
        float pos_z,
        float v_x,
        float v_y,
        float v_z,
        int id,
        void *userdata) {
    // void *ptr = map_find(&g_sound->playing, handle);
    return 1.0f;
}

RELOAD_VISIBLE void sound_destroy(void*) {
    if (!g_sound->valid) { return; }

    // Collider_destroy(g_sound->collider);
    Bus_destroy(g_sound->bus);

    Soloud_deinit(g_sound->soloud);
    Soloud_destroy(g_sound->soloud);

    // RELOAD_DELETE_FUNCPTR(&g_sound->collider_fp)

    heap_allocator_destroy(&g_sound->arena);
    g_sound->valid = false;
}

bool sound_init(const char **errmsg) {
    hook_register(HOOK_EXIT, sound_destroy, NULL);

    g_sound = mem_calloc(&g->arena, sizeof(*g_sound));
    heap_allocator_init(&g_sound->arena, &g->arena, NULL);

    g_sound->soloud = Soloud_create();
    if (!g_sound->soloud) { return -1; }

    const int soloud_res = Soloud_init(g_sound->soloud);
    if (soloud_res) {
        *errmsg =
            mem_strfmt(
                tlscratch(),
                "error initializing soloud: %d",
                soloud_res);
        return false;
    }

    g_sound->bus = Bus_create();
    if (!g_sound->bus) {
        *errmsg = mem_strfmt(tlscratch(), "error creating soloud bus");
        return false;
    }

    // g_sound->collider = Collider_create(collide_f, NULL, &g_sound->collider_fp);
    // if (!g_sound->collider) {
    //     *errmsg = mem_strfmt(tlscratch(), "error creating soloud collider");
    //     return false;
    // }

    // RELOAD_FUNCPTR(&g_sound->collider_fp);

    // sound system is usable
    g_sound->valid = true;

    // char* -> Wav*
    map_init(
        &g_sound->sounds,
        &g_sound->arena,
        sizeof(char*),
        sizeof(Wav),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        free_wav,
        NULL);

    // sound_id_t -> playing_sound_t
    map_init(
        &g_sound->playing,
        &g_sound->arena,
        sizeof(sound_id_t),
        sizeof(playing_sound_t),
        map_hash_bytes,
        map_cmp_bytes,
        NULL, NULL, NULL);

    LOG(
        "initialized sound engine: %s/%d/%d",
        Soloud_getBackendString(g_sound->soloud),
        Soloud_getBackendChannels(g_sound->soloud),
        Soloud_getBackendSamplerate(g_sound->soloud));

    Soloud_setGlobalVolume(g_sound->soloud, 1.0f);
    Bus_setVolume(g_sound->soloud, 1.0f);
    // Bus_set3dColliderEx(g_sound->bus, g_sound->collider, 0);
    Soloud_play(g_sound->soloud, g_sound->bus);
    return true;
}

void sound_clear() {
    if (!g_sound->valid) { return; }

    map_each(sound_id_t, playing_sound_t, &g_sound->playing, it) {
        Soloud_stop(g_sound->soloud, *it.key);
    }
    map_clear(&g_sound->sounds);
    map_clear(&g_sound->playing);
}

void sound_update() {
    if (!g_sound->valid) { return; }

    Soloud_setGlobalVolume(g_sound->soloud, satf(g_sound->volume));
    Soloud_set3dListenerPosition(g_sound->soloud, v3_spread(g->cam.pos));
    Soloud_set3dListenerAt(g_sound->soloud, v3_spread(g->cam.dir));
    Soloud_set3dListenerUp(g_sound->soloud, v3_spread(g->cam.up));
    if (g->player) {
        Soloud_set3dListenerVelocity(g_sound->soloud, v3_spread(g->player->vel_xyz));
    }

    map_each(sound_id_t, playing_sound_t, &g_sound->playing, it) {
        if (!Soloud_isValidVoiceHandle(g_sound->soloud, it.value->id)) {
            map_remove_it(&g_sound->playing, it);
            continue;
        }

        Soloud_set3dSourcePosition(
            g_sound->soloud,
            *it.key,
            v3_spread(it.value->pos));
        Soloud_set3dSourceVelocity(
            g_sound->soloud,
            *it.key,
            v3_spread(it.value->velocity));

        it.value->volume = satf(it.value->volume);
        it.value->speed = max(it.value->speed, 0.0f);

        Soloud_setRelativePlaySpeed(
            g_sound->soloud,
            *it.key,
            it.value->speed
                * ((it.value->flags & SOUND_FLAG_NO_SLOW_MO) ?
                    1.0f :
                    g->time_scale));
        Soloud_setVolume(g_sound->soloud, *it.key, it.value->volume);
        Soloud_setLooping(g_sound->soloud, *it.key, it.value->loop);
    }

    Soloud_update3dAudio(g_sound->soloud);
}

static Wav *get_or_load_wav(const char *name) {
    ASSERT(g_sound->valid);

    Wav **pwav = map_getp(Wav*, &g_sound->sounds, &name), *wav = NULL;

    if (pwav) {
        wav = *pwav;
    } else {
        // check if sound exists as resource
        const char *path =
            mem_strfmt(tlscratch(), "assets/sounds/%s.wav", name);

        if (!file_exists(path)) {
            WARN("could not find sound %s", name);
            return NULL;
        }

        wav = Wav_create();
        LOG("loading new wav %s from %s", name, path);

        if (!wav) {
            WARN("could not create wav");
            return NULL;
        }

        int res;
        if ((res = Wav_load(wav, path))) {
            WARN("bad wav load %s: %d", path, res);
            return NULL;
        }

        pwav = map_insert(&g_sound->sounds, mem_strdup(&g_sound->arena, name), wav);
    }

    return wav;
}

playing_sound_t *sound_play(const char *name) {
    if (!g_sound->valid) { return NULL; }

    Wav *wav = get_or_load_wav(name);
    if (!wav) { return NULL; }

    sound_id_t id = Bus_play(g_sound->bus, wav);
    Soloud_setVolume(g_sound->soloud, id, 1.0f);

    return
        map_insertp(
            &g_sound->playing,
            &id,
            &((playing_sound_t) {
                .id = id,
                .length = Wav_getLength(wav),
                .start_time = g->time.total_ns,
                .stop = false,
                .volume = 1.0f,
                .speed = 1.0f,
                .loop = false,
            }));
}

void sound_stop_all() {
    if (!g_sound->valid) { return; }

    map_each(sound_id_t, playing_sound_t, &g_sound->playing, it) {
        Soloud_stop(g_sound->soloud, *it.key);
    }

    map_clear(&g_sound->playing);
}

void sound_stop(sound_id_t id) {
    if (!g_sound->valid) { return; }

    if (map_try_remove(&g_sound->playing, id)) {
        Soloud_stop(g_sound->soloud, id);
    }
}

playing_sound_t *sound_play_3d(const char *name, v3 pos) {
    if (!g_sound->valid) { return NULL; }

    Wav *wav = get_or_load_wav(name);
    if (!wav) { return NULL; }

    sound_id_t id = Bus_play3d(g_sound->bus, wav, v3_spread(pos));
    Wav_set3dColliderEx(wav, g_sound->collider, id);
    Soloud_setVolume(g_sound->soloud, id, 1.0f);
    Soloud_set3dSourceAttenuation(g_sound->soloud, id, 2, 1.5f);
    Soloud_set3dSourceMinMaxDistance(g_sound->soloud, id, 0.0f, 48.0f);

    return
        map_insertp(
            &g_sound->playing,
            &id,
            (&(playing_sound_t) {
                .id = id,
                .is_3d = true,
                .pos = pos,
                .velocity = v3_of(0),
                .length = Wav_getLength(wav),
                .start_time = g->time.total_ns,
                .stop = false,
                .speed = 1.0f,
                .volume = 1.0f,
                .loop = false,
            }));
}

playing_sound_t *sound_get(sound_id_t id) {
    if (!g_sound->valid) { return NULL; }
    if (!id) { return NULL; }
    return map_get(playing_sound_t, &g_sound->playing, id);
}
