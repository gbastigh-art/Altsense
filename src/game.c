#include "cam.h"
#define RELOADHOST_CLIENT_IMPL
#include "reloadhost.h"

#include "level/room.h"
#include "mtext.h"
#include "level/sector.h"
#include "main_menu.h"
#include "util/primes.h"
#include "blood.h"
#include "fingers.h"
#include "vtext.h"
#include "level/io.h"
#include "editor/ui_misc.h"
#include "level/entity.h"
#include "particle_sim.h"
#include "sound/sound.h"
#include "config.h"
#include "editor/editor.h"
#include "ext/stb_image_write.h"
#include "gfx/tex_atlas.h"
#include "gfx/debug_draw.h"
#include "gfx/font.h"
#include "gfx/model.h"
#include "gfx/palette.h"
#include "gfx/passes.h"
#include "gfx/renderer.h"
#include "gfx/shaders.h"
#include "gfx/sprite.h"
#include "gfx/screenquad.h"
#include "ext/cimgui.h"
#include "level/block.h"
#include "level/level.h"
#include "level/particle.h"
#include "reloadhost.h"
#include "ext/sdl2.h"
#include "game.h"
#include "util/file.h"
#include "util/hooks.h"
#include "util/input.h"
#include "util/time.h"
#include "platform.h"

#include "ext/sokol.h"

// global game state, hot reloaded
game_t *g = NULL;
RELOAD_STATIC_GLOBAL(g)

// sg_imgui state
static sgimgui_t sg_imgui;
RELOAD_STATIC_GLOBAL(sg_imgui)

bool game_set_mode(game_mode_e mode, const char **errmsg) {
    if (mode == g->mode) { return true; }

    g->mode = mode;

    // reload level if it does not already exist
    if (!g->level) { return true; }

    return game_set_level(g->level_path, errmsg);
}

bool game_set_level(const char *path, const char **errmsg) {
    g->player_death_tick = 0;

    switch (g->mode) {
    case GAMEMODE_EDITOR:
    case GAMEMODE_GAME:
        g_fingers->mode = FINGERS_MODE_SHOOT;
        break;
    case GAMEMODE_MAIN_MENU:
        g_fingers->mode = FINGERS_MODE_MENU;
        break;
    }

    sound_stop_all();

    if (g->level) {
        level_destroy(g->level);
    } else {
        g->level = mem_alloc(&g->arena, sizeof(*g->level));
    }

    level_init(g->level, &g->arena);

    if (path && path[0] != '\0') {
        bool load_failed = false;
        allocator_t *allocator = g_mallocator;
        range_t data = { 0 };
        const file_error_e file_err = file_read(&data, allocator, path);
        if (file_err != FILE_OK) {
            load_failed = true;
            *errmsg =
                mem_strfmt(
                    tlscratch(),
                    "file error: %s",
                    file_error_to_str(file_err));
            goto done_loading;
        }

        const io_error_e io_err = io_load_level(g->level, &data);
        if (io_err != IO_OK) {
            load_failed = true;
            *errmsg =
                mem_strfmt(
                    tlscratch(),
                    "io error: %s",
                    io_error_to_str(io_err));
            goto done_loading;
        }

done_loading:
        mem_free_range(allocator, &data);
        if (load_failed) {
            WARN("failed to set level: %s", errmsg);
            return false;
        }
    }

    g->cam.sector = LPTR_NULL;
    g->player = NULL;
    g_editor->saved_version = -1;
    g_editor->level = g->level;

    renderer_set_level(g->level);

    snprintf(
        g->level_path,
        sizeof(g->level_path),
        "%s",
        path && path[0] != '\0' ? path : "");

    if (g->mode != GAMEMODE_GAME && g->mode != GAMEMODE_MAIN_MENU) {
        return true; // OK, no more config if not spawning things
    }

    // spawn player
    v3 spawn_pos;
    f32 spawn_angle = 0.0f;

    if (g->force_spawn.do_force_spawn) {
        g->force_spawn.do_force_spawn = false;
        spawn_pos =
            v3_of(
                g->force_spawn.pos,
                level_point_zs(g->level, g->force_spawn.pos).z0);
    } else if (g->level->entities_by_type[ENTITY_TYPE_SPAWN_POINT].head) {
        // spawn point
        const entity_t *spawn_point =
            g->level->entities_by_type[ENTITY_TYPE_SPAWN_POINT].head;
        spawn_pos = spawn_point->pos_xyz;
        spawn_angle = spawn_point->spawn_point_angle;
    } else {
        // entry juice
        sector_t *entry_sect = NULL;
        level_each(sector_t, &g->level->sectors, it) {
            if (it.el->type == SECTOR_TYPE_ENTRY_JUICE) {
                entry_sect = it.el;
                break;
            }
        }

        // TODO: have to offset to fix a nasty bug with exact sector centers
        // not being detected as "in" the sector
        if (entry_sect) {
            spawn_pos =
                v3_of(
                    v2_add(
                        box2f_center(entry_sect->bounds),
                        v2_of(0.01f, 0.01f)),
                    entry_sect->floor.z + 5.0f);
        } else {
            WARN("level has no entry sect");
            const v2 point = level_clamp_point(g->level, v2_of(2));
            spawn_pos = v3_of(point, level_point_zs(g->level, point).z0);
        }
    }

    spawn_pos = level_clamp_point_3d(g->level, spawn_pos, 1.0f);

    // spawn player
    g->player =
        entity_new(
            g->level,
            &(entity_t) {
                .itype = ENTITY_TYPE_PLAYER,
            });

    g->cam.yaw = spawn_angle;
    g->player->dir = v3_of(cosf(spawn_angle), sinf(spawn_angle), 0.0f);

    g->player->pos = v2_of(0);
    entity_try_move(g->level, g->player, v2_from(spawn_pos));
    g->player->z = spawn_pos.z;
    g->slow_mo_time = 0;
    g->psim->reset = true;

    if (g->level->is_hub) {
        // fade in on entry
        renderer_add_tint(
            &(screen_tint_t) {
                .duration = 0.25f,
                .fade = true,
                .color = v4_of(v3_of(0.0f), 1.0f),
            });
    }

    g->last_level_set_s = g->time.total_scaled_s;
    return true;
}

void game_update_settings() {
    ini_set_bool(&g->settings, "debug", "no_ai", g->debug.no_ai);
    ini_set_f32(&g->settings, "gfx", "gamma", g->gamma);

    int w, h;
    SDL_GetWindowSize(g->window, &w, &h);
    ini_set_int(&g->settings, "gfx", "width", w);
    ini_set_int(&g->settings, "gfx", "height", h);

    const int display = SDL_GetWindowDisplayIndex(g->window);
    ini_set_int(&g->settings, "gfx", "display", display);

    ini_set_bool(&g->settings, "gfx", "vsync", g->vsync);
    ini_set_int(&g->settings, "gfx", "max_fps", g->max_fps);

    const int flags = SDL_GetWindowFlags(g->window);

    const char *fullscreen = "off";

    if (flags & SDL_WINDOW_FULLSCREEN) {
        fullscreen = "on";
    } else if ((flags & SDL_WINDOW_MAXIMIZED)) {
#ifdef TARGET_PLATFORM_macos
        // on OSX, maximized windows are borderless fullscreen
        fullscreen = "borderless";
#endif // ifdef TARGET_PLATFORM_macos
    }

    ini_set(&g->settings, "gfx", "fullscreen", fullscreen);

    ini_set_f32(
        &g->settings, "sound", "volume_global", g_sound->volume);
}

void game_update_and_dump_settings() {
    game_update_settings();
	ini_dump_to_path(&g->settings, g->path.settings);
}

bool game_go_to_main_menu(const char **errmsg) {
    main_menu_reset();

    if (g->mode == GAMEMODE_MAIN_MENU && g->level) {
        // already in main menu
        return true;
    }

    g->mode = GAMEMODE_MAIN_MENU;

    // go to hub
    if (!game_set_level("levels/hub.json", errmsg)) {
        ERROR("could not go to levels/hub.json: %s", errmsg);
        return false;
    }

    return true;
}

static void render_ui() {
    if (g->mode == GAMEMODE_EDITOR || !g->player) { return; }

    DYNLIST(sprite_t) sprites =
        dynlist_create(sprite_t, &g->frame_arena);

    if (g->player_death_tick != 0) {
        const char *str = "$ITCONSUMED";
        const v2i size = font_size(str);
        font_v(
            v2_from_i(v2i_divs(v2i_sub(g->target_size, size), 2)),
            0.0f,
            v4_of(
                v3_of(1),
                ease_cubic_out(secs_since_tick(g->player_death_tick) / 0.33f)),
            FONT_DOUBLED,
            GFX_NO_FLAGS,
            &sprites,
            str);
    }

    /* font_v( */
    /*     v2_of(10), */
    /*     0.0f, */
    /*     v4_of(1), */
    /*     FONT_DOUBLED, */
    /*     GFX_NO_FLAGS, */
    /*     &sprites, */
    /*     "dir: %" PRIv3, */
    /*     FMTv3(g->player->dir)); */

    font_v(
        v2_of(20),
        0.0f,
        v4_of(1),
        FONT_DOUBLED,
        GFX_NO_FLAGS,
        &sprites,
        "$ITALTERED\n SENSORIUM");

    if (g->mode == GAMEMODE_MAIN_MENU) {
        main_menu_render_2d(&sprites);
    }

    const m4
        view = m4_identity(),
        proj =
            cam_ortho(
                0.0f, g->target_size.x,
                0.0f, g->target_size.y,
                1.0f, -1.0f);

    sprite_batch_render(
        g_passes.overlay.attach,
        sprites,
        dynlist_size(sprites),
        &proj,
        &view);
}

