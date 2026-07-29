#pragma once

#include "util/types.h"
#include "util/dynlist.h"
#include "util/math.h"
#include "util/map.h"

typedef union SDL_Event SDL_Event;
typedef struct SDL_Window SDL_Window;

#define INPUT_PRESENT      (1 << 7)
#define INPUT_PRESS        (1 << 2)
#define INPUT_RELEASE      (1 << 1)
#define INPUT_DOWN         (1 << 0)
#define INPUT_INVALID      0

// complete information about an input
typedef struct {
    u8 state;
    nstime_t time;
} input_state_t;

// SDL2 input manager
typedef struct input {
    SDL_Window *window;

    allocator_t *allocator;

    // current time ns
    nstime_t now_ns;

    v2i window_size, viewport_size;

    struct {
        v2i pos_raw, motion_raw;

        v2i pos;
        v2 motion;

        bool grab: 1;
    } cursor;

    f32 scroll;

    DYNLIST(input_state_t) buttons;

    // char* -> int index
    map_t name_to_index;

    // cache of name -> SDL_Keycode
    map_t sdl_key_from_name_cache;

    int next_index;

    // buttons which must have PRESS/RELEASE reset next frame
    DYNLIST(int) clear;
} input_t;

// initialize input
void input_init(input_t*, allocator_t *al, SDL_Window *window);

// deinitialize input
void input_destroy(input_t*);

// call at the start of each frame, before input_process_event
void input_update(input_t*, f64 now, v2i window_size, v2i viewport_size);

// process SDL event (call each step for each event after input_step)
void input_process_event(input_t *input, const SDL_Event *ev);

// get state, time for specified string
input_state_t input_get_info(input_t *p, const char *s);

// get button state for specified string
int input_get(input_t*, const char*);

// get time (s) of last event (either PRESS if DOWN or RELEASE if !DOWN)
nstime_t input_time(input_t *p, const char *s);

// get time (seconds) since last event (see input_time)
stime_t input_seconds_since(input_t*, const char*);

#ifdef UTIL_IMPL
#include "ext/sdl2.h" /* IWYU pragma: keep */
#include "util/assert.h"
#include "util/str.h"
#include "platform.h"

// caching wrapped around SDL_GetKeyFromName
static SDL_Keycode get_key_from_name_cached(input_t *input, const char *name) {
    const SDL_Keycode *slot =
        map_get(SDL_Keycode, &input->sdl_key_from_name_cache, name);

    if (slot) {
        return *slot;
    }

    const SDL_Keycode res = SDL_GetKeyFromName(name);
    map_insert(
        &input->sdl_key_from_name_cache,
        mem_strdup(input->allocator, name),
        res);
    return res;
}

void input_init(input_t *input, allocator_t *al, SDL_Window *window) {
    *input = (input_t) {
        .allocator = al,
        .window = window,
    };

    dynlist_init(input->clear, input->allocator);
    dynlist_init(input->buttons, input->allocator);

    map_init(
        &input->name_to_index,
        input->allocator,
        sizeof(char*),
        sizeof(int),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        NULL,
        NULL);

    map_init(
        &input->sdl_key_from_name_cache,
        input->allocator,
        sizeof(char*),
        sizeof(SDL_Keycode),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        NULL,
        NULL);
}

void input_destroy(input_t *p) {
    dynlist_destroy(p->clear);
    dynlist_destroy(p->buttons);
    map_destroy(&p->name_to_index);
    map_destroy(&p->sdl_key_from_name_cache);
    *p = (input_t) { 0 };
}

void input_update(input_t *input, f64 now, v2i window_size, v2i viewport_size) {
    input->now_ns = secs_to_ns(now);
    input->window_size = window_size;
    input->viewport_size = viewport_size;

    dynlist_each(input->clear, it) {
        input->buttons[*it.el].state &= ~(INPUT_PRESS | INPUT_RELEASE);
    }
    dynlist_resize(input->clear, 0);

    input->cursor.motion_raw = v2i_of(0);
    input->cursor.motion = v2_of(0);
    input->scroll = 0.0f;

    // cache cursor state, it's really slow on cocoa (+macos+metal) to
    // continually set the cursor state
    static bool prev_grab;
    RELOAD_STATIC_VAR(prev_grab);

    if (input->cursor.grab != prev_grab) {
        SDL_SetWindowMouseGrab(
            input->window, input->cursor.grab ? SDL_TRUE : SDL_FALSE);
        platform_set_relative_mouse_mode(input->cursor.grab);
    }

    prev_grab = input->cursor.grab;
}

