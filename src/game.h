#pragma once

#include "util/alloc.h"
#include "util/rand.h"
#include "util/ini.h"
#include "util/input.h"
#include "util/time.h"
#include "level/lptr.h"
#include "ext/sokol.h"
#include "defs.h"

#include "gfx/debug_draw.h" /* IWYU pragma: keep */
#include "reloadhost.h"     /* IWYU pragma: keep */

typedef struct ImGuiContext ImGuiContext;
typedef struct SDL_Window SDL_Window;
typedef void *SDL_GLContext;

typedef struct game {
    // application-lifetime arena
    allocator_t arena;

    // tick-lifetime arena
    allocator_t frame_arena;

    SDL_Window *window;

    void *imgui_ctx;

    v2i window_size;

    // size of render target, pixel scaled
    v2i target_size;

    // if true, vsync is enabled
    bool vsync;

    // if != 0 and !vsync, fps is limited to this value
    int max_fps;

    // VISOPT_*
    int visopt;

    // GAMEOPT_*
    int gameopt;

    // ticks at TICKS_PER_SECOND, repsects time scale during gameplay
    int tick;

    struct {
        // time when last second was registered
        nstime_t last_second_ns;

        // time of last frame, nanoseconds
        nstime_t last_frame_ns;

        // average time for frame -> swapchain get
        nstime_t frame_to_swapchain_avg_ns;

        struct {
            // total number of (frames/fixed update/whatever)
            int count;

            // total in the last second
            int count_per_second;

            // total number in *current* second
            int count_this_second;

            // total in last frame
            int count_per_frame;

            // total in *current* frame
            int count_this_frame;

            // current dt + slow-mo scaled
            stime_t dt, dt_scaled;

            // delta, delta scaled
            nstime_t dt_ns, dt_scaled_ns;

            // average duration of frame/fixed update/whatever
            nstime_t avg_duration_ns;

            // "remainder" from this frame, only applicable for !frame
            nstime_t remainder_ns;
        } frame, fixed, tick;

        // total nanoseconds since start + slow-mo scaled
        nstime_t total_ns, total_scaled_ns;

        stime_t total_s, total_scaled_s;
    } time;

    // current time scale, >0
    f32 time_scale;

    // time remaining in slow-motion (TODO: units!!)
    f32 slow_mo_time;

    struct {
        // current camera sector, can be NULL
        lptr_t sector;

        v3 pos, dir, dir_xy, right, up;
        f32 pitch, yaw;
        f32 slide;
        f32 fov;
        m4 view, proj, view_proj;
        m4 view_no_pitch, view_proj_no_pitch;
        m4 inv_view, inv_proj, inv_view_proj;

        frustum_3d_t frustum;

        m4 phantom_view, phantom_proj, phantom_view_proj;
        m4 inv_phantom_view_proj;
    } cam;

    // current extra_id_pos "id" under cursor
    i32 cursor_id;

    bool quit;
    input_t *input;

    level_t *level;

    game_mode_e mode;

    // if true, mouse is forced free regardless of game state
    // TODO: disable in release
    bool force_mouse_free;

    // camera (editor)/player (game) input
    bool allow_control_input;

    // if true, editor will do 3D picking - can disable for perf reasons
    bool allow_editor_picking;

    // *DEFERRED RENDERER* sgl context
    sgl_context sgl_ctx;

    // current level path
    char level_path[PATH_MAX];

    // global random, use for costmetic randoms
    rand_t rand;

    // current player
    entity_t *player;

    struct {
        hash_t last_level_palette_hash;
        palette_t *level;

        palette_t *generic;
    } palettes;

    f32 gamma;

    // if true, player is spawned at specified location instead of at ENTITY_TYPE_SPAWN
    struct {
        bool do_force_spawn;
        v2 pos;
    } force_spawn;

    struct {
        input_state_t up, down, left, right;
        input_state_t jump;
        input_state_t shoot;
        input_state_t dash;
        input_state_t slide;
        input_state_t switch_weapon;
    } controls;

    struct {
        // allocator sizes
        union {
            struct {
                allocator_stats_t
                    g_arena,
                    level_arena,
                    frame_arena;
            };

            allocator_stats_t allocators[3];
        };
    } stats;

    // expanded (file_fix_path'd) paths
    struct {
        char *data_dir;
        char *settings;
        char *editor_settings;
        char *assets;
        char *levels;
    } path;

    // default settings (used for fallbacks)
    ini_t default_settings;

    // game settings
    ini_t settings;

    // editor settings
    ini_t editor_settings;

    // particle simulation for health/ammo displays
    particle_sim_t *psim;

    // interlevel text generator
    mtextgen_t *text_gen;

    union {
        struct {
            bool show_debug;
            bool show_particle_config;
            bool show_renderer_debug;
            bool show_stats;
            bool wireframe;
            bool show_particles;
            bool show_hulls;
            bool show_screen_verts;
            f32 psim_lock_ms;
            v2 psim_draw_offset;
            bool pause_particles;
            int pause_particle_tick;
            f32 pause_particle_s;
            bool no_ai;
            bool no_phantom;
            bool override_particles_black;
            bool half_hand_particles;
            bool no_vignette;
            bool no_dither256;
            bool no_dither8;
            bool no_bloom;
            bool show_finger_boxes;
        };

#ifdef TARGET_DEBUG
        u8 scratch0[16 * 1024];
#endif // ifdef TARGET_DEBUG
    } debug;

    // scratch space for when you want to do weird debugging things, but don't
    // want the debugger to crash on reload :-)
    union {
        struct {
            box2f_t boxes[15];

            struct {
                tex_id_t small[16];
                tex_id_t large[16];
            } blood_textures;

            // liquid fall effect
            struct {
                strbuf_t texts[2];
                f32 effect; // 0..1
                f32 mix;    // 0..1, mix from text0 to text1
                f32 alpha;  // total alpha
            } liquid_fall;

            // sky text effect
            struct {
                strbuf_t texts[2];
                f32 mix;   // 0..1
                f32 alpha; // 0..1, fades in from regular sky background
            } sky_text;

            // extra bloom strength (see renderer.c)
            f32 extra_bloom;

            // if non-empty, game will teleport here (keeping same game mode)
            // on next update.
            // will try to go to level "levels/<teleport_level>.json"
            strbuf_t teleport_level;

            int player_death_tick;

            f32 contrast_effect;

            v4 phantom_color;

            // transition text effect (centered texture with scrolling in
            // background)
            struct {
                f32 strength; // 0..1
                strbuf_t text;
            } transition_effect;

            // time total_scaled_s when level was set
            f32 last_level_set_s;

            // if true, game goes to main menu at next frame
            bool should_go_to_main_menu;

            // last heartbeat time in nanoseconds, based on total_scaled_ns
            nstime_t last_heartbeat_ns;

            struct {
                strbuf_t texts[2]; // vignette_tex0/1
                f32 tex_mix;       // mix from texts[0]..texts[1]
                v4 color;          // color of non-tex part
                v4 tex_color;      // color of tex part (the text)
                f32 strength;      // 0..1
            } vignette;

            // if true, map editor is accessible
            bool editor_enabled;

            // if true, game goes to editor at next frame
            bool should_go_to_editor;

            strbuf_t hand_text;

            // hand-liquid-agnostic override color
            v4 hand_override_color;

            // if true, player is moved to next room on following tick
            bool move_player_to_next_room;
        };

#ifdef TARGET_DEBUG
        u8 scratch1[16 * 1024];
#endif // ifdef TARGET_DEBUG
    };
} game_t;

