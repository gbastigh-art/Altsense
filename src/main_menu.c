#include "main_menu.h"
#include "fingers.h"
#include "game.h"
#include "gfx/font.h"
#include "gfx/renderer.h"
#include "util/color.h"

typedef struct {
    v2i pos;
    v2i button_pos;
    bool disabled: 1;
    bool center_x: 1;
    bool hovered: 1;
    const char *prefix; // fx. "FOV: "
    const char *button; // fx. "90"
    void (*on_click)();
} text_button_t;

typedef enum {
    MM_OPTION_QUIT,
    MM_OPTION_OPTIONS,
    MM_OPTION_LEADERBOARDS,
    MM_OPTION_PLAY,
    MM_OPTION_ABOUT,
    MM_OPTION_EDITOR,
} main_menu_option_e;

static const char *option_names[] = {
    [MM_OPTION_QUIT]         = "EXIT",
    [MM_OPTION_OPTIONS]      = "OPTIONS",
    [MM_OPTION_LEADERBOARDS] = "LEADERBOARDS",
    [MM_OPTION_PLAY]         = "START",
    [MM_OPTION_ABOUT]        = "ABOUT",
    [MM_OPTION_EDITOR]       = "EDITOR",
};

static main_menu_option_e finger_to_option[] = {
    [FINGER_PINKY]   = MM_OPTION_ABOUT,
    [FINGER_RING]    = MM_OPTION_OPTIONS,
    [FINGER_MIDDLE]  = MM_OPTION_LEADERBOARDS,
    [FINGER_POINTER] = MM_OPTION_PLAY,
    [FINGER_THUMB]   = MM_OPTION_QUIT,
    [FINGER_EX_LAST] = MM_OPTION_EDITOR,
};

// global main_menu state
main_menu_t *g_main_menu;
RELOAD_STATIC_GLOBAL(g_main_menu)

static main_menu_t *mm;
RELOAD_STATIC_GLOBAL(mm)

void main_menu_init() {
    mm = mem_calloc(&g->arena, sizeof(*mm));
    g_main_menu = mm;
}

void main_menu_reset() {
    mm->tilt_up = 0.0f;
    mm->last_play_transition_s = 0.0f;
    mm->hover_state = 0.0f;
    mm->screen = MAIN_MENU_SCREEN_DEFAULT;
}

static void back_on_click() {
    mm->screen = MAIN_MENU_SCREEN_DEFAULT;
}

static void gather_buttons_for_screen(
        main_menu_screen_e screen,
        DYNLIST(text_button_t) *buttons) {
    switch (screen) {
    case MAIN_MENU_SCREEN_OPTIONS: {
        const int x = (g->target_size.x / 2) - 100;
        const int button_x = x + 140;
        int y = g->target_size.y - 80;
        *dynlist_push(*buttons) = (text_button_t) {
            .prefix = "VOLUME",
            .button = "100",
            .pos = v2i_of(x, y),
            .button_pos.x = button_x,
            .on_click = NULL,
        };
        y -= 1.5 * FONT_GLYPH_SIZE.y;
        *dynlist_push(*buttons) = (text_button_t) {
            .prefix = "FULLSCREEN",
            .button = "OFF",
            .pos = v2i_of(x, y),
            .button_pos.x = button_x,
            .on_click = NULL,
        };
        y -= 1.5 * FONT_GLYPH_SIZE.y;
        *dynlist_push(*buttons) = (text_button_t) {
            .prefix = "VSYNC",
            .button = "OFF",
            .pos = v2i_of(x, y),
            .button_pos.x = button_x,
            .on_click = NULL,
        };
        y -= 1.5 * FONT_GLYPH_SIZE.y;
        *dynlist_push(*buttons) = (text_button_t) {
            .prefix = "FOV",
            .button = mem_strfmt(&g->frame_arena, "%d", 90),
            .pos = v2i_of(x, y),
            .button_pos.x = button_x,
            .on_click = NULL,
        };
    } break;
    default:
    }

    if (screen != MAIN_MENU_SCREEN_DEFAULT) {
        *dynlist_push(*buttons) = (text_button_t) {
            .button = "BACK",
            .center_x = true,
            .pos.y = 16,
            .on_click = back_on_click,
        };
    }

    dynlist_each(*buttons, it) {
        if (it.el->center_x) {
            const char *str =
                mem_strfmt(
                    &g->frame_arena,
                    "%s%s",
                    it.el->prefix ? it.el->prefix : "",
                    it.el->button); 
            it.el->pos.x = (g->target_size.x - font_width(str)) / 2;
        }

        // check hover
        v2i pos = it.el->pos;
        if (it.el->button_pos.x != 0) {
            pos.x = it.el->button_pos.x;
        } else if (it.el->prefix) {
            pos.x += font_width(it.el->prefix);
        }

        if (it.el->button_pos.y != 0) {
            pos.y = it.el->button_pos.y;
        }

        const box2i_t box = box2i_ps(pos, font_size(it.el->button));
        it.el->hovered = box2i_contains(box, g->input->cursor.pos);
    }
}