static void load_settings_inis() {
    // load settings inis
    ini_error_e ini_err;

    if (ini_valid(&g->default_settings)) {
        ini_destroy(&g->default_settings);
    }

	ini_init(&g->default_settings, &g->arena);
    if ((ini_err =
            ini_parse_from_path(
                &g->default_settings,
                file_join_path(
                    &g->frame_arena,
                    g->path.assets,
                    "default_settings.ini")))
            != INI_OK) {
        WARN(
            "error loading default_settings.ini: %s",
            ini_error_to_str(ini_err));
    }

    if (ini_valid(&g->settings)) {
        ini_destroy(&g->settings);
    }

	ini_init(&g->settings, &g->arena);
    if ((ini_err =
			ini_parse_from_path(
                &g->settings, g->path.settings))
            != INI_OK) {
        WARN(
			"error loading settings.ini (%s): %s",
			g->path.settings,
			ini_error_to_str(ini_err));
    }

    if (ini_valid(&g->editor_settings)) {
        ini_destroy(&g->editor_settings);
    }

	ini_init(&g->editor_settings, &g->arena);
    if ((ini_err =
            ini_parse_from_path(
                &g->editor_settings, g->path.editor_settings))
            != INI_OK) {
        WARN(
            "error loading editor.ini (%s): %s",
            g->path.editor_settings,
            ini_error_to_str(ini_err));
    }

    g->vsync =
        ini_get_bool_or_default(&g->settings, "gfx", "vsync", false);

    g->max_fps =
        ini_get_int_or_default(&g->settings, "gfx", "max_fps", 0);

#ifdef TARGET_DEBUG
    g->debug.no_ai =
        ini_get_bool_or_default(&g->settings, "debug", "no_ai", false);
#else
    g->debug.no_ai = false;
#endif // ifdef TARGET_DEBUG
}

// recompute target size (size of internal pixellated fb)
// always fit height and adjust width accordingly
static void set_target_size() {
    g->target_size.y = TARGET_HEIGHT;
    g->target_size.x =
        g->window_size.x * (TARGET_HEIGHT / (f32) g->window_size.y);
}

static void init() {
    // TODO: release
    g->editor_enabled = true;

    // so we don't have to check timers for != 0
    g->time.frame.count = 1;
    g->time.tick.count = 1;
    g->time.fixed.count = 1;

    g->teleport_level = strbuf_create(&g->arena);
    g->transition_effect.text = strbuf_create(&g->arena);
    g->liquid_fall.texts[0] = strbuf_create(&g->arena);
    g->liquid_fall.texts[1] = strbuf_create(&g->arena);
    g->sky_text.texts[0] = strbuf_create(&g->arena);
    g->sky_text.texts[1] = strbuf_create(&g->arena);
    g->vignette.texts[0] = strbuf_create(&g->arena);
    g->vignette.texts[1] = strbuf_create(&g->arena);
    g->hand_text = strbuf_create(&g->arena);

    trig_init();

    g->allow_editor_picking = true;
    g->allow_control_input = true;
    g->rand = rand_create(0xDEADBEEF);

    // allocate in 4 MiB blocks
    bump_allocator_init(
        &g->frame_arena,
        g_mallocator,
        4 * 1024 * 1024,
        &g->stats.frame_arena);

    // load paths
    {
        char *pref_path = SDL_GetPrefPath("jdh", "altsense");
        g->path.data_dir = mem_strdup(&g->arena, pref_path);
        SDL_free(pref_path);

        if (!file_exists(g->path.data_dir)) {
            LOG("no directory %s, creating", g->path.data_dir);
            const file_error_e err =
                file_mkdir(
                    file_expand_path(
                        &g->frame_arena,
                        g->path.data_dir), true);
            ASSERT(err == FILE_OK);
        } else {
            LOG("found data dir %s", g->path.data_dir);
        }
    }

    g->path.settings =
        file_join_path(&g->arena, g->path.data_dir, "settings.ini");
    g->path.editor_settings =
        file_join_path(&g->arena, g->path.data_dir, "editor.ini");

    g->path.assets = mem_strdup(&g->arena, "assets");
    g->path.levels = mem_strdup(&g->arena, "levels");

    // if settings file doesn't exist, overwrite with default
    if (!file_exists(g->path.settings)) {
        LOG("no settings at %s, copying defaults", g->path.settings);
        const file_error_e err =
            file_copy(
                g->path.settings,
                file_join_path(
                    &g->frame_arena,
                    g->path.assets,
                    "default_settings.ini"));

        if (err != FILE_OK) {
            WARN("failed to copy default settings: %s", file_error_to_str(err));
        }
    } else {
        LOG("found settings %s", g->path.settings);
    }

    load_settings_inis();

    g->gamma =
        clamp(
            ini_get_f32_or_default(
                &g->settings, "gfx", "gamma", 1.0f),
            0.0f, 3.0f);

    ASSERT(
        !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO),
        "failed to init SDL: %s", SDL_GetError());

    SDL_version compiled;
    SDL_version linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    LOG(
        "Compiled SDL version: %d.%d.%d",
        compiled.major, compiled.minor, compiled.patch);
    LOG(
        "Linked SDL version: %d.%d.%d",
        linked.major, linked.minor, linked.patch);

	/* const int display = */
	/* 	ini_get_int_or_default(&g->settings, "gfx", "display", 0); */

    g->window =
        SDL_CreateWindow(
            "ALTERED SENSORIUM",
            SDL_WINDOWPOS_CENTERED_DISPLAY(0),
            SDL_WINDOWPOS_CENTERED_DISPLAY(0),
            1600,
            900,
            0
#ifdef SOKOL_GLCORE
            | SDL_WINDOW_OPENGL
#endif // ifdef SOKOL_GLCORE
            | SDL_WINDOW_SHOWN
            | SDL_WINDOW_RESIZABLE);
    ASSERT(g->window);

    /* const char *fullscreen_type = */
    /*     ini_get_or_default(&g->settings, "gfx", "fullscreen", "off"); */

    /* if (!strcmp(fullscreen_type, "borderless")) { */
    /*     SDL_SetWindowFullscreen(state->window, SDL_WINDOW_FULLSCREEN_DESKTOP); */
/* #ifdef GAME_PLATFORM_osx */
    /*     SDL_MaximizeWindow(state->window); */
/* #endif // ifdef GAME_PLATFORM_osx */
    /* } else if (!strcmp(fullscreen_type, "on")) { */
/* #ifdef GAME_PLATFORM_osx */
		/* SDL_SetWindowFullscreen(state->window, SDL_WINDOW_FULLSCREEN_DESKTOP); */
/* #else */
		/* SDL_SetWindowFullscreen(state->window, SDL_WINDOW_FULLSCREEN); */
/* #endif // ifdef GAME_PLATFORM_osx */
    /* } */

    // init gfx platform
    platform_init(g->window);

    g->input = mem_alloc(&g->arena, sizeof(*g->input));
    input_init(g->input, &g->arena, g->window);

    sg_setup(
        &(sg_desc) {
            .allocator.alloc_fn = sokol_ext_alloc,
            .allocator.free_fn = sokol_ext_free,
            .environment = platform_environment(),
            .logger.func = sokol_ext_log,
        });
    ASSERT(sg_query_features().storage_buffer, "storage buffers not supported");
    ASSERT(sg_isvalid());

    simgui_setup(
        &(simgui_desc_t) {
            .allocator.alloc_fn = sokol_ext_alloc,
            .allocator.free_fn = sokol_ext_free,
        });
    igCreateContext(NULL);

    sgimgui_init(
        &sg_imgui,
        &(sgimgui_desc_t) {
            .allocator.alloc_fn = sokol_ext_alloc,
            .allocator.free_fn = sokol_ext_free,
        });

    Imgui_ImplSDL_Init(g->window);

    platform_size(&g->window_size.x, &g->window_size.y);
    set_target_size();

    shaders_init();
    passes_init();
    renderer_init();

    sgp_setup(
        &(sgp_desc) {
            .max_vertices = 65536 * 32,
            .max_commands = 16384,
            .color_format =
                sg_query_image_desc(g_passes.editor.color).pixel_format,
            .depth_format =
                sg_query_image_desc(g_passes.editor.depth).pixel_format,
        });
    
    // create SGP pipeline with custom shader
    // const sg_pipeline sgp_pipeline =
    //     sgp_make_pipeline(
    //         &(sgp_pipeline_desc) {
    //             .shader = shaders_get(SHADER_SGP),
    //             .color_format = 
    //                 sg_query_image_desc(g_passes.editor.color).pixel_format,
    //             .depth_format =
    //                 sg_query_image_desc(g_passes.editor.depth).pixel_format,
    //         });
    // sgp_set_pipeline(sgp_pipeline);

    sgl_setup(
        &(sgl_desc_t) {
            .allocator.alloc_fn = sokol_ext_alloc,
            .allocator.free_fn = sokol_ext_free,
            .logger.func = sokol_ext_log,
        });
    g->sgl_ctx =
        sgl_make_context(
            &(sgl_context_desc_t) {
                .max_vertices = 65536,
                .max_commands = 16384,
                .color_format =
                    sg_query_image_desc(g_passes.deferred.color)
                        .pixel_format,
                .depth_format =
                    sg_query_image_desc(g_passes.deferred.depth_stencil)
                        .pixel_format,
                .sample_count = 1,
            });

    tex_atlas_init(&g->arena);
    tex_atlas_load_all();

    model_atlas_init(&g->arena);

    g->palettes.level = mem_alloc(&g->arena, sizeof(*g->palettes.level));
    palette_init(g->palettes.level);

    g->palettes.generic = mem_alloc(&g->arena, sizeof(*g->palettes.generic));
    palette_init(g->palettes.generic);
    palette_load_gpl(g->palettes.generic, PALETTE_GENERIC);

    const char *errmsg;
    if (!sound_init(&errmsg)) {
        WARN("failed to initialize sound: %s", errmsg);
    } else {
        // TODO: settings configurable
        g_sound->volume = 1.0f;
    }

    // TODO: configurable with argv to launch directly into editor
    g->mode = GAMEMODE_MAIN_MENU;

    editor_init();
    fingers_init();
    main_menu_init();
    blood_init();

    g->psim = mem_alloc(&g->arena, sizeof(*g->psim));
    particle_sim_init(
        g->psim,
        &g->arena,
        &(particle_sim_desc_t) {
            .render_offset = v2_of(0.0f, -1.0f),
            .bounds = v2_of(6.0f, 6.0f * (9.0f / 16.0f)),
            .kern_radius = 0.45f,
            .collision_radius = 0.0125f,
            .boundary_thickness = 0.01f,
            .visc = 50.0f,
            .target_density = 80.0f,
            .pressure_mult = 60.0f,
            .near_pressure_mult = 35.0f,
            .boundary_force = 100.0f,
            .boundary_force_dist = 0.5f,
            .gravity = v2_of(0.0f, -6000.0f),
            .bounds_restitution = 0.01f,
            .lookahead = 1.0f / 120.0f,
            .dt_scale = 1.0f,
            .dt_target = 1.0f / 60.0f,
        });

    // TODO: markov text gen
    g->text_gen = mem_alloc(&g->arena, sizeof(*g->text_gen));
    {
        strbuf_t corpus = strbuf_create(tlscratch());
        const file_error_e err =
            file_read_strbuf(&corpus, "assets/misc/corpus_shelley.txt");
        ASSERT(err == FILE_OK, "%s", file_error_to_str(err));
        mtextgen_init(g->text_gen, &g->arena, corpus, 2);
    }

    if (g->mode == GAMEMODE_MAIN_MENU) {
        ASSERT(!g->level, "main menu but level is already set?");

        const char *errmsg;
        if (!game_go_to_main_menu(&errmsg)) {
            ERROR("error going to main menu: %s, going to editor", errmsg);

            if (!game_set_level(NULL, &errmsg)) {
                ERROR("error setting null level: %s", errmsg);
            }

            g->mode = GAMEMODE_EDITOR;
        }
    } else {
        // start with a blank level if one was not auto-loaded by editor
        if (!g->level) {
            const char *errmsg;
            ASSERT(
                game_set_level(NULL, &errmsg),
                "could not set level to NULL: %s",
                errmsg);
        }
    }
}