// get (or create!) index from name
static bool try_get_index(
        input_t *input,
        const char *name,
        bool create,
        int *out) {
    // convert name -> lowercase
    char name_lower[64];
    strncpy(name_lower, name, sizeof(name_lower));
    str_to_lower(name_lower);

    int *slot = map_get(int, &input->name_to_index, (char*) name_lower);
    if (slot) {
        *out = *slot;
        return true;
    } else if (!create) {
        *out = UINT_MAX;
        return false;
    }

    // create index
    const int index = input->next_index++;

    ASSERT(dynlist_size(input->buttons) == (int) index);
    *dynlist_push(input->buttons) = (input_state_t) {
        .state = INPUT_PRESENT,
        .time = 0,
    };
    ASSERT(dynlist_size(input->buttons) - 1 == (int) index);

    map_insert(
        &input->name_to_index,
        mem_strdup(input->allocator, (char*) name_lower),
        index);

    *out = index;
    return true;
}

void input_process_event(input_t *input, const SDL_Event *ev) {
    // use frame time as it is accurate *enough*, and we can keep track of
    // multiple events on the same frame for the same key this way
    switch (ev->type) {
    case SDL_MOUSEMOTION:
        input->cursor.motion_raw =
            v2i_add(
                input->cursor.motion_raw,
                v2i_of(ev->motion.xrel, -ev->motion.yrel));
        input->cursor.pos_raw =
            v2i_of(
                ev->motion.x,
                input->window_size.y - ev->motion.y - 1);

        const v2 scale =
            v2_div(
                v2_from_i(input->viewport_size),
                v2_from_i(input->window_size));

        input->cursor.pos =
            v2i_from_v(
                v2_mul(v2_from_i(input->cursor.pos_raw), scale));
        input->cursor.pos =
            v2i_clampv(
                input->cursor.pos,
                v2i_of(0),
                v2i_add(input->viewport_size, v2i_of(-1)));
        input->cursor.motion =
            v2_mul(v2_from_i(input->cursor.motion_raw), scale);
        break;
    case SDL_MOUSEWHEEL:
        input->scroll += ev->wheel.preciseY;
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        const char *name = NULL;
        bool down = false;

        if (ev->type == SDL_KEYDOWN
            || ev->type == SDL_KEYUP) {
            down = ev->key.type == SDL_KEYDOWN;

            // ignore repeats
            if (ev->key.repeat) { break; }

            name = SDL_GetKeyName(ev->key.keysym.sym);
        } else if (
            ev->type == SDL_MOUSEBUTTONDOWN
            || ev->type == SDL_MOUSEBUTTONUP) {
            down = ev->button.type == SDL_MOUSEBUTTONDOWN;
            name = mem_strfmt(tlscratch(), "mouse%d", ev->button.button);
        } else {
            ASSERT(false);
        }

        int i;
        if (!try_get_index(input, name, true, &i)) {
            WARN("could not get index for %s?", name);
            break;
        }

        if (!(input->buttons[i].state & INPUT_DOWN)) {
            input->buttons[i].time = input->now_ns;
        }

        const u8 state = input->buttons[i].state;
        u8 new_state = INPUT_PRESENT;

        if (state & INPUT_PRESS) {
            new_state |= INPUT_PRESS;
        }

        if (state & INPUT_RELEASE) {
            new_state |= INPUT_RELEASE;
        }

        if (down) {
            new_state |= INPUT_DOWN;

            if (!(state & INPUT_DOWN)) {
                new_state |= INPUT_PRESS;
                *dynlist_push(input->clear) = i;
            }
        } else {
            if (state & INPUT_DOWN) {
                new_state |= INPUT_RELEASE;
                *dynlist_push(input->clear) = i;
            }
        }

        if ((state & INPUT_PRESS) || (state & INPUT_RELEASE)) {
            input->buttons[i].time = input->now_ns;
        }

        input->buttons[i].state = new_state;
    } break;
    }
}