void main_menu_update() {
    if (mm->last_play_transition_s != 0.0f
        && secs_since_s(mm->last_play_transition_s) >= PLAY_TRANSITION_TIME_S) {
        g->mode = GAMEMODE_GAME;
        return;
    }

    g->sky_text.mix = 0.0f;
    g->sky_text.alpha = 0.35f;

    for (finger_e f = FINGER_ALL_FIRST; f <= FINGER_ALL_LAST; f++) {
        if (!g_fingers->fingers[f].present) { continue; }

        const main_menu_option_e opt = finger_to_option[f];

        if (g_fingers->hovered[f]) {
            strbuf_setf(&g->sky_text.texts[1], "$IT%s ", option_names[opt]);
            g->sky_text.mix = 1.0f;
            g->sky_text.alpha =
                0.75f * satf(secs_since_s(g_fingers->hover.begin_s) / 0.16f);
        }

        if (g_fingers->clicked[f]) {
            switch (opt) {
            case MM_OPTION_QUIT: {
                g->quit = true;
            } break;
            case MM_OPTION_OPTIONS: {
                mm->screen = MAIN_MENU_SCREEN_OPTIONS;
            } break;
            case MM_OPTION_ABOUT: {
                mm->screen = MAIN_MENU_SCREEN_ABOUT;
            } break;
            case MM_OPTION_LEADERBOARDS: {
                // TODO
            } break;
            case MM_OPTION_PLAY: {
                mm->last_play_transition_s = g->time.total_scaled_s;
                renderer_add_tint(
                    &(screen_tint_t) {
                        .duration = PLAY_TRANSITION_TIME_S / 1.5f,
                        .fade = true,
                        .color = v4_of(v3_of(1.0f), 0.8f)
                    });

            } break;
            case MM_OPTION_EDITOR: {
                g->should_go_to_editor = true;
            } break;
            }
        }
    }

    // control upwards tilt
    {
        f32 tilt_up_target;
        if (mm->screen == MAIN_MENU_SCREEN_DEFAULT) {
            tilt_up_target = 0.0f;
        } else {
            tilt_up_target = 1.0f;
        }

        mm->tilt_up =
            dtlerp(mm->tilt_up, tilt_up_target, 8.0f, g->time.frame.dt_scaled);
    }

    // TODO: remove
    if (input_get(g->input, "escape") & INPUT_PRESS) {
        mm->screen = MAIN_MENU_SCREEN_DEFAULT;
    }

    // process buttons
    DYNLIST(text_button_t) buttons = NULL;
    dynlist_init(buttons, &g->frame_arena);
    gather_buttons_for_screen(mm->screen, &buttons);

    const int mouse_state = input_get(g->input, "mouse1");
    const bool press = mouse_state & INPUT_PRESS;
    const bool release = mouse_state & INPUT_RELEASE;

    bool any_hover = false;

    dynlist_each(buttons, it) {
        any_hover |= it.el->hovered;

        if (release && it.el->hovered && it.el->on_click) {
            it.el->on_click();
        }
    }

    // smooth lerp hover_state
    {
        f32 hover_state_target;
        if (any_hover) {
            hover_state_target = 1.0f;
        } else {
            hover_state_target = 0.0f;
        }

        mm->hover_state =
            dtlerp(mm->hover_state, hover_state_target, 18.0f, g->time.frame.dt);
    }
}