static void deinit() {
    particle_sim_destroy(g->psim);

    hook_call_hooks(HOOK_EXIT);

    game_update_and_dump_settings();

    tex_atlas_destroy();
    model_atlas_destroy();
    renderer_destroy();
    level_destroy(g->level);

    sgl_shutdown();
    sgp_shutdown();
    Imgui_ImplSDL_Shutdown();
    simgui_shutdown();
    sg_shutdown();

    platform_destroy();
    SDL_DestroyWindow(g->window);

    bump_allocator_destroy(&g->frame_arena);

    SDL_Quit();

    // free global arena memory
    heap_allocator_destroy(&g->arena);
}

static void screenshot(const char *out_path, bool raw) {
    // TODO: fix screenshotting
    return;

    LOG("screenshotting to %s (%d)", out_path, raw);
    const v2i size =
        raw ?
            g->window_size
            : v2i_of(g->target_size.x, g->target_size.y);

    void *pixels = mem_alloc(&g->frame_arena, size.x * size.y * 4);

    if (raw) {
        /* sg_query_pixels( */
        /*     0, 0, g->window_size.x, g->window_size.y, */
        /*     false, */
        /*     pixels, */
        /*     size.x * size.y * 4); */
    } else {
        /* sg_query_image_pixels( */
        /*     g_passes.post2.color, pixels, size.x * size.y * 4); */
    }

    stbi_flip_vertically_on_write(true);
    const int res =
        stbi_write_png(out_path, size.x, size.y, 4, pixels, size.x * 4);
    if (res != 1) {
        LOG("  failure: %d", res);
    }

    mem_free(&g->frame_arena, pixels);
}