// get button state for specified string
static bool get_button_state(
    input_t *p,
    const char *name,
    input_state_t *out) {
    if (str_is_prefixed_by(name, "mouse")) {
        // should be mouse%d, verify
        int index;
        if (sscanf(name, "mouse%d", &index) != 1) {
            WARN("unknown mouse button %s", name);
            return false;
        }
    } else {
#ifdef TARGET_PLATFORM_macos
#   define METANAME "command"
#elifdef TARGET_PLATFORM_windows
#   define METANAME "windows"
#else
#   define METANAME "gui"
#endif // ifdef TARGET_PLATFORM_macos

        // replace meta -> platform-specific name
        if (str_is_suffixed_by(name, " meta")) {
            // should be prefixed by "<left/right> meta"
            if (str_is_prefixed_by(name, "left")) {
                name = "left " METANAME;
            } else if (str_is_prefixed_by(name, "right")) {
                name = "right " METANAME;
            } else {
                WARN("invalid meta name %s", name);
                return false;
            }
        }

        // check that name is valid
        const SDL_Keycode keycode = get_key_from_name_cached(p, name);

        if (keycode == SDLK_UNKNOWN) {
            WARN("unknown button %s", name);
            return false;
        }
    }

    int i;
    if (try_get_index(p, name, false, &i)) {
        *out = p->buttons[i];
    } else {
        // nothing yet for this slot -> 0
        *out = (input_state_t) { .state = INPUT_PRESENT, .time = 0 };
    }

    return true;
}

static bool get_info(input_t *p, const char *name, input_state_t *out) {
    *out = (input_state_t) { 0 };

    if (strstr(name, "%|")) {
        // union of buttons (OR)
        char *dup = mem_strdup(tlscratch(), name);

        char *lasts;
        for (char *tok = strtokm(dup, "%|", &lasts);
             tok != NULL;
             tok = strtokm(NULL, "%|", &lasts)) {

            input_state_t i;
            if (!get_info(p, tok, &i)) {
                return false;
            }

            out->state |= i.state;
            out->time = out->time ? min(out->time, i.time) : i.time;
        }

        return true;
    } else if (strstr(name, "%+")) {
        // multiple buttons (AND)
        char *dup = mem_strdup(tlscratch(), name);

        bool first = true,
             down_norel = false,
             any_press = false,
             any_rel = false;

        char *lasts;
        for (char *tok = strtokm(dup, "%+", &lasts);
             tok != NULL;
             tok = strtokm(NULL, "%+", &lasts)) {

            input_state_t s;
            if (!get_button_state(p, tok, &s)) {
                return false;
            }

            if (first) {
                down_norel = !!(s.state & INPUT_DOWN);
                first = false;
            } else if (!(s.state & INPUT_RELEASE)) {
                down_norel &= !!(s.state & INPUT_DOWN);
            }

            any_press |= !!(s.state & INPUT_PRESS);
            any_rel |= !!(s.state & INPUT_RELEASE);

            out->time = max(s.time, out->time);
        }

        const bool down = !any_rel && down_norel;

        u8 b = INPUT_PRESENT;
        b |= down ? INPUT_DOWN : 0;
        b |= (down && any_press) ? INPUT_PRESS : 0;
        b |= down_norel && any_rel ? INPUT_RELEASE : 0;
        out->state = b;
        return true;
    }

    return get_button_state(p, name, out);
}

input_state_t input_get_info(input_t *p, const char *s) {
    input_state_t info;

    if (!get_info(p, s, &info)) {
        return (input_state_t) { .state = INPUT_INVALID, .time = 0 };
    }

    return info;
}

int input_get(input_t *p, const char *s) {
    input_state_t info;
    if (!get_info(p, s, &info)) { return INPUT_INVALID; }
    return info.state;
}

nstime_t input_time(input_t *p, const char *s) {
    input_state_t info;
    if (!get_info(p, s, &info)) { return I64_MAX; }
    return info.time;
}

stime_t input_seconds_since(input_t *p, const char *s) {
    const nstime_t time = input_time(p, s);
    if (time == I64_MAX) { return 1e10f; }
    return ns_to_secs(p->now_ns - time);
}

#endif // ifdef UTIL_IMPL