void main_menu_render() {

}

void main_menu_render_2d(DYNLIST(sprite_t) *sprites) {
    if (mm->screen == MAIN_MENU_SCREEN_DEFAULT) { return; }

    DYNLIST(text_button_t) buttons = NULL;
    dynlist_init(buttons, &g->frame_arena);
    gather_buttons_for_screen(mm->screen, &buttons);

    const int mouse_state = input_get(g->input, "mouse1");
    const bool press = mouse_state & INPUT_PRESS;
    const bool down = mouse_state & INPUT_DOWN;
    const bool release = mouse_state & INPUT_RELEASE;

    dynlist_each(buttons, it) {
        v2 pos = v2_from_i(it.el->pos);
        if (!str_is_empty(it.el->prefix)) {
            font_v(
                pos,
                0.0f,
                v4_of(v3_of(0.75f), mm->tilt_up * 0.9f),
                FONT_DOUBLED,
                GFX_NO_FLAGS,
                sprites,
                "$IT%s",
                it.el->prefix);
            pos.x += font_width(it.el->prefix);
        }

        if (it.el->button_pos.x != 0) { pos.x = it.el->button_pos.x; }
        if (it.el->button_pos.y != 0) { pos.y = it.el->button_pos.y; }

        v3 color;

        {
            f32 t = 0.0f;
            v3 rgb = v3_of(1);

            if (it.el->hovered) {
                if (down) {
                    rgb = v3_scale(v3_of(1.0f, 0.6f, 0.5f), 1.5f);
                } else if (release) {
                    rgb = v3_of(2);
                } else {
                    rgb = v3_of(1.0f, 0.6f, 0.5f);
                }

                t = mm->hover_state;
            }

            color =
                color_xyz_to_rgb(
                    v3_lerp(
                        color_rgb_to_xyz(v3_of(1)),
                        color_rgb_to_xyz(rgb),
                        t));
        }

        if (it.el->hovered) {
            pos.x -= font_width(">");
            font_v(
                pos,
                0.0f,
                v4_of(color, mm->hover_state * 0.9f),
                FONT_DOUBLED,
                GFX_NO_FLAGS,
                sprites,
                "$IT>");
            pos.x += font_width(">");
        }

        font_v(
            pos,
            0.0f,
            v4_of(color, mm->tilt_up * 0.9f),
            FONT_DOUBLED,
            GFX_NO_FLAGS,
            sprites,
            "$IT%s",
            it.el->button);

        pos.x += font_width(it.el->button);

        if (it.el->hovered) {
            font_v(
                pos,
                0.0f,
                v4_of(color, mm->hover_state * 0.9f),
                FONT_DOUBLED,
                GFX_NO_FLAGS,
                sprites,
                "$IT<");
        }
    }

    switch (mm->screen) {
    case MAIN_MENU_SCREEN_OPTIONS: {

    } break;
    case MAIN_MENU_SCREEN_ABOUT: {
        const char *str = "$ITTO DO";
        font_v(
            v2_from_i(v2i_divs(v2i_sub(g->target_size, font_size(str)), 2)),
            0.0f,
            v4_of(v3_of(1), mm->tilt_up * 0.9f),
            FONT_DOUBLED,
            GFX_NO_FLAGS,
            sprites,
            "%s",
            str);
    } break;
    default: ASSERT(false);
    }

}