static void do_debug_ui() {
    if (g->debug.show_debug
        && igBegin(
            "debug",
            &g->debug.show_debug,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        if (igTreeNodeEx_Str("sg", ImGuiTreeNodeFlags_DefaultOpen)) {
            igCheckbox("buffer",      &sg_imgui.buffer_window.open);
            igCheckbox("image",       &sg_imgui.image_window.open);
            igCheckbox("sampler",     &sg_imgui.sampler_window.open);
            igCheckbox("shader",      &sg_imgui.shader_window.open);
            igCheckbox("pipeline",    &sg_imgui.pipeline_window.open);
            igCheckbox("attachments", &sg_imgui.attachments_window.open);
            igCheckbox("capture",     &sg_imgui.capture_window.open);
            igCheckbox("caps",        &sg_imgui.caps_window.open);
            igCheckbox("stats",       &sg_imgui.frame_stats_window.open);
            igTreePop();
        }

        if (igTreeNodeEx_Str("visopt", 0)) {
            input_flags(
                &g->visopt,
                VISOPT_MASK,
                visopt_desc()->names,
                NULL,
                0);
            igTreePop();
        }

        igCheckbox("stats", &g->debug.show_stats);
        igCheckbox("particle config", &g->debug.show_particle_config);
        igCheckbox("renderer debug", &g->debug.show_renderer_debug);
        igCheckbox("wireframe", &g->debug.wireframe);
        if (igCheckbox("pause particles", &g->debug.pause_particles)) {
            g->debug.pause_particle_tick = g->tick;
            g->debug.pause_particle_s = g->time.total_scaled_s;
        }
        igCheckbox("no ai", &g->debug.no_ai);
        igCheckbox("ovr. particles black", &g->debug.override_particles_black);
        igCheckbox("no phantom", &g->debug.no_phantom);
        igCheckbox("half hand particles", &g->debug.half_hand_particles);
        igCheckbox("no vignette", &g->debug.no_vignette);
        igCheckbox("no dither 256", &g->debug.no_dither256);
        igCheckbox("no dither 8", &g->debug.no_dither8);
        igCheckbox("no bloom", &g->debug.no_bloom);
        igCheckbox("finger boxes", &g->debug.show_finger_boxes);
        igEnd();
    }

    if (g->debug.show_particle_config
        && igBegin(
            "particle config",
            &g->debug.show_particle_config,
            ImGuiWindowFlags_AlwaysAutoResize)) {

        igText("  dt avg. (ms): %.3f", g->psim->stats.dt_avg_ms);
        igText("step avg. (ms): %.3f", g->psim->stats.step_avg_ms);
        igText("hull avg. (ms): %.3f", g->psim->stats.hull_avg_ms);
        igText("   num threads: %d", g->psim->stats.threads);
        igText("     lock (ms): %.3f", g->debug.psim_lock_ms);
        igText("     num. left: %d", g->psim->stats.n_hand_particles[0]);
        igText("    num. right: %d", g->psim->stats.n_hand_particles[1]);

        igCheckbox("show particles?", &g->debug.show_particles);
        igCheckbox("show hulls?", &g->debug.show_hulls);
        igCheckbox("show hand verts?", &g->debug.show_screen_verts);
        igInputFloat2("draw offset", g->debug.psim_draw_offset.raw, "%.3f", 0);

        particle_sim_desc_imgui(&g->psim->desc);

        igEnd();
    }

    sgimgui_draw(&sg_imgui);

    if (g->debug.show_stats
        && igBegin(
            "stats",
            &g->debug.show_stats,
            ImGuiWindowFlags_AlwaysAutoResize)) {

#define STAT(_n, _fmt, ...)   \
    igTableNextRow(0, 0);     \
    igTableSetColumnIndex(0); \
    igText("%s", _n);         \
    igTableSetColumnIndex(1); \
    igText(_fmt, __VA_ARGS__)

        if (igTreeNode_Str("MEM"))  {
            if (igBeginTable("MEM", 2, 0, (ImVec2) {}, 0.0f)) {
                STAT("...", "%s", "USED (M) / PEAK (M) / RESERVED (M)");

                struct {
                    const char *name;
                    allocator_stats_t stats;
                } allocators[] = {
                    { "SYSTEM",    global_malloc_stats()      },
                    { "TLSCRATCH", *tlscratch()->stats,       },
                    { "MALLOC",    *g_mallocator->stats,      },
                    { "SOKOL",     *sokol_ext_arena()->stats, },
                    { "GLOBAL",    g->stats.g_arena,          },
                    { "LEVEL",     g->stats.level_arena,      },
                    { "FRAME",     g->stats.frame_arena,      },
                };

                for (int i = 0; i < ARRLEN(allocators); i++) {
                    allocator_stats_t *stats = &allocators[i].stats;
                    STAT(
                        allocators[i].name,
                        "%7.2f    %7.2f    %7.2f",
                        stats->used     / (1024.0f * 1024.0f),
                        stats->peak     / (1024.0f * 1024.0f),
                        stats->reserved / (1024.0f * 1024.0f));
                }

                igEndTable();
            }

            igTreePop();
        }

        if (igTreeNode_Str("LEVEL"))  {
            if (igBeginTable("LEVEL", 2, 0, (ImVec2) {}, 0.0f)) {

                STAT("...", "%s", "  SIZE /    CAP / USAGE (K) / OVERHEAD (K)");

                for (int i = LT_VERTEX; i <= LT_ROOM; i++) {
                    STAT(
                        level_type_to_str(i),
                        "%6d   %6d   %6.2f     %6.2f",
                        g->level->lists[i].data.size,
                        g->level->lists[i].data.capacity,
                        blklist_footprint(&g->level->lists[i].data) / 1024.0f,
                        blklist_overhead(&g->level->lists[i].data) / 1024.0f);
                }

                igEndTable();
            }

            igTreePop();
        }

        igEnd();
#undef STAT
    }
}

static void frame_update() {
    // process events
    input_update(
        g->input,
        ns_to_secs(g->time.total_ns),
        g->window_size,
        v2i_of(g->target_size.x, g->target_size.y));

    // allow game mode ImGui mouse input
    if (g->mode == GAMEMODE_GAME || g->mode == GAMEMODE_MAIN_MENU) {
        if (g->input->cursor.grab) {
            igGetIO()->ConfigFlags |= ImGuiConfigFlags_NoMouse;
        } else {
            igGetIO()->ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
    }

    if (g->mode == GAMEMODE_EDITOR) {
        g->allow_control_input =
            !g_editor->ui_has_keyboard
            && g_editor->mode == EDITOR_MODE_CAM;
    } else {
        g->allow_control_input = true;
    }

    bool update_settings = false;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            g->quit = true;
            break;
        case SDL_WINDOWEVENT:
            switch (ev.window.event) {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_MAXIMIZED:
                update_settings = true;
                break;
            }
            break;
        }

        Imgui_ImplSDL_ProcessEvent(&ev);
        input_process_event(g->input, &ev);
    }

    if (update_settings) {
        game_update_settings();
    }

#ifdef TARGET_DEBUG
    if (input_get(g->input, "F1") & INPUT_PRESS) {
        g->debug.show_debug = !g->debug.show_debug;
    }

    if (input_get(g->input, "F10") & INPUT_PRESS) {
        strbuf_set(&g->teleport_level, "hub");
    }

    if (input_get(g->input, "F6") & INPUT_PRESS) {
        g->should_go_to_main_menu = true;
    }
#endif // ifdef TARGET_DEBUG

    if (g->mode == GAMEMODE_EDITOR) {
        g->input->cursor.grab =
            g_editor->mouse_grab
            && g_editor->mode == EDITOR_MODE_CAM;
    } else {
        g->input->cursor.grab = !g->force_mouse_free;
    }

    if (g->mode == GAMEMODE_GAME
        && (input_get(g->input, "z") & INPUT_PRESS)) {
        g->slow_mo_time = 10.0f;
        sound_play("s_slowmo");
    }

    if (g->player) {
        if (g->level->is_hub || g_fingers->mode == FINGERS_MODE_EDIT) {
            g->psim->n_target_particles[0] = 0;
            g->psim->n_target_particles[1] = 0;
        } else {
            const f32 min_percent = 0.05f;

            f32 health = g->player->health / g->player->ptype->max_health;
            if (health < 0.1f) {
                health = 0.0f;
            } else if (health > 0.0f && health < min_percent) {
                health = min_percent;
            }
            g->psim->n_target_particles[0] = 700 * health;


            f32 stamina = g->player->stamina / 100.0f;
            if (stamina < 0.1f) {
                stamina = 0.0f;
            } else if (stamina > 0.0f && stamina < min_percent) {
                stamina = min_percent;
            }
            g->psim->n_target_particles[1] = 700.0f * health;


            if (g->debug.half_hand_particles) {
                g->psim->n_target_particles[0] = 700.0f * 0.5f;
                g->psim->n_target_particles[1] = 700.0f * 0.5f;
            }
        }
    }

    // table of control names in settings.ini mapped to input info location
    struct { const char *setting; input_state_t *pinfo; } controls[] = {
        { "up",            &g->controls.up            },
        { "down",          &g->controls.down          },
        { "left",          &g->controls.left          },
        { "right",         &g->controls.right         },
        { "jump",          &g->controls.jump          },
        { "shoot",         &g->controls.shoot         },
        { "dash",          &g->controls.dash          },
        { "slide",         &g->controls.slide         },
        { "switch_weapon", &g->controls.switch_weapon },
    };

    for (int i = 0; i < ARRLEN(controls); i++) {
        input_state_t info = { .state = INPUT_INVALID, .time = 0 };
        char *name =
            ini_get_str_or_null(
                &g->settings,
                "controls",
                controls[i].setting,
                &g->frame_arena);

        if (name) {
            info = input_get_info(g->input, name);
        }

        if (info.state == INPUT_INVALID) {
            WARN(
                "invalid control %s: %s, using fallback from defaults",
                controls[i].setting,
                name);

            // try to get fallback from deafult settings
            name =
                ini_get_str_or_null(
                    &g->default_settings,
                    "controls",
                    controls[i].setting,
                    &g->frame_arena);

            if (name) {
                info = input_get_info(g->input, name);
            } else {
                WARN(
                    "could not find %s in default settings",
                    controls[i].setting);
            }
        }

        *controls[i].pinfo = info;
    }

    if (g->should_go_to_editor) {
        g->should_go_to_editor = false;

        if (g->editor_enabled) {
            g->mode = GAMEMODE_EDITOR;
        }
    }

    // TODO: disable hotkeys under "true" gameplay
    if (g->allow_control_input
        || (g->mode == GAMEMODE_EDITOR
            && g_editor->mode == EDITOR_MODE_MAP)) {

        // mouse grab toggle
        if ((g->mode == GAMEMODE_GAME || g->mode == GAMEMODE_MAIN_MENU)
            && (input_get(g->input, "1") & INPUT_PRESS)) {
            g->force_mouse_free = !g->force_mouse_free;
        }

        if (input_get(g->input, "F4") & INPUT_PRESS) {
            palette_load_gpl(g->palettes.generic, PALETTE_GENERIC);

            sound_clear();
            load_settings_inis();
            tex_atlas_reset();
            tex_atlas_load_all();
            model_atlas_reset();
            renderer_set_level(g->level);
            shaders_reload();
        }

        if ((input_get(g->input, "F3") & INPUT_PRESS)) {
            if (g->mode == GAMEMODE_EDITOR
                && g->level->version != g_editor->saved_version) {
                const char *errmsg;
                if (!editor_try_save_level(g->level_path, &errmsg)) {
                    WARN("error saving level: %s", errmsg);
                }
            }

            if (g->mode == GAMEMODE_EDITOR
                && (input_get(g->input, "right shift") & INPUT_DOWN)) {
                g->force_spawn.do_force_spawn = true;
                g->force_spawn.pos =
                    level_clamp_point(g->level, v2_from(g->cam.pos));
            }

            game_mode_e target_mode;

            switch (g->mode) {
            case GAMEMODE_EDITOR:
                target_mode = GAMEMODE_GAME;
                break;
            case GAMEMODE_GAME:
            case GAMEMODE_MAIN_MENU:
                target_mode = GAMEMODE_EDITOR;
                break;
            }

            const char *errmsg;
            if (!game_set_mode(target_mode, &errmsg)) {
                WARN("could set mode: %s", errmsg);
            }
        }

        if (input_get(g->input, "F5") & INPUT_PRESS) {
            // reload level, keep current gamemode
            const char *errmsg;
            if (!game_set_level(g->level_path, &errmsg)) {
                WARN("could not reload level: %s", errmsg);
            }
        }

        if (input_get(g->input, "F7") & INPUT_PRESS) {
            g->allow_editor_picking = !g->allow_editor_picking;
        }

        if (input_get(g->input, "F2") & INPUT_PRESS) {
            char timestamp[256], filename[1024];
            str_fmt_timestamp(timestamp, sizeof(timestamp));
            snprintf(
                filename,
                sizeof(filename),
                "screens/screenshot_%s.png",
                timestamp);
            str_to_safe_filename(filename);

            // TODO: awful
            system("mkdir -p screens");
            screenshot(
                filename,
                !(input_get(g->input, "left shift") & INPUT_DOWN));
        }
    }


    if (g->mode == GAMEMODE_GAME || g->mode == GAMEMODE_MAIN_MENU) {
        g->cursor_id =
            f32_bits_to_i32(renderer_info_at(g->input->cursor.pos).y);
    } else {
        g->cursor_id = 0;
    }

    // find player
    entity_t *ent = NULL;
    level_each(entity_t, &g->level->entities, it) {
        if (it.el->itype == ENTITY_TYPE_PLAYER) {
            ent = it.el;
            break;
        }
    }

    ASSERT(g->mode != GAMEMODE_GAME || ent, "could not find player?");
    g->player = ent;

    g->cam.sector =
        lptr_from(
            level_find_point_sector(
                g->level,
                v2_from(g->cam.pos),
                lptr_sector(g->level, g->cam.sector)));

    do_debug_ui();
    vtexts_update();
    tex_atlas_update();

    if (g->player) {
        fingers_update(g->level, g->player);
    }

    level_frame_update(g->level);

#ifdef TARGET_DEBUG
    level_each(entity_t, &g->level->entities, it) {
        const entity_t *e = it.el;
        ASSERT_DEBUG(
            fabsf(v3_norm(e->dir) - 1.0f) < 0.001f,
            "%d/%s",
            e->id,
            entity_type_to_str(e->itype));
        ASSERT_DEBUG(
            fabsf(v3_norm(e->fly_dir) - 1.0f) < 0.001f,
            "%d/%s",
            e->id,
            entity_type_to_str(e->itype));
        ASSERT_DEBUG(
            fabsf(v2_norm(e->walk_dir) - 1.0f) < 0.001f,
            "%d/%s",
            e->id,
            entity_type_to_str(e->itype));
        ASSERT_DEBUG(
            fabsf(v3_norm(e->target_last_seen_dir) - 1.0f) < 0.001f,
            "%d/%s",
            e->id,
            entity_type_to_str(e->itype));
        ASSERT_DEBUG(
            fabsf(v3_norm(e->turret_dir) - 1.0f) < 0.001f,
            "%d/%s",
            e->id,
            entity_type_to_str(e->itype));
    }

#endif // ifdef TARGET_DEBUG

    const bool player_in_liquid =
        g->mode == GAMEMODE_GAME
        && g->player
        && g->player->sector
        && g->player->in_liquid
        && g->cam.pos.z < g->player->sector->floor.z + g->player->sector->liquid_offset;

    const bool in_entry_juice =
        player_in_liquid && g->player->sector->type == SECTOR_TYPE_ENTRY_JUICE;

    const bool in_exit_juice =
        player_in_liquid && g->player->sector->type == SECTOR_TYPE_EXIT_JUICE;

    const stime_t liquid_transition_hold_s = 2.5f;
    stime_t since_liquid_enter = 0.0f;

    if (g->player) {
        if (g->player->room && g->player->room->is_entry) {
            // entry room, player entered liquid at level load
            since_liquid_enter = secs_since_s(g->last_level_set_s);
        } else {
            since_liquid_enter = secs_since_tick(g->player->liquid_enter_tick);
        }
    }

    // control finger mode
    if (g->mode == GAMEMODE_GAME) {
        if (in_entry_juice) {
            g_fingers->mode = FINGERS_MODE_EDIT; 
        } else {
            static bool force_edit_mode = false;
            RELOAD_STATIC_VAR(force_edit_mode);


            if (input_get(g->input, "e") & INPUT_PRESS) {
                force_edit_mode = !force_edit_mode;
            }

            g_fingers->mode =
                force_edit_mode ?
                    FINGERS_MODE_EDIT
                    : FINGERS_MODE_SHOOT;
        }
    }

    // control transition entry/exit juice transitions
    {
        // compute transition effect into level
        if (in_entry_juice || in_exit_juice) {
            f32 strength = 0.0f;

            if (since_liquid_enter < liquid_transition_hold_s) {
                strength = 1.0f;
            } else {
                strength =
                    ease_exp_inout(
                        invsatf(
                            (since_liquid_enter - liquid_transition_hold_s)
                                / 0.8f));
            }

            g->transition_effect.strength = strength;
        } else {
            g->transition_effect.strength = 0.0f; 
        }

        if (in_entry_juice) {
            // level transition text
            strbuf_setf(
                &g->transition_effect.text,
                "$IT%s ",
                g->level->name);
        } else if (in_exit_juice) {
            // room-to-room transition text
            static int last_gen_tick = -1;

            if (g->tick - last_gen_tick > 1) {
                const char *errmsg;

                strbuf_t tmp = strbuf_create(tlscratch());
                rand_t rand = rand_create(time_epoch_ns());
                if (mtextgen_gen(
                        g->text_gen,
                        &tmp,
                        5,
                        &rand,
                        &errmsg)) {
                    strbuf_each(tmp, it) {
                        *it.el = toupper(*it.el);
                    }
                    strbuf_setf(
                        &g->transition_effect.text,
                        "$IT%s ",
                        tmp);
                } else {
                    ERROR("error generating text: %s", errmsg);
                    ASSERT(false);
                }
            }

            last_gen_tick = g->tick;
        }

        if (in_exit_juice) {
            // if we've been in exit juice for > hold seconds, exit it
            if (since_liquid_enter >= liquid_transition_hold_s) {
                g->move_player_to_next_room = true;
            }
        } else if (in_entry_juice) {
            // leave entry juice when requested
            if (input_get(g->input, "o") & INPUT_PRESS) {
                entity_do_liquid_exit(g->level, g->player);
            }
        }
    }

    // should the player be moved to the next room?
    if (g->move_player_to_next_room) {
        g->move_player_to_next_room = false;

        if (g->mode == GAMEMODE_GAME && g->player) {
            // TODO: room sequencing
            // find another non-completed room with an entry sector portal
            bool found = false;
            level_each(room_t, &g->level->rooms, it) {
                if (it.el->is_entry) { continue; }
                if (it.el->is_completed) { continue; }

                const sector_t *room_entry_sect =
                    room_find_sector_with_type(
                        g->level,
                        it.el,
                        SECTOR_TYPE_ROOM_ENTRY_JUICE);

                if (!room_entry_sect) { continue; }

                found = true;
                ent->dir = v3_normalize_of(1, 0, 0);
                g->cam.yaw = atan2f(ent->dir.y, ent->dir.x);
                g->liquid_fall.effect = 0.0f;

                entity_try_move(
                    g->level,
                    ent,
                    box2f_center(room_entry_sect->bounds));
                ent->z = sector_point_zs(room_entry_sect, ent->pos).z1 - 2.0f;
                g_fingers->mode = FINGERS_MODE_SHOOT;

                entity_do_liquid_exit(g->level, ent);
            }

            if (!found) {
                strbuf_set(&g->teleport_level, "hub");
            }
        }
    }

    // control hand text
    {
        if (g->mode == GAMEMODE_MAIN_MENU) {
            strbuf_set(&g->hand_text, "$ITALTERED SENSORIUM ");
        } else if (g->mode == GAMEMODE_GAME) {
            char *str;

            if (g->mode == GAMEMODE_MAIN_MENU) {
                str = "$ITALTERED SENSORIUM ";
            } else if (g_fingers->mode != FINGERS_MODE_SHOOT) {
                str = "$ITSUFFER FROM SENSATION ";
            } else {
                str =
                    mem_strfmt(
                        &g->frame_arena,
                        "%03d ",
                        g->player ? (int) g->player->stamina : 0);
            }

            strbuf_set(&g->hand_text, str);
        }
    }

    // control hand override color
    {
        if (g->debug.override_particles_black) {
            g->hand_override_color = v4_of(0.0f, 0.0f, 0.0f, 1.0f);
        } else if (g->mode == GAMEMODE_MAIN_MENU) {
            g->hand_override_color = v4_of(0.8f, 0.2f, 0.0f, 1.0f);
        } else {
            g->hand_override_color = v4_of(0);
        }
    }

    // control vignette
    {
        if (g->mode == GAMEMODE_GAME) {
            strbuf_setf(&g->vignette.texts[0], "$IT%s ", "WITHIN HIS HEART OF HEARTS");

            static bool go;
            RELOAD_STATIC_VAR(go);

            if (input_get(g->input, "p") & INPUT_PRESS) {
                go = !go;
            }

            bool any_enemies = false;

            if (g->player->room) {
                DYNLIST(entity_t*) ents = NULL;
                dynlist_init(ents, &g->frame_arena);
                room_get_entities(g->level, g->player->room, &ents);

                dynlist_each(ents, it) {
                    any_enemies |= (*it.el)->ptype->is_enemy;
                }
            }

            bool last_go = go;
            go =
                !g->level->is_hub
                && !any_enemies
                && g->player->room
                && !g->player->room->is_entry;

            // check for transition
            bool transition_vignette = false;
            if (g->transition_effect.strength > 0.0f
                && g->player->in_liquid) {
                transition_vignette = true;
            }

            if (go && !last_go && g->player->room) {
                g->player->room->is_completed = true;

                sector_t *exit =
                    room_find_sector_with_type(
                        g->level,
                        g->player->room,
                        SECTOR_TYPE_EXIT_JUICE);

                if (exit) {
                    exit->diff_trigger_tick = g->tick;
                    exit->diff_rate = 4.0f;
                }
            }

            const stime_t since_damage_s =
                secs_since_tick(g->player->last_damage_tick);

            if (since_damage_s <= 0.33f) {
                const f32 t = invsatf(since_damage_s / 0.33f);
                g->vignette.strength = 1.0f + (1.5f * t);
                g->vignette.tex_mix = 1.0f;
                g->vignette.color = v4_of(1, 0, 0, 0.4);
                g->vignette.tex_color = v4_of(0.8f, 0.3f, 0.1f, 0.8f);
                strbuf_setf(&g->vignette.texts[1], "$ITSUFFER ");
            } else if (transition_vignette) {
                g->vignette.strength = 1.5f;
                g->vignette.tex_mix = 1.0f;
                g->vignette.color = v4_of(1, 0, 0, 0.4);
                g->vignette.tex_color = v4_of(0.8f, 0.3f, 0.1f, 0.8f);
                strbuf_setf(&g->vignette.texts[1], "$ITSUFFER ");
            } else if (go) {
                g->vignette.strength = 1.5f;
                g->vignette.tex_mix = 1.0f;
                g->vignette.color = v4_of(0, 1, 0, 0.4);
                g->vignette.tex_color = v4_of(1);
                strbuf_setf(&g->vignette.texts[1], "$ITEXIT ");
            } else {
                g->vignette.strength = 1.0f;
                g->vignette.tex_mix = 0.0f;
                g->vignette.color = v4_of(v3_of(0), 1.0);
                g->vignette.tex_color = v4_of(v3_of(0.2), 1.0);
                // no need to set texts[1], not visible
            }
        } else {
            g->vignette.color = v4_of(0);
            g->vignette.tex_color = v4_of(0);
        }
    }

    // control sky text
    {
        strbuf_set(&g->sky_text.texts[0], "$ITALTERED SENSORIUM ");
        g->sky_text.mix = 0.0f;
        g->sky_text.alpha = g->mode == GAMEMODE_MAIN_MENU ? 0.2f : 0.1f;

        if (g->mode == GAMEMODE_GAME && g->player) {
            DYNLIST(sector_t*) sectors = NULL;
            dynlist_init(sectors, &g->frame_arena);
            level_sectors_in_radius(g->level, g->player->pos, 3.0f, &sectors);

            dynlist_each(sectors, it) {
                const sector_t *sect = *it.el;
                if (sect->type != SECTOR_TYPE_HUB_JUICE) {
                    continue;
                }

                const v2 pos_edge = sector_project_onto_edge(sect, g->player->pos);
                const f32 dist = v2_distance(pos_edge, g->player->pos);

                if (dist < 3.0f) {
                    const f32 factor = ease_exp_out(invsatf(dist / 3.0f));
                    g->sky_text.mix = 1.0f;
                    g->sky_text.alpha = 0.6f * factor;
                    strbuf_set(
                        &g->sky_text.texts[1],
                        !str_is_empty(sect->approach_name) ?
                            sect->approach_name
                            : "$ITMISSINGNO");
                }
            }
        }
    }

    // control liquid fall effect
    {
        g->liquid_fall.effect = 0.0f;

        if (g->mode == GAMEMODE_GAME && g->player) {
            const sector_t *sect = g->player->sector;
            if (sect
                && g->player->in_liquid
                && (sect->type == SECTOR_TYPE_ENTRY_JUICE
                    || sect->type == SECTOR_TYPE_EXIT_JUICE)
                && g->cam.pos.z < sect->floor.z + sect->liquid_offset) {
                g->liquid_fall.effect = 1.0f;
            }
        }
    }

    // TODO: elsewhere
    // control heartbeat sound
    if (g->mode == GAMEMODE_GAME) {
        if (secs_since_ns(g->last_heartbeat_ns) >= 1.0f) {
            g->last_heartbeat_ns = g->time.total_scaled_ns;
            sound_play("heartbeat");
        }
    }

    sound_update();

    if (g->visopt & VISOPT_AI_VIS) {
        const level_t *l = g->level;

        level_each(entity_t, &g->level->entities, it) {
            const entity_t *e = it.el;

            if (e == g->player) { continue; }

            // draw line showing forward direction (white)
            debug_draw_line(
                &(debug_draw_line_t) {
                    .a = entity_center(l, e),
                    .b =
                        v3_add(
                            entity_center(l, e),
                            e->dir),
                    .frames = 1,
                    .color = v4_of(1.0f),
                });

            // draw line showing desired move direction (red)
            debug_draw_line(
                &(debug_draw_line_t) {
                    .a = entity_center(l, e),
                    .b =
                        v3_add(
                            entity_center(l, e),
                            v3_of(e->walk_dir, 0.0f)),
                    .frames = 1,
                    .color = v4_of(1.0f, 0.2f, 0.2f, 1.0f),
                });

            // draw path, if present
            if (e->path_ticks != 0) {
                rand_t rand = rand_create(e->id);
                const v3 path_color = v3_abs(rand_v3_dir(&rand));

                v2 last = e->pos;
                for (int i = 0; i < e->path.n; i++) {
                    debug_draw_line(
                        &(debug_draw_line_t) {
                            .a =
                                v3_of(
                                    last,
                                    level_point_zs(l, last).z0 + 0.25f),
                            .b =
                                v3_of(
                                    e->path.arr[i].point,
                                    level_point_zs(l, e->path.arr[i].point).z0
                                        + 0.25f),
                            .frames = 1,
                            .color = v4_of(path_color, 1.0f),
                        });
                    last = e->path.arr[i].point;
                }
            }
        }
    }
}