// global game 
extern game_t *g;

// switch game modes
bool game_set_mode(game_mode_e mode, const char **errmsg);

// sets current level, keeping gamemode (respawns player in game mode)
// if path exists, then level is loaded, otherwise a blank level is initialized
// with the specified path
bool game_set_level(const char *path, const char **errmsg);

// pull current application state -> settings inis
void game_update_settings();

// update settings and dump to inis
void game_update_and_dump_settings();

// try to go to the main menu
bool game_go_to_main_menu(const char **errmsg);

// ticks elapsed since tick
M_INLINE int ticks_since_tick(int event) {
    return max(g->tick - event, 0);
}

// seconds elapsed since tick
M_INLINE stime_t secs_since_tick(int event) {
    return ticks_since_tick(event) * (1.0f / (f64) TICKS_PER_SECOND);
}

// seconds since nanosecond event
M_INLINE stime_t secs_since_ns(nstime_t event) {
    return ns_to_secs(g->time.total_scaled_ns - event);
}

// seconds since second event
M_INLINE stime_t secs_since_s(stime_t event) {
    return ns_to_secs(g->time.total_scaled_ns) - event;
}

// absolute seconds since nanosecond event
M_INLINE stime_t abs_secs_since_ns(nstime_t event) {
    return ns_to_secs(g->time.total_ns - event);
}

// absolute seconds since second event
M_INLINE stime_t abs_secs_since_s(stime_t event) {
    return ns_to_secs(g->time.total_ns) - event;
}

v3 compute_left_hand_particle_color();

v3 compute_right_hand_particle_color();
