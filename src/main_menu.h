#pragma once

#include "defs.h"

typedef enum {
    MAIN_MENU_SCREEN_DEFAULT,
    MAIN_MENU_SCREEN_OPTIONS,
    MAIN_MENU_SCREEN_ABOUT,
} main_menu_screen_e;

typedef struct {
    f32 tilt_up; // 0..1
    main_menu_screen_e screen;

#ifdef TARGET_DEBUG
    union {
#endif // ifdef TARGET_DEBUG
        struct {
            f32 hover_state; // 0..1
            stime_t last_play_transition_s;
        };

#ifdef TARGET_DEBUG
        u8 scratch[16 * 1024];
    };
#endif // ifdef TARGET_DEBUG
} main_menu_t;

// global main menu state
extern main_menu_t *g_main_menu;

void main_menu_init();

void main_menu_reset();

void main_menu_update();

void main_menu_render();

void main_menu_render_2d(DYNLIST(sprite_t) *sprites);