static void fixed_update() {
    const nstime_t start = time_ns();

    level_fixed_update(g->level);

    if (g->mode == GAMEMODE_GAME
        && (input_get(g->input, "m") & INPUT_DOWN)) {
        g->player->health = 0.0f;
    }

    if (g->mode == GAMEMODE_GAME
        && g->player_death_tick != 0
        && secs_since_tick(g->player_death_tick) >= 0.5f
        && (input_get(g->input, "space") & INPUT_DOWN)) {
        const char *errmsg;
        if (!game_set_level(g->level_path, &errmsg)) {
            ERROR("could not set level? %s", errmsg);
        }
    }

    // TODO: use for particle tests
    static v3 pos;
    RELOAD_STATIC_VAR(pos);
    if (input_get(g->input, "-") & INPUT_DOWN) {
        pos = g_editor->cam.pos_3d;
    }

    if (g->mode == GAMEMODE_EDITOR
        && (g->time.fixed.count % 8) == 0
        && !g->debug.pause_particles) {
        const v3 dir = rand_v3_cone(&g->rand, v3_of(0, 0, 1), PI_2);
        particle_new(
            g->level,
            v2_from(pos),
            &(particle_t) {
                .type = PARTICLE_TYPE_BLOOD,
                .duration =
                    (1.0f + rand_f32(&g->rand, -0.3f, 0.3f))
                        * TICKS_PER_SECOND,
                .color = v4_of(1.0f, 0.4f, 0.1f, 1.0f),
                .pos_xyz = pos,
                .vel_xyz =
                    v3_add(
                        v3_scale(dir, 14.0f),
                        v3_of(0, 0, 2.0f)),
                .dir = v3_of(1, 0, 0),
            });
    }

    g->time.fixed.avg_duration_ns =
        g->time.fixed.avg_duration_ns
            * ((FIXED_UPDATES_PER_SECOND - 1) / (f64) FIXED_UPDATES_PER_SECOND)
            + ((time_ns() - start) * (1.0 / (f64) FIXED_UPDATES_PER_SECOND));

    g->time.fixed.count++;
}

static void tick() {
    bump_allocator_reset(&g->frame_arena, 8 * 1024 * 1024);

    const i64 start = time_ns();

    level_tick(g->level);
    particles_tick(g->level);

    if (g->mode == GAMEMODE_GAME) {
        if (false && g->tick % 300 == 0) {
            // try to spawn
            DYNLIST(entity_t*) head_points = NULL;
            dynlist_init(head_points, &g->frame_arena);
            dynlist_push_all_from_dlist(
                level_by_type_node,
                head_points,
                &g->level->entities_by_type[ENTITY_TYPE_HEAD_POINT]);

            const int n = dynlist_size(head_points);
            const int p = prime_random_above(&g->rand, n);
            for (int i = 0, q = p % n;
                 i < dynlist_size(head_points);
                 i++, q = (q + p) % n) {
                const entity_t *head_point = head_points[q];

                const entity_t *nearest =
                    level_nearest_entity_with_type(
                        g->level,
                        head_point->pos_xyz,
                        1.0f,
                        ENTITY_TYPE_HEAD);

                if (nearest) {
                    continue;
                }

                // head here
                entity_t *head =
                    entity_new(
                        g->level,
                        &(entity_t) { .itype = ENTITY_TYPE_HEAD });

                entity_attach_copy(g->level, head, head_point);
                break;
            }
        }

        if (false && !g->level->is_hub && g->tick % 100 == 0) {
            v2 p;

            do {
                p =
                    level_random_point_in_radius(
                        g->level,
                        &g->rand,
                        v2_from(g->cam.pos),
                        24.0f);

                p = level_clamp_point(g->level, p);
            } while (v2_distance(p, v2_from(g->cam.pos)) <= 6.0f);

            const rangef_t zs = level_point_zs(g->level, p);

            if (zs.hi - zs.lo >= 1.5f) {
                entity_type_e type;
                if (rand_chance(&g->rand, 0.05f)) {
                    type = ENTITY_TYPE_MOTHER;
                } else {
                    const entity_type_e types[3] = {
                        ENTITY_TYPE_SCION, ENTITY_TYPE_CRAWLER, ENTITY_TYPE_BIGMOUTH,
                    };
                    type = types[rand_n(&g->rand, 0, 2)];
                }

                entity_new(
                    g->level,
                    &(entity_t) {
                        .itype = type,
                        .pos_xyz = v3_of(p, rangef_lerp(zs, 0.5f)),
                    });
            }
        }
    }

    g->time.tick.avg_duration_ns =
        g->time.tick.avg_duration_ns
            * ((TICKS_PER_SECOND - 1) / (f64) TICKS_PER_SECOND)
            + ((time_ns() - start) * (1.0 / (f64) TICKS_PER_SECOND));

    g->tick++;
    g->time.tick.count++;
}

// draw phantom cursor
static void render_phantom_cursor() {
    const bool clicked = (input_get(g->input, "mouse1") & INPUT_DOWN);
    const model_data_t *data = model_atlas_lookup(
         clicked ?
            "hand$point_grab$0"
            : "hand$point$0");
    const model_index_group_t *fingertip_group =
        model_data_try_get_index_group(data, "tip_pointer");

    g->phantom_color = clicked ? v4_of(v3_of(1), 0.35) : v4_of(v3_of(1), 0.3);

    if (g->mode == GAMEMODE_MAIN_MENU
        && g_main_menu->last_play_transition_s != 0.0f) {
        g->phantom_color.a *=
            ease_exp_inout(
                invsatf(
                    secs_since_s(g_main_menu->last_play_transition_s)
                        / (PLAY_TRANSITION_TIME_S / 2.0f)));
    }

    const f32 aspect = g->target_size.x / (f32) g->target_size.y;
    const v2 cursor_scaled =
        v2_div(
            v2_from_i(g->input->cursor.pos),
            v2_of(g->target_size.x / aspect, g->target_size.y));
    const v2 offset = v2_of(0.12f, 0.02f);
    const v2 yz = v2_add(cursor_scaled, offset);
    const v3 origin_target = v3_of(0.6f, yz.x, yz.y);

    static v3 origin;
    origin = v3_dtlerp(origin, origin_target, 12.0f, g->time.frame.dt_scaled);

    m4 m;
    m = m4_translate_make(origin);
    m = m4_mul(m, m4_translate_make(v3_scale(fingertip_group->centroid, -1)));
    // m = m4_mul(m, m4_translate_make(v3_scale(index_group->centroid, +1)));
    // m = m4_mul(m, m4_rotate_make(sinf(time_s() * 5.0f) * 0.15f, v3_of(0, 0, 1)));
    // m = m4_mul(m, m4_translate_make(v3_scale(index_group->centroid, -1)));
    m = m4_mul(m, m4_scale_make(v3_of(1, -1, 1)));
    *dynlist_push(g_renderer->frame_phantom_models) = (model_t) {
        .transform = m,
        .data = data,
    };
}

static void render() {
    // update palette
    if (g->level) {
        const hash_t level_palette_hash =
            hash_add_v3s(
                0x12345,
                g->level->palette,
                ARRLEN(g->level->palette));

        if (level_palette_hash != g->palettes.last_level_palette_hash) {
            g->palettes.last_level_palette_hash = level_palette_hash;

            palette_load(
                g->palettes.level,
                g->level->palette,
                ARRLEN(g->level->palette));
        }
    }

    passes_update();
    tex_atlas_render();
    model_atlas_update();

    renderer_update_cam();

    if (g->mode == GAMEMODE_MAIN_MENU) {
        main_menu_render();
    }

    if (g->player) {
        fingers_render(g->level, g->player);
    }

    if ((g->mode == GAMEMODE_GAME
            && g_fingers->mode == FINGERS_MODE_EDIT)
        || g->mode == GAMEMODE_MAIN_MENU) {
        render_phantom_cursor();
    } else {
        g->phantom_color = v4_of(0);
    }

    renderer_render();

    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.editor.attach,
            .action = {
                .colors[0] = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = { 0.0f, 0.0f, 0.0f, 0.0f }
                },
                .depth = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = 1000.0f
                }
            },
            .label = "editor.pass",
        });
    sg_push_debug_group("EDITOR");
    {
        sgp_begin(g->window_size.x, g->window_size.y);
        sgp_viewport(0, 0, g->window_size.x, g->window_size.y);
        sgp_project(0.0f, g->window_size.x, g->window_size.y, 0.0f);
        sgp_set_blend_mode(SGP_BLENDMODE_BLEND);

        if (g->mode == GAMEMODE_EDITOR) {
            editor_do_frame();
        }

        sgp_flush();
        sgp_end();
    }
    sg_pop_debug_group();
    sg_end_pass();

    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.overlay.attach,
            .action = {
                .colors[0] = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = { 0.0f, 0.0f, 0.0f, 0.0f }
                },
                .depth = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = 1.0f,
                }
            },
            .label = "overlay.pass",
        });
    sg_push_debug_group("2D");
    {
        sgp_begin(g->target_size.x, g->target_size.y);
        sgp_viewport(0, 0, g->target_size.x, g->target_size.y);
        sgp_project(0.0f, g->target_size.x, g->target_size.y, 0.0f);
        sgp_set_blend_mode(SGP_BLENDMODE_BLEND);

        sgp_push_transform();
        {
            sgp_project(0.0f, g->target_size.x, g->target_size.y, 0.0f);
            render_ui();

            if (g->debug.show_finger_boxes) {
                sgp_set_color(1.0f, 0.3f, 0.3f, 0.8f);
                for (int i = 0; i < 10; i++) {
                    v2 ps[4];
                    box2f_points(g->boxes[i], ps);
                    for (int i = 0; i < 4; i++) {
                        sgp_ext_draw_thick_line(
                            v2_spread(ps[i]),
                            v2_spread(ps[(i + 1) % 4]),
                            3.0f);
                    }
                }
            }
        }
        sgp_pop_transform();

        // particle vis
        sgp_push_transform();
        {
            sgp_project(
                0.0f,
                g->psim->desc.bounds.x,
                g->psim->desc.bounds.y,
                0.0f);

            if (g->debug.show_particles) {
                sgp_set_color(1.0f, 0.3f, 0.3f, 0.8f);
                dynlist_each(g->psim->particles, it) {
                    v2 pos = it.el->pos;
                    pos = v2_add(pos, g->psim->desc.render_offset);
                    pos = v2_add(pos, g->debug.psim_draw_offset);
                    sgp_ext_fill_circle(
                        pos,
                        g->psim->desc.kern_radius / 10.0f);
                }
            }

            if (g->debug.show_hulls) {
                DYNLIST(line2f_t) collision_lines =
                    dynlist_create(line2f_t, &g->frame_arena);
                dynlist_push_all(collision_lines, g->psim->hulls[0]);
                dynlist_push_all(collision_lines, g->psim->hulls[1]);

                dynlist_each(collision_lines, it) {
                    line2f_t line = *it.el;
                    for (int i = 0; i < 2; i++) {
                        line.points[i] =
                            v2_add(
                                line.points[i],
                                g->psim->desc.render_offset);
                        line.points[i] =
                            v2_add(
                                line.points[i],
                                g->debug.psim_draw_offset);

                    }

                    // draw normal line
                    const v2 midpoint = v2_divs(v2_add(line.a, line.b), 2.0f);
                    const v2 n_point =
                        v2_add(
                            midpoint,
                            v2_scale(line_right_normal(line.a, line.b), 0.1f));

                    sgp_set_color(1.0f, 0.3f, 1.0f, 0.8f);
                    sgp_ext_draw_thick_line(
                        v2_spread(midpoint),
                        v2_spread(n_point),
                        0.025f);

                    sgp_set_color(1.0f, 1.0f, 0.3f, 0.8f);
                    sgp_ext_draw_thick_line(
                        v2_spread(line.a),
                        v2_spread(line.b),
                        0.05f);
                }
            }
        }
        sgp_pop_transform();

        sgp_push_transform();
        {
            sgp_project(0.0f, g->target_size.x, g->target_size.y, 0.0f);

            if (g->debug.show_screen_verts) {
                sgp_set_color(0.3f, 1.0f, 0.3f, 0.8f);
                dynlist_each(g->psim->vertices[1], it) {
                    const v3 v_world =
                        v3_from(
                            m4_mulv(
                                g->psim->hand_models[1].transform,
                                v4_of(*it.el, 1.0f)));

                    // transform
                    const v2 v_screen =
                        world_pos_to_clamped_pixel(
                             &g->cam.view_proj,
                             &g->cam.frustum,
                             g->target_size,
                             v_world);
                    const f32 h_size = 1.0f;
                    sgp_draw_filled_rect(
                        v_screen.x - h_size,
                        v_screen.y - h_size,
                        2.0f * h_size,
                        2.0f * h_size);
                }
            }
        }
        sgp_pop_transform();

        sgp_flush();
        sgp_end();
    }
    sg_pop_debug_group();
    sg_end_pass();

    const i64 frame_to_swapchain = time_ns() - g->time.total_ns;
    g->time.frame_to_swapchain_avg_ns =
        (g->time.frame_to_swapchain_avg_ns * (59.0f / 60.0f))
            + ((1.0f / 60.0f) * frame_to_swapchain);

    sg_begin_pass(
        &(sg_pass) {
            .action = {
                .colors[0] = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f },
                },
            },
            .swapchain = platform_swapchain(),
            .label = "default.pass",
        });
    sg_push_debug_group("COMBINE");
    {
        // combine current tints
        v4 tint = v4_of(0);

        dynlist_each(g_renderer->tints, it) {
            f32 a = 1.0f;
            if (it.el->fade) {
                a -=
                    satf(
                        ns_to_secs(g->time.total_ns - it.el->start_abs_ns)
                            / it.el->duration);
            }

            a *= it.el->color.a;

            tint =
                v4_add(
                    v4_scale(it.el->color, a),
                    v4_scale(tint, 1.0f - a));
        }

        const sg_environment env = sg_query_desc().environment;
        const sg_pixel_format dst_color_format = env.defaults.color_format;
        const bool has_depth = env.defaults.depth_format != SG_PIXELFORMAT_NONE;

        screenquad_render(
            dst_color_format,
            has_depth,
            g_passes.post2.color,
            g_renderer->smp_nearest,
            &(screenquad_params_t) { .tint = tint, });
        screenquad_render(
            dst_color_format,
            has_depth,
            g_passes.overlay.color,
            g_renderer->smp_nearest,
            &(screenquad_params_t) { .tint = tint, });
        screenquad_render(
            dst_color_format,
            has_depth,
            g_passes.editor.color,
            g_renderer->smp_nearest,
            &(screenquad_params_t) { 0 });

        simgui_render();
    }
    sg_end_pass();
    sg_commit();

    debug_draw_end_frame();
}

static bool try_do_teleport(const char *dst) {
    const char *errmsg;
    if (!game_set_level(
            mem_strfmt(tlscratch(), "levels/%s.json", dst),
            &errmsg)) {
        ERROR("error: %s", errmsg);
        return false;
    }

    return true;
}

static void frame() {
    const nstime_t now_ns = time_ns();

#ifdef __OBJC__
    @autoreleasepool {
#endif // ifdef __OBJC__

    platform_start_frame();

    platform_set_vsync(g->vsync);

    const v2i old_window_size = g->window_size;
    platform_size(&g->window_size.x, &g->window_size.y);

    if (!v2i_eqv(old_window_size, g->window_size)) {
        set_target_size();
    }

    g->time.total_ns = now_ns;
    g->time.total_s = ns_to_secs(g->time.total_ns);

    // clamp dt to ensure simulation times remain reasonable
    g->time.frame.dt_ns = max(g->time.total_ns - g->time.last_frame_ns, 1);

    g->time_scale =
        dtlerp(
            g->time_scale,
            g->slow_mo_time > 0 ? 0.33f : 1.0f,
            30.0f,
            g->time.frame.dt);

    g->time.frame.dt_scaled_ns = g->time.frame.dt_ns * g->time_scale;
    g->time.frame.dt = ns_to_secs(g->time.frame.dt_ns);
    g->time.frame.dt_scaled = ns_to_secs(g->time.frame.dt_scaled_ns);

    // running average of last 60 frames
    g->time.frame.avg_duration_ns =
        (g->time.frame.avg_duration_ns * (59.0 / 60.0))
            + (g->time.frame.dt_ns * (1.0 / 60.0));

    if (g->time.total_ns - g->time.last_second_ns >= NS_PER_SECOND) {
        g->time.frame.count_per_second = g->time.frame.count_this_second;
        g->time.frame.count_this_second = 0;

        g->time.fixed.count_per_second = g->time.fixed.count_this_second;
        g->time.fixed.count_this_second = 0;

        g->time.tick.count_per_second = g->time.tick.count_this_second;
        g->time.tick.count_this_second = 0;

        g->time.last_second_ns = g->time.total_ns;

        if (g->mode != GAMEMODE_EDITOR) {
            LOG(
                "%d FPS (%.3f ms) / %d FUPS (%.3f ms) / %d TPS (%.3f ms)",
                g->time.frame.count_per_second,
                ns_to_ms(g->time.frame.avg_duration_ns),
                g->time.fixed.count_per_second,
                ns_to_ms(g->time.fixed.avg_duration_ns),
                g->time.tick.count_per_second,
                ns_to_ms(g->time.tick.avg_duration_ns));
        }
    }

    // NOTE: game ticks happen at time scale!
    // there are FEWER TICKS when the game is in slow mo
    const nstime_t ns_per_tick = NS_PER_SECOND / TICKS_PER_SECOND;
    const nstime_t tick_ns =
        g->time.frame.dt_scaled_ns + g->time.tick.remainder_ns;
    g->time.tick.count_this_frame = min(tick_ns / ns_per_tick, TICKS_PER_SECOND);
    g->time.tick.remainder_ns = tick_ns % ns_per_tick;

    // counters are always fixed since ticks represent the same elapsed game
    // time, always
    g->time.tick.dt_ns = NS_PER_SECOND / TICKS_PER_SECOND;
    g->time.tick.dt_scaled_ns = g->time.tick.dt_ns;
    g->time.tick.dt = ns_to_secs(g->time.tick.dt_ns);
    g->time.tick.dt_scaled = ns_to_secs(g->time.tick.dt_scaled_ns);

    // NOTE: fixed updates *DO NOT* happen at time scale!
    // there are THE SAME NUMBER OF FIXED UPDATES when the game is in slow mo
    const nstime_t ns_per_fixed = NS_PER_SECOND / FIXED_UPDATES_PER_SECOND;
    const nstime_t fixed_ns = g->time.frame.dt_ns + g->time.fixed.remainder_ns;
    g->time.fixed.count_this_frame =
        min(fixed_ns / ns_per_fixed, FIXED_UPDATES_PER_SECOND);
    g->time.fixed.remainder_ns = fixed_ns % ns_per_fixed;

    g->time.fixed.dt_ns = NS_PER_SECOND / FIXED_UPDATES_PER_SECOND;
    g->time.fixed.dt_scaled_ns = g->time.fixed.dt_ns * g->time_scale;
    g->time.fixed.dt = ns_to_secs(g->time.fixed.dt_ns);
    g->time.fixed.dt_scaled = ns_to_secs(g->time.fixed.dt_scaled_ns);

    // total elapsed
    g->time.total_scaled_ns += g->time.frame.dt_scaled_ns;
    g->time.total_scaled_s = ns_to_secs(g->time.total_scaled_ns);

    // update remaining slow-mo time
    g->slow_mo_time = max(g->slow_mo_time - g->time.frame.dt, 0.0f);

    simgui_new_frame(
        &(simgui_frame_desc_t) {
            .width = g->window_size.x,
            .height = g->window_size.y,
            .delta_time = g->time.frame.dt,
            .dpi_scale = 1.0f,
        });
    Imgui_ImplSDL_NewFrame();

    // switch levels if requested
    if (g->should_go_to_main_menu) {
        g->should_go_to_main_menu = false;

        const char *errmsg;
        if (!game_go_to_main_menu(&errmsg)) {
            ERROR("error going to main menu: %s", errmsg);
        }
    } else if (strbuf_len(&g->teleport_level) != 0) {
        LOG("attempting teleport to %s", g->teleport_level);

        if (!try_do_teleport(g->teleport_level)) {
            WARN("could not teleport to %s", g->teleport_level);
        }

        strbuf_clear(&g->teleport_level);
    }

    frame_update();

    for (int i = 0; i < g->time.fixed.count_this_frame; i++) {
        fixed_update();
    }
    g->time.fixed.count_this_second += g->time.fixed.count_this_frame;

    for (int i = 0; i < g->time.tick.count_this_frame; i++) {
        tick();
    }
    g->time.tick.count_this_second += g->time.tick.count_this_frame;

    if (g->mode == GAMEMODE_MAIN_MENU) {
        main_menu_update();
    }

    render();

    bump_allocator_reset(&g->frame_arena, 8 * 1024 * 1024);
    bump_allocator_reset(tlscratch(), 2 * 1024 * 1024);

    renderer_do_query_pixels();

    platform_end_frame();

    particle_sim_end_frame(g->psim);
#ifdef __OBJC__
}
#endif // ifdef __OBJC__

    g->time.last_frame_ns = g->time.total_ns;
    g->time.frame.count_this_second++;
    g->time.frame.count++;

    if (!g->vsync && g->max_fps != 0) {
        const nstime_t approx_overhead = secs_to_ns(3.4f / 10000.0f);
        const nstime_t f_dur = time_ns() - now_ns;
        const nstime_t f_target_dur = secs_to_ns(1.0f / (f32) g->max_fps);

        // sleep rest of frame
        if (f_dur + approx_overhead < f_target_dur) {
            struct timespec remaining;
            thrd_sleep(
                &(struct timespec) {
                    .tv_nsec = (f_target_dur - f_dur - approx_overhead),
                },
                &remaining);
        }
    }
}

#ifdef RELOADHOST_CLIENT_ENABLED
// called on RELOADHOST_PRE_RELOAD
static void pre_reload() {
    g->imgui_ctx = igGetCurrentContext();
    hook_call_hooks(HOOK_PRE_RELOAD);

    // TODO: bandaid
    platform_set_relative_mouse_mode(false);
}

// called on RELOADHOST_RELOAD
static void reload() {
    platform_reload();

    sg_reset_state_cache();
    igSetCurrentContext(g->imgui_ctx);

    renderer_reset();

    level_each(entity_t, &g->level->entities, it) {
        it.el->ptype = &ENTITY_TYPES[it.el->itype];
    }

    blocks_reset(g->level);
    sector_matrices_recompute(g->level);

    renderer_set_level(g->level);

    hook_call_hooks(HOOK_POST_RELOAD);
}
#else
void pre_reload() {}
void reload() {}
#endif // ifdef RELOADHOST_CLIENT_ENABLED

int RELOADHOST_ENTRY_NAME(
    int argc,
    char *argv[],
    reloadhost_op_e op,
    reloadhost_t *reloadhost);

int main(int argc, char *argv[]) {
    int res;
    if ((res = RELOADHOST_ENTRY_NAME(argc, argv, RELOADHOST_INIT, NULL))) {
        return res;
    }

    while (true) {
        if ((res = RELOADHOST_ENTRY_NAME(argc, argv, RELOADHOST_STEP, NULL))) {
            if (res == RELOADHOST_CLOSE_REQUESTED) {
                break;
            } else {
                return res;
            }
        }
    }

    return RELOADHOST_ENTRY_NAME(argc, argv, RELOADHOST_DEINIT, NULL);
}

// reloadhost entry point
int RELOADHOST_ENTRY_NAME(
    int argc,
    char *argv[],
    reloadhost_op_e op,
    reloadhost_t *reloadhost) {
    // alloc global state if not present
    if (!g) {
        g = calloc(1, sizeof(*g));

        heap_allocator_init(&g->arena, g_mallocator, &g->stats.g_arena);
    }

    g_reloadhost = reloadhost;

    switch (op) {
    case RELOADHOST_INIT:
        init();
        return 0;
    case RELOADHOST_DEINIT:
        deinit();

        free(g);
        g = NULL;
        return 0;
    case RELOADHOST_RELOAD:
        return 0;
    case RELOADHOST_PRE_RELOAD:
        pre_reload();
        return 0;
    case RELOADHOST_POST_RELOAD:
        reload();
        return 0;
    case RELOADHOST_STEP:
        if (g->quit) {
            return RELOADHOST_CLOSE_REQUESTED;
        }

        frame();
        return 0;
    }

    return 0;
}

// "health" hand
v3 compute_left_hand_particle_color() {
    if (!g->player) { return v3_of(1); }

    v3 col = v3_of(1.0f, 0.3f, 0.3f);

    // flash on health pickup
    const stime_t since_health_s = secs_since_tick(g->player->last_heal);
    col =
        color_lerp_rgb(
            v3_of(2.0f, 0.8f, 0.8f),
            col,
            satf(since_health_s / 0.5f));

    return col;
}

// "stamina" hand
v3 compute_right_hand_particle_color() {
    if (!g->player) { return v3_of(1); }

    v3 col = v3_of(0.9f, 0.1f, 0.1f);

    // flash on dash
    const stime_t since_dash_s = abs_secs_since_ns(g->player->last_dash_abs_ns);
    col =
        color_lerp_rgb(
            v3_of(2.0f),
            col,
            satf(since_dash_s / 0.15f));

    // flash red on shot
    const stime_t since_shot_s = secs_since_tick(g->player->last_shot);
    col =
        color_lerp_rgb(
            v3_of(0.5f, 0.5f, 1.5f),
            col,
            satf(since_shot_s / 0.15f));

    return col;
}
