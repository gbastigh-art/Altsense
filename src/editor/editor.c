#include "editor/editor.h"
#include "editor/ui_misc.h"
#include "gfx/renderer.h"
#include "gfx/tex_atlas.h"
#include "gfx/passes.h"
#include "level/decal.h"
#include "level/level.h"
#include "level/entity.h"
#include "level/portal.h"
#include "level/sector.h"
#include "level/side.h"
#include "level/room.h"
#include "level/io.h"
#include "level/vertex.h"
#include "level/wall.h"
#include "trace.h"
#include "util/file.h"
#include "util/hooks.h"
#include "util/ini.h"
#include "util/input.h"
#include "util/sort.h"
#include "util/time.h"
#include "sound/sound.h"
#include "game.h"
#include "ext/sdl2.h" /* IWYU pragma: keep */

// global editor instance

static editor_t *ed = NULL;
RELOAD_STATIC_GLOBAL(ed)

editor_t *g_editor = NULL;
RELOAD_STATIC_GLOBAL(g_editor)

// forward decls

// EDITOR FUNCTIONS
static void load_settings();
static void save_settings();
static void reset_editor();

// MAP FUNCTIONS
static v2 map_center_for_ptr(lptr_t ptr);

static void map_select(lptr_t ptr);
static void map_deselect(lptr_t ptr);
static void map_clear_select();
static bool map_is_select(lptr_t ptr);

static v2 map_snap_to_grid_for_size(v2 p, f32 grid_size);
static v2 map_snap_to_grid(v2 p);

// un-transform a point p according to map camera (screen to level space)
static v2 screen_point_to_map(v2 p);

// center camera on point
static void map_center_on(v2 p);

// set scale and keep center at the same point
static void map_set_scale(f32 scale);

// locate ID which position is over and its sector
static lptr_t map_locate(v2 pos, sector_t **sector_out);

// get cursor position according to current snapping rules
static v2 map_snapped_cursor_pos(cursor_t *c, lptr_t *hover_out);

static void do_map_frame();

// snap editor map location to cam location
static void map_to_cam();

// snap editor cam location to map location
static void cam_to_map();

// returns true if ptr should visually indicate change
static bool is_ptr_changed(lptr_t ptr);

// cursor modes
static cursor_mode_t CURSOR_MODES[CM_COUNT];

// ui functions
static void show_editor_error(const char *fmt, ...);
static void do_editor_ui();

// see editor.h
static const char *BUTTON_KEYS[BUTTON_COUNT] = {
    [BUTTON_NONE]               = NULL,
    [BUTTON_UP]                 = "up",
    [BUTTON_DOWN]               = "down",
    [BUTTON_LEFT]               = "left",
    [BUTTON_RIGHT]              = "right",
    [BUTTON_EDIT]               = "mouse3",
    [BUTTON_SELECT]             = "mouse1",
    [BUTTON_DESELECT]           = "mouse3",
    [BUTTON_ZOOM_IN]            = "]",
    [BUTTON_ZOOM_OUT]           = "[",
    [BUTTON_SNAP]               = "q",
    [BUTTON_SNAP_WALL]          = "z",
    [BUTTON_MULTI_SELECT]       = "left shift",
    [BUTTON_CANCEL]             = "escape%|left ctrl%+c",
    [BUTTON_MOVE_DRAG]          = "mouse3",
    [BUTTON_SELECT_AREA]        = "left shift",
    [BUTTON_SWITCH_MODE]        = "/%|`",
    [BUTTON_NEW_WALL]           = "w",
    [BUTTON_NEW_SIDE]           = "s",
    [BUTTON_NEW_VERTEX]         = "v",
    [BUTTON_EZPORTAL]           = "e",
    [BUTTON_FIXER]              = "m",
    [BUTTON_DELETE]             = "x",
    [BUTTON_FUSE]               = "f",
    [BUTTON_SPLIT]              = "g",
    [BUTTON_CONNECT]            = "c",
    [BUTTON_PLANE_UP]           = "k",
    [BUTTON_PLANE_DOWN]         = "j",
    [BUTTON_PLANE_SLOPE_UP]     = "left shift%+k",
    [BUTTON_PLANE_SLOPE_DOWN]   = "left shift%+j",
    [BUTTON_SECTOR_UP]          = "i",
    [BUTTON_SECTOR_DOWN]        = "u",
    [BUTTON_BIGADJUST]          = "left shift",
    [BUTTON_SAVE]               = "left meta%+s",
    [BUTTON_NEW_DECAL]          = "l",
    [BUTTON_NEW_ENTITY]         = "o",
    [BUTTON_MOUSEMODE]          = "1",
    [BUTTON_MAP_TO_CAM]         = "2",
    [BUTTON_CAM_TO_MAP]         = "3",
    [BUTTON_CLOSE_ALL]          = "f12",
    [BUTTON_TEX_MOD]            = "t",
    [BUTTON_TEX_MOD_RESET]      = "r",
    [BUTTON_TEX_MOD_OVERRIDE]   = "v",
    [BUTTON_TEX_MOD_SCALE_UP]   = "+",
    [BUTTON_TEX_MOD_SCALE_DOWN] = "-",
    [BUTTON_TEX_MOD_SCALE_X]    = "q",
    [BUTTON_TEX_MOD_SCALE_Y]    = "w",
    [BUTTON_TEX_MOD_SNAP]       = "left shift",
    [BUTTON_DELETE_SECTOR]      = "left ctrl",
    [BUTTON_BOOKMARK]           = "\\",
    [BUTTON_ROOM_MODE]          = "r",
    [BUTTON_NEW_ROOM]           = "n",
};

// defines list of button conflicts, if any
// if a button defines a conflict it means that it takes priority and the other
// button is not triggered at all when the first is down
static int BUTTON_CONFLICTS[BUTTON_COUNT][4] = {
    [BUTTON_SAVE]               = { BUTTON_NEW_SIDE, BUTTON_TEX_MOD },
    [BUTTON_CANCEL]             = { BUTTON_CONNECT },
    [BUTTON_PLANE_SLOPE_UP]     = { BUTTON_PLANE_UP },
    [BUTTON_PLANE_SLOPE_DOWN]   = { BUTTON_PLANE_DOWN },
    [BUTTON_BOOKMARK]           = {
        BUTTON_MOUSEMODE,
        BUTTON_MAP_TO_CAM,
        BUTTON_CAM_TO_MAP
    },
};

void go_to_bookmark(int bookmark) {
    if (!ed->bookmarks[bookmark]) { return; }
    map_center_on(ed->bookmarks[bookmark]->pos);

    f32 z = g->cam.pos.z;
    if (ed->bookmarks[bookmark]->sector) {
        z =
            rangef_lerp(
                sector_point_zs(
                    ed->bookmarks[bookmark]->sector,
                    ed->bookmarks[bookmark]->pos),
                0.5f);
    }

    g->cam.pos = v3_of(ed->bookmarks[bookmark]->pos, z);

    ed->last_bookmark = bookmark;
}

bool try_select_ptr(lptr_t ptr) {
    if (ed->cur.mode == CM_SELECT) {
        ed->cur.mode_select_id.selected = ptr;
        return true;
    }

    return false;
}

void editor_open(lptr_t ptr) {
    if (lptr_is_null(ptr)) {
        return;
    }

    bool already_open = false;

    // check if window is already open
    dynlist_each(ed->open_ptrs, it) {
        if (lptr_eq(*it.el, ptr)) {
            // already open
            already_open = true;
            break;
        }

        if (lptr_is(ptr, LT_SIDE)
            && lptr_is(ptr, LT_WALL)
            && lptr_side(ed->level, ptr)->wall
                == lptr_wall(ed->level, *it.el)) {
            // opening side and wall is already open, just move to front
            already_open = true;
            break;
        }
    }

    if (!already_open) {
        *dynlist_push(ed->to_open_ptrs) = ptr;
    }

    *dynlist_push(ed->front_ptrs) = ptr;
}

static void reset_editor() {
    if (allocator_valid(&ed->arena)) {
        heap_allocator_destroy(&ed->arena);
    }
    *ed = (editor_t) { 0 };
    heap_allocator_init(&ed->arena, &g->arena, NULL);
    ed->map.grid_size = MAP_DEFAULT_GRIDSIZE;
    map_set_scale(1);
    g->visopt |= VISOPT_GRID;

    dynlist_init(ed->map.selected, &ed->arena);
    dynlist_init(ed->open_ptrs, &ed->arena);
    dynlist_init(ed->to_open_ptrs, &ed->arena);
    dynlist_init(ed->front_ptrs, &ed->arena);
    map_init(
        &ed->changed_ptr_to_time,
        &ed->arena,
        sizeof(lptr_t),
        sizeof(nstime_t),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);
}

RELOAD_VISIBLE void destroy_editor(void*) {
    save_settings();
    heap_allocator_destroy(&ed->arena);
}

void editor_init() {
    hook_register(HOOK_EXIT, destroy_editor, NULL);

    ed = mem_calloc(&g->arena, sizeof(*ed));
    g_editor = ed;

    reset_editor();
    load_settings();
}

static void load_settings() {
    ini_t *ini = &g->editor_settings;

    ed->map.grid_size =
        ini_get_f32_or_default(ini, "", "grid_size", MAP_DEFAULT_GRIDSIZE);
    ed->map.scale =
        ini_get_f32_or_default(ini, "", "scale", 1.0f);

    if (ini_scan(ini, "", "pos", "%" SCNv2, PSCNv2(ed->map.pos)) != 2) {
        ed->map.pos = v2_of(0);
    }

    g->visopt = ini_get_int_or_default(ini, "", "visopt", VISOPT_GRID);
    g->gameopt = ini_get_int_or_default(ini, "", "gameopt", 0);

    if (g->mode == GAMEMODE_EDITOR) {
        // if loading settings while editor controls things, then also change
        // level to whatever prefs have

        char level_path[PATH_MAX];
        if (ini_scan(ini, "", "level", "%1023s", level_path) == 1) {
            ed->reload.on = true;
            snprintf(
                ed->reload.level_path,
                sizeof(ed->reload.level_path),
                "%s",
                level_path);
        }
    }

    if (ini_scan(ini, "", "cam_pos", "%" SCNv3, PSCNv3(g->cam.pos)) != 3) {
        g->cam.pos = v3_of(0);
    }

    map_center_on(v2_from(g->cam.pos));

    if (ini_scan(ini, "", "cam_dir", "%" SCNv3, PSCNv3(g->cam.dir)) != 3) {
        g->cam.dir = v3_of(0);
    }
}

static void save_settings() {
    // save base game settings
    game_update_and_dump_settings();

    ini_t *ini = &g->editor_settings;
    ASSERT(g->settings.allocator);
    ASSERT(g->editor_settings.allocator);

    ini_set_f32(ini, "", "grid_size", ed->map.grid_size);
    ini_set_f32(ini, "", "scale", ed->map.scale);
    ini_set_fmt(ini, "", "pos", "%" PRIv2, FMTv2(ed->map.pos));
    ini_set_int(ini, "", "visopt", g->visopt);
    ini_set_int(ini, "", "gameopt", g->gameopt);
    ini_set(ini, "", "level", g->level_path);
    ini_set_fmt(ini, "", "cam_pos", "%" PRIv3, FMTv3(g->cam.pos));
    ini_set_fmt(ini, "", "cam_dir", "%" PRIv3, FMTv3(g->cam.dir));

    const ini_error_e err = ini_dump_to_path(ini, g->path.editor_settings);
    if (err != INI_OK) {
        WARN("error saving editor settings: %s", ini_error_to_str(err));
    }
}

// sort in DESCENDING order
static int cursor_mode_priority_cmp(const void *a, const void *b, void*) {
    return
        (*((cursor_mode_t**) b))->priority
            - (*((cursor_mode_t**) a))->priority;
}

void editor_do_frame() {
    // clear changed pointers
    map_each(lptr_t, nstime_t, &ed->changed_ptr_to_time, it) {
        if (ns_to_secs(g->time.total_ns - *it.value) > 0.066f) {
            map_remove_it(&ed->changed_ptr_to_time, it);
        }
    }

    ImGuiIO *io = igGetIO();
    io->ConfigFlags =
        ((ed->mouse_grab && ed->mode == EDITOR_MODE_CAM)
         || g->input->cursor.grab) ?
            (io->ConfigFlags | ImGuiConfigFlags_NoMouse)
            : (io->ConfigFlags & ~ImGuiConfigFlags_NoMouse);

    // camera control
    if (ed->mode == EDITOR_MODE_CAM && ed->mouse_grab) {
        const f32 speed =
            ((input_get(g->input, "left ctrl") & INPUT_DOWN) ? 3.0f : 1.0f)
                * 6.0f
                * g->time.frame.dt;

        if (g->controls.up.state & INPUT_DOWN) {
            g->cam.pos =
                v3_add(
                    g->cam.pos,
                    v3_scale(
                        g->cam.dir, speed));
        }

        if (g->controls.down.state & INPUT_DOWN) {
            g->cam.pos =
                v3_add(
                    g->cam.pos,
                    v3_scale(
                        g->cam.dir, -speed));
        }

        if (g->controls.right.state & INPUT_DOWN) {
            g->cam.pos =
                v3_add(
                    g->cam.pos,
                    v3_scale(
                        g->cam.right, speed));
        }

        if (g->controls.left.state & INPUT_DOWN) {
            g->cam.pos =
                v3_add(
                    g->cam.pos,
                    v3_scale(
                        g->cam.right, -speed));
        }

        if (g->controls.jump.state & INPUT_DOWN) {
            g->cam.pos =
                v3_add(
                    g->cam.pos,
                    v3_scale(
                        v3_of(0, 0, 1), speed));
        }

        if (input_get(g->input, "left shift") & INPUT_DOWN) {
            g->cam.pos =
                v3_add(
                    g->cam.pos,
                    v3_scale(
                        v3_of(0, 0, 1), -speed));
        }

        g->cam.yaw -= g->input->cursor.motion.x / 50.0f;
        g->cam.pitch += g->input->cursor.motion.y / 50.0f;
    }

    if (!v2_isvalid(ed->map.pos)) {
        ed->map.pos = v2_of(0);
    }

    ed->highlight.alpha = (sin(time_s() * 4.0f) + 1.0f) * 0.5f;
    ed->highlight.ptr = LPTR_NULL;

    ed->level = g->level;

    if (ed->saved_version == -1) {
        ed->saved_version = g->level->version;
    }

    if (!v2i_eqv(ed->size, g->window_size)) {
        const v2 center =
            screen_point_to_map(
                v2_of(
                    ed->size.x / 2.0f,
                    ed->size.y / 2.0f
                ));

        ed->size = g->window_size;
        map_set_scale(ed->map.scale);
        map_center_on(center);
    }

    ed->cur.start_mode = ed->cur.mode;

    // read all button states and prevent conflicts
    for (uint i = BUTTON_NONE + 1; i < BUTTON_COUNT; i++) {
        ed->buttons[i] = input_get(g->input, BUTTON_KEYS[i]);
    }

    // overwrite conflicts
    for (uint i = 0; i < BUTTON_COUNT; i++) {
        const u8 state = ed->buttons[i];

        if (state & (INPUT_PRESS | INPUT_DOWN | INPUT_RELEASE)) {
            for (uint j = 0; j < ARRLEN(BUTTON_CONFLICTS[0]); j++) {
                const uint k = BUTTON_CONFLICTS[i][j];
                if (k == BUTTON_NONE) {
                    break;
                }

                ed->buttons[k] = INPUT_PRESENT;
            }
        }
    }

    if (!io->WantCaptureMouse && !io->WantCaptureKeyboard) {
        if (ed->mode == EDITOR_MODE_CAM
            && (ed->buttons[BUTTON_MOUSEMODE] & INPUT_PRESS)) {
            ed->mouse_grab = !ed->mouse_grab;
            ed->cur.mode = CM_DEFAULT;
        }

        if (ed->buttons[BUTTON_MAP_TO_CAM] & INPUT_PRESS) {
            map_to_cam();
       }

        if (ed->buttons[BUTTON_CAM_TO_MAP] & INPUT_PRESS) {
            cam_to_map();
        }
    }

    // workaround for RMB not focusing on OSX
    if (ed->buttons[BUTTON_DESELECT] & INPUT_PRESS) {
        SDL_RaiseWindow(g->window);
    }

    // complete level reload if requested
    if (ed->reload.on) {
        ed->reload.on = false;

        const char *path = ed->reload.level_path;
        const char *errmsg = NULL;

        if (str_is_empty(path)) {
            errmsg = "path is empty";
        } else if (ed->reload.is_new) {
            // new level
            game_set_level(NULL, &errmsg);
            snprintf(
                g->level_path,
                sizeof(g->level_path),
                "%s",
                ed->reload.level_path);
        } else if (file_isdir(path)) {
            errmsg = "path is directory";
        } else {
            const file_error_e err = file_makebak(path, ".bak");
            if (err == FILE_OK) {
                game_set_level(path, &errmsg);
            } else {
                errmsg =
                    mem_strfmt(
                        tlscratch(),
                        "file error: %s", file_error_to_str(err));
            }
        }

        if (errmsg) {
            show_editor_error("error opening level %s: %s", path, errmsg);
        } else {
            reset_editor();
        }

        ed->level = g->level;
    }

    ed->cur.pos.screen = v2_from_i(g->input->cursor.pos_raw);
    ed->cur.pos.map =
        screen_point_to_map(ed->cur.pos.screen);

    // update bookmarks
    memset(ed->bookmarks, 0, sizeof(ed->bookmarks));
    level_each(entity_t, &ed->level->entities, it) {
        entity_t *ent = it.el;
        if (ent->itype == ENTITY_TYPE_BOOKMARK
            && ent->bookmark_index != 0) {
            if (ed->bookmarks[ent->bookmark_index]) {
                WARN(
                    "conflicting bookmark at %d",
                    ent->bookmark_index);
                ent->bookmark_index = 0;
            } else {
                ed->bookmarks[ent->bookmark_index] = ent;
            }
        }
    }

    // if saved version is 0 (reset or new level), set to current level version
    if (!ed->saved_version) {
        ed->saved_version = ed->level->version;
    }

    ed->ui_has_mouse = io->WantCaptureMouse;
    ed->ui_has_keyboard = io->WantCaptureKeyboard;

    // check global hotkeys
    if (!igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopup)) {
        if (ed->buttons[BUTTON_SAVE] & INPUT_PRESS) {
            const char *errmsg;
            if (!editor_try_save_level(g->level_path, &errmsg)) {
                show_editor_error("error saving level: %s", errmsg);
            }

            save_settings();
        }

        if (ed->buttons[BUTTON_CLOSE_ALL] & INPUT_PRESS) {
            dynlist_resize(ed->open_ptrs, 0);
            ed->ltexts_open = false;
        }
    }

    if (!ed->ui_has_keyboard) {
        // switch editor mode
        if (ed->buttons[BUTTON_SWITCH_MODE] & INPUT_PRESS) {
            ed->mode =
                ed->mode == EDITOR_MODE_MAP ?
                    EDITOR_MODE_CAM
                    : EDITOR_MODE_MAP;
        }

        // cancel operation with CANCEL
        if (ed->buttons[BUTTON_CANCEL] & INPUT_PRESS) {
            map_clear_select();
            ed->cur.mode = CM_DEFAULT;
        }

        if (ed->mode == EDITOR_MODE_MAP) {
            // map controls
            const f32
                base_speed =
                    (ed->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN) ?
                        35.0f : 20.0f,
                speed = (base_speed / ed->map.scale) * g->time.frame.dt;

            if (ed->buttons[BUTTON_LEFT] & INPUT_DOWN) {
                ed->map.pos.x -= speed;
            }

            if (ed->buttons[BUTTON_RIGHT] & INPUT_DOWN) {
                ed->map.pos.x += speed;
            }

            if (ed->buttons[BUTTON_UP] & INPUT_DOWN) {
                ed->map.pos.y += speed;
            }

            if (ed->buttons[BUTTON_DOWN] & INPUT_DOWN) {
                ed->map.pos.y -= speed;
            }

            if (ed->buttons[BUTTON_ZOOM_IN] & INPUT_PRESS) {
                map_set_scale(ed->map.scale * 2.0f);
            }

            if (ed->buttons[BUTTON_ZOOM_OUT] & INPUT_PRESS) {
                map_set_scale(ed->map.scale / 2.0f);
            }
        }

        if (ed->buttons[BUTTON_ROOM_MODE] & INPUT_PRESS) {
            ed->rooms = !ed->rooms;
        }
    }

    // update camera cursor data
    memset(&ed->cam, 0, sizeof(ed->cam));

    if (ed->mode == EDITOR_MODE_CAM && !ed->ui_has_mouse) {
        ed->cam.extra_id_pos = renderer_info_at(g->input->cursor.pos);

        const f32 ex = ed->cam.extra_id_pos.x;
        const u32 id = f32_bits_to_i32(ed->cam.extra_id_pos.y);
        const v2 pos = v2_of(ed->cam.extra_id_pos.z, ed->cam.extra_id_pos.w);

        ed->cam.ptr =
            lptr_from_nogen(
                ed->level,
                (lptr_nogen_t) {
                    .type = id >> 16,
                    .index = id & 0xFFFF,
                });

        ed->cam.pos_3d = v3_of(0);

        switch (lptr_type(ed->cam.ptr)) {
        case LT_SECTOR: {
            const bool is_ceil = f32_bits_to_i32(ex) > 0;
            ed->cam.sect = lptr_sector(ed->level, ed->cam.ptr);
            ed->cam.plane = is_ceil ? PLANE_TYPE_CEIL : PLANE_TYPE_FLOOR;
            ed->cam.sect_pos = pos;

            const sectmat_data_t mat = sector_get_mat(ed->cam.sect);
            ed->cam.texture = mat.texs[is_ceil > 0 ? 1 : 0];
            ed->cam.hsva = mat.hsva[is_ceil > 0 ? 1 : 0];
            ed->cam.pos_3d =
                v3_of(
                    pos,
                    sector_point_zs(ed->cam.sect, pos).zs[is_ceil ? 1 : 0]);
        } break;
        case LT_SIDE:
            ed->cam.side = lptr_side(ed->level, ed->cam.ptr);
            ed->cam.sect = ed->cam.side->sector;
            ed->cam.side_pos = pos;
            ed->cam.texture = (tex_id_t) { f32_bits_to_i32(ex) };
            ed->cam.hsva = ed->cam.side->mat.hsva;

            vertex_t *vs[2];
            side_get_vertices(ed->cam.side, vs);
            ed->cam.pos_3d =
                v3_of(
                    v2_lerp(
                        vs[0]->pos,
                        vs[1]->pos,
                        pos.x / ed->cam.side->wall->len),
                    ed->cam.side->sector->floor.z + pos.y);
            break;
        case LT_DECAL:;
            const decal_t *decal = lptr_decal(ed->level, ed->cam.ptr);
            if (decal->is_on_side) {
                ed->cam.side = decal->side.ptr;
                ed->cam.side_pos = pos;
                ed->cam.sect = decal->side.ptr->sector;

                vertex_t *vs[2];
                side_get_vertices(decal->side.ptr, vs);
                ed->cam.pos_3d =
                    v3_of(
                        v2_lerp(
                            vs[0]->pos,
                            vs[1]->pos,
                            pos.x / decal->side.ptr->wall->len),
                        decal->side.ptr->sector->floor.z + pos.y);
            } else {
                const bool is_ceil = f32_bits_to_i32(ex) > 0;
                ed->cam.sect = decal->sector.ptr;
                ed->cam.plane = is_ceil ? PLANE_TYPE_CEIL : PLANE_TYPE_FLOOR;
                ed->cam.sect_pos = pos;
                ed->cam.pos_3d =
                    v3_of(
                        pos,
                        sector_point_zs(decal->sector.ptr, pos)
                            .zs[is_ceil ? 1 : 0]);
            }
            break;
        case LT_ENTITY:
            ed->cam.sect = lptr_entity(ed->level, ed->cam.ptr)->sector;
            ed->cam.pos_3d = lptr_entity(ed->level, ed->cam.ptr)->pos_xyz;
            break;
        default: break;
        }
    }

    // bookmark key
    static bool did_bookmark = false;

    if (ed->buttons[BUTTON_BOOKMARK] & INPUT_DOWN) {
        for (int i = 1; i <= 9; i++) {
            char button[8];
            snprintf(button, sizeof(button), "%d", i);
            if (input_get(g->input, button) & INPUT_RELEASE) {
                go_to_bookmark(i);
                did_bookmark = true;
            }
        }
    } else if (
        (ed->buttons[BUTTON_BOOKMARK] & INPUT_RELEASE) && !did_bookmark) {
        // cycle to next bookmark
        int i = ed->last_bookmark, n = 0;
        do {
            i = max((i + 1) % EDITOR_MAX_BOOKMARKS, 1);
            n++;
            if (ed->bookmarks[i]) {
                go_to_bookmark(i);
                break;
            }
        } while (i != ed->last_bookmark && n != EDITOR_MAX_BOOKMARKS);
    } else {
        did_bookmark = false;
    }

    // respond to sector up/down, light adjustments
    if (ed->mode == EDITOR_MODE_CAM
        && ed->cam.sect
        && ed->cur.mode == CM_DEFAULT) {
        sector_t *sect = ed->cam.sect;
        plane_t *plane = &sect->planes[ed->cam.plane];

        // do "big" adjustments
        const f32 delta =
            (ed->buttons[BUTTON_BIGADJUST] & INPUT_DOWN) ? 0.125f : (1.0f / 32.0f);

        bool change = true;
        if (plane
            && (ed->buttons[BUTTON_PLANE_SLOPE_UP]
                & INPUT_PRESS)
            && plane->slope_side) {
            plane->slope += PI / 64.0f;
        } else if (plane
            && (ed->buttons[BUTTON_PLANE_SLOPE_DOWN]
                & INPUT_PRESS)
            && plane->slope_side) {
            plane->slope -= PI / 64.0f;
        } else if (plane
            && (ed->buttons[BUTTON_PLANE_UP]
                & INPUT_PRESS)) {
            plane->z += delta;
        } else if (
            plane
            && (ed->buttons[BUTTON_PLANE_DOWN]
                & INPUT_PRESS)) {
            plane->z -= delta;
        } else if (ed->buttons[BUTTON_SECTOR_UP]
                & INPUT_PRESS) {
            sect->floor.z += delta;
            sect->ceil.z += delta;
        } else if (ed->buttons[BUTTON_SECTOR_DOWN]
                & INPUT_PRESS) {
            sect->floor.z -= delta;
            sect->ceil.z -= delta;
        } else {
            change = false;
        }

        if (change) {
            lptr_recalculate(ed->level, lptr_from(sect));
        }
    }

    // update cursor mode
    if (!ed->ui_has_mouse && !ed->ui_has_keyboard) {
        // sort modes by priority
        const cursor_mode_t *cmds[CM_COUNT];
        for (int i = 0; i < CM_COUNT; i++) {
            cmds[i] = &CURSOR_MODES[i];
        }

        sort(
            cmds,
            CM_COUNT,
            sizeof(cursor_mode_t*),
            cursor_mode_priority_cmp,
            NULL);

        for (int i = 0; i < CM_COUNT; i++) {
            const cursor_mode_t *cmd = cmds[i];


            // also update in priority order
            if (cmd == &CURSOR_MODES[ed->cur.mode]) {
                if (cmd->update) { cmd->update(&ed->cur); }
            }

            // check for trigger
            if (!cmd->trigger) {
                // no trigger method
                continue;
            }

            if (CURSOR_MODES[ed->cur.mode].flags & CMF_EXPLICIT_CANCEL) {
                // no triggering others if current mode is explicit cancel
                continue;
            }

            // only trigger in matching mode
            if (ed->mode == EDITOR_MODE_CAM) {
                if (!(cmd->flags & CMF_CAM)) { continue; }
            } else if (ed->mode == EDITOR_MODE_MAP) {
                if (!(cmd->flags & CMF_MAP)) { continue; }
            }

            if (cmd->trigger(&ed->cur)) {
                // run cancel operation if present
                if (CURSOR_MODES[ed->cur.mode].cancel) {
                    CURSOR_MODES[ed->cur.mode].cancel(&ed->cur);
                }

                // switch mode, don't check others
                ed->cur.mode = cmd->mode;
                break;
            }
        }
    }

    // if clicked in 3D camera area, open editor for selected thing
    if (!lptr_is_null(ed->cam.ptr)
        && (ed->cur.mode == CM_DEFAULT
            || !(CURSOR_MODES[ed->cur.mode].flags & CMF_CAM))) {
        const bool
            select = ed->buttons[BUTTON_SELECT] & INPUT_PRESS,
            deselect = ed->buttons[BUTTON_DESELECT] & INPUT_PRESS;

        // try to select something (as in "SELECT ...") but only with select
        // button
        bool selected_ptr = false;
        if (select) {
            selected_ptr = try_select_ptr(ed->cam.ptr);
        }

        // otherwise, open editor for (or focus) clicked element if not being
        // selected. also works for deselect (right click)
        if ((select || deselect) && !selected_ptr) {
            editor_open(ed->cam.ptr);
        }
    }

    do_editor_ui();

    if (ed->mode == EDITOR_MODE_MAP) {
        do_map_frame();
    }
}

bool editor_try_save_level(const char *path, const char **errmsg) {
    path = path ? path : g->level_path;

    file_error_e file_err;

    // only backup if file already exists
    if (file_exists(path)
        && (file_err = file_makebak(path, ".bak")) != FILE_OK) {
        *errmsg =
            mem_strfmt(
                tlscratch(),
                "failed to make level backup: %s",
                file_error_to_str(file_err));

        return false;
    }

    DYNLIST(u8) bytes = dynlist_create(u8, &g->frame_arena);

    const io_error_e io_err = io_save_level(g->level, &bytes);
    if (io_err != IO_OK) {
        *errmsg =
            mem_strfmt(
                tlscratch(),
                "failed to save level: %s",
                io_error_to_str(io_err));

        return false;
    }

    const range_t data = range_from_dynlist(bytes);
    file_err = file_write(path, &data);
    if (file_err != FILE_OK) {
        *errmsg =
            mem_strfmt(
                tlscratch(),
                "failed to write level out: %s",
                file_error_to_str(file_err));

        return false;
    }

    snprintf(g->level_path, sizeof(g->level_path), "%s", path);
    ed->saved_version = ed->level->version;
    return true;
}

void editor_mark_ptr_change(lptr_t ptr) {
    if (map_contains(&ed->changed_ptr_to_time, ptr)) {
        return;
    }

    map_insert(&ed->changed_ptr_to_time, ptr, g->time.total_ns);
}

static v2 map_center_for_ptr(lptr_t ptr) {
    switch (lptr_type(ptr)) {
    case LT_VERTEX:
        return lptr_vertex(ed->level, ptr)->pos;
    case LT_WALL:
        return wall_midpoint(lptr_wall(ed->level, ptr));
    case LT_SIDE:
        return map_center_for_ptr(lptr_from(lptr_side(ed->level, ptr)->wall));
    case LT_SECTOR: {
        sector_t *s = lptr_sector(ed->level, ptr);
        return v2_lerp(s->min, s->max, 0.5f);
    }
    case LT_DECAL:
        return v2_from(decal_worldpos(lptr_decal(ed->level, ptr)));
    case LT_ENTITY:
        return lptr_entity(ed->level, ptr)->pos;
    case LT_ROOM:
        return box2f_center(box2f_from(lptr_room(ed->level, ptr)->bounds));
    }
}

// get draw color for lptr_t
static v4 get_color_for_ptr(
        lptr_t ptr,
        v4 normal,
        v4 select,
        v4 hover) {
    if (lptr_is_null(ptr)) { return v4_of(1); }

    // changed color
    const v4 changed = v4_of(1.0f, 0.6f, 0.03f, 1.0f);

    v4 color = normal;
    const bool highlight = lptr_eq(ptr, ed->highlight.ptr);

    if (is_ptr_changed(ptr)) {
        color = changed;
        goto done;
    } else if (map_is_select(ptr)) {
        color = select;
        goto done;
    } else if (lptr_eq(ptr, ed->cur.hover)) {
        color = hover;
        goto done;
    } else if (
        !lptr_is_null(ed->cur.hover_room)
        && room_contains_lptr(
            ed->level,
            lptr_room(ed->level, ed->cur.hover_room),
            ptr)) {
        color =
            v4_clamp(v4_add(normal, v4_of(v3_of(0.25f), 0.0f)), 0.0f, 1.0f);
        goto done;
    }

    switch (lptr_type(ptr)) {
    case LT_ENTITY: {
        const entity_t *ent = lptr_entity(ed->level, ptr);
        if (!ent->sector && ent->itype != ENTITY_TYPE_BOOKMARK) {
            color = v4_of(0.8f, 0.1f, 0.1f, 1.0f);
        }
    } break;
    case LT_SIDE: {
        side_t *side = lptr_side(ed->level, ptr);
        if (side->sector && is_ptr_changed(lptr_from(side->sector))) {
            color = changed;
        } else if (side->portal) {
            if (side->portal == side_other(side)) {
                color = MAP_SIDE_PORTAL_COLOR;
            } else {
                color = MAP_SIDE_PORTAL_DISCONNECT_COLOR;
            }
        }
    } break;
    case LT_WALL: {
        wall_t *wall = lptr_wall(ed->level, ptr);

        // show vertex connections
        for (int i = 0; i < 2; i++) {
            if (lptr_eq(ed->cur.hover, lptr_from(wall->vertices[i]))
                || lptr_eq(ed->highlight.ptr, lptr_from(wall->vertices[i]))) {
                color = MAP_WALL_CONNECT_COLOR;
            }
        }

        // show connection to side or sector highlight
        for (int i = 0; i < 2; i++) {
            side_t *side = wall->sides[i];
            if (!side) { continue; }

            if (side->sector && is_ptr_changed(lptr_from(side->sector))) {
                color = changed;
            } else if (map_is_select(lptr_from(side))) {
                color = select;
            } else if (
                lptr_eq(ed->cur.hover, lptr_from(side))
                || lptr_eq(ed->highlight.ptr, lptr_from(side))) {
                color = hover;
            } else if (
                side->sector
                && (ed->cur.sector == side->sector
                    || lptr_eq(ed->highlight.ptr, lptr_from(side->sector)))) {
                color =
                    color_scale_rgb(
                        color, 0.5f + 0.5f * ed->highlight.alpha);
            }

            // got color assigned by this side
            break;
        }
    } break;
    case LT_VERTEX:
    case LT_SECTOR:
    case LT_DECAL:
    case LT_ROOM:
        break;
    }

done:
    // even "done" colors must be highlighted
    if (highlight) {
        color = color_scale_rgb(color, 0.5f + 0.5f * ed->highlight.alpha);
    }

    return color;
}

static void set_color_for_ptr(
        lptr_t ptr,
        v4 normal,
        v4 select,
        v4 hover) {
    const v4 color = get_color_for_ptr(ptr, normal, select, hover);
    sgp_set_color(v4_spread(color));
}

static void map_select(lptr_t ptr)  {
    if (lptr_is_null(ptr)) {
        return;
    }

    dynlist_each(ed->map.selected, it) {
        if (lptr_eq(*it.el, ptr)) {
            WARN("not double selecting");
            return;
        }
    }

    *dynlist_push(ed->map.selected) = ptr;
}

static void map_deselect(lptr_t ptr) {
    dynlist_each(ed->map.selected, it) {
        if (lptr_eq(*it.el, ptr)) {
            dynlist_remove_it(ed->map.selected, it);
            break;
        }
    }
}

static void map_clear_select() {
    dynlist_resize(ed->map.selected, 0);
}

static bool map_is_select(lptr_t ptr)  {
    dynlist_each(ed->map.selected, it) {
        if (lptr_eq(*it.el, ptr)) {
            return true;
        }
    }

    return false;
}

static v2 map_snap_to_grid_for_size(v2 p, f32 grid_size) {
    return v2_scale(v2_round(v2_divs(p, grid_size)), grid_size);
}

static v2 map_snap_to_grid(v2 p) {
    return map_snap_to_grid_for_size(p, ed->map.grid_size);
}

static v2 map_transform(v2 p) {
    return v2_of(
        (p.x - ed->map.pos.x) * (ed->map.scale),
        (p.y - ed->map.pos.y) * (ed->map.scale));
}

static v2 screen_point_to_map(v2 p) {
    const f32 ratio = ed->size.x / (f32) ed->size.y;
    const f32 scale = (EDITOR_BASE_SCALE / ed->map.scale);
    return
        v2_of(
            (p.x * (scale / ed->size.x * ratio)) + ed->map.pos.x,
            (p.y * (scale / ed->size.y)) + ed->map.pos.y);
}

static v2 map_point_to_screen(v2 p) {
    const f32 ratio = ed->size.x / (f32) ed->size.y;
    const f32 scale = (EDITOR_BASE_SCALE / ed->map.scale);
    const v2 local = v2_sub(p, ed->map.pos);
    return
        v2_of(
            local.x / (scale / ed->size.x * ratio),
            local.y / (scale / ed->size.y));
}

static void map_center_on(v2 p) {
    const v2
        center = screen_point_to_map(v2_divs(v2_from_i(ed->size), 2.0f)),
        bottom_left = screen_point_to_map(v2_of(0));

    ed->map.pos = v2_sub(p, v2_sub(center, bottom_left));
}

static void map_set_scale(f32 scale) {
    scale = clamp(scale, MAP_SCALE_MIN, MAP_SCALE_MAX);

    // snap to power of two
    int exp;
    frexp(scale, &exp);
    scale = powf(2.0f, exp - 1);

    const v2 center =
        screen_point_to_map(
            v2_of(
                ed->size.x / 2.0f,
                ed->size.y / 2.0f
            ));

    ed->map.scale = scale;
    map_center_on(center);

    // calculate scale params
    ed->map.normal_length = min(0.15f / scale, 0.15f);
    ed->map.vertex_size = clamp(0.15f / scale, 0.1f, 0.20f);
    ed->map.side_select_dist = min(0.2f / scale, 0.2f);
    ed->map.wall_select_dist = min(0.1f / scale, 0.1f);
    ed->map.grid_point_size = min(0.075f / scale, 0.075f);
    ed->map.line_thickness = clamp(0.1f / scale, 0.05f, 0.15f);
}

// NOTE: pretty slow since it doesn't rely on blocks, use sparingly
static lptr_t map_locate(v2 pos, sector_t **sector_out) {
    lptr_t ptr = LPTR_NULL;
    f32 dist = 1e10;
    sector_t *sector = NULL;

    // find sector
    level_each(sector_t, &ed->level->sectors, it) {
        sector_t *sect = it.el;

        if (sector_contains_point(sect, pos)) {
            sector = sect;
            ptr = lptr_from(sect);
            break;
        }
    }

    if (ed->rooms) {
        level_each(room_t, &ed->level->rooms, it) {
            if (box2f_contains(room_min_box(it.el), pos)
                || box2f_contains(room_max_box(it.el), pos)) {
                ptr = lptr_from(it.el);
            }
        }

        if (lptr_matches(ptr, LTF_ROOM) && !lptr_is_null(ptr)) { goto done; }
    }

    // check entities
    level_each(entity_t, &ed->level->entities, it) {
        entity_t *entity = it.el;

        const f32 d = v2_norm(v2_sub(pos, entity->pos));
        if (d < dist && d <= MAP_ENTITY_SIZE) {
            ptr = lptr_from(entity);
            dist = d;
        }
    }

    if (lptr_matches(ptr, LTF_ENTITY) && !lptr_is_null(ptr)) { goto done; }

    // check decals
    level_each(decal_t, &ed->level->decals, it) {
        const v2 p = v2_from(decal_worldpos(it.el));
        const f32 d = v2_norm(v2_sub(pos, p));
        if (d <= dist && d <= MAP_DECAL_SIZE) {
            ptr = lptr_from(it.el);
            dist = d;
        }
    }

    if (lptr_matches(ptr, LTF_DECAL) && !lptr_is_null(ptr)) { goto done; }

    // check vertices
    level_each(vertex_t, &ed->level->vertices, it) {
        vertex_t *v = it.el;

        const f32 d = v2_norm(v2_sub(pos, v->pos));
        if (d <= dist && d <= (ed->map.vertex_size / 2.0f)) {
            ptr = lptr_from(v);
            dist = d;
        }
    }

    if (lptr_matches(ptr, LTF_VERTEX) && !lptr_is_null(ptr)) { goto done; }

    // check walls
    level_each(wall_t, &ed->level->walls, it) {
        wall_t *wall = it.el;

        const f32 d =
            point_to_segment(
                pos,
                wall->v0->pos,
                wall->v1->pos);

        const int side =
            sign(
                point_side(
                    pos,
                    wall->v0->pos,
                    wall->v1->pos));

        if (d <= dist && d < ed->map.side_select_dist) {
            // check if directly on side normal
            const v2
                midpoint = wall_midpoint(wall),
                normal = v2_scale(
                    wall->normal,
                    side * ed->map.normal_length),
                npoint = v2_add(midpoint, normal),
                left = v2_of(-normal.y, normal.x),
                phleft = v2_scale(left, 0.5f * ed->map.line_thickness),
                nhleft = v2_scale(phleft, -1.0f);

            const box2f_t box =
                box2f_scale_center(
                    box2f_sort(
                        box2f_mm(
                            v2_add(midpoint, nhleft),
                            v2_add(npoint, phleft))),
                    v2_of(2.0f, 2.0f));

            side_t *s = wall->sides[side < 0 ? 0 : 1];
            if (s && box2f_contains(box, pos)) {
                ptr = lptr_from(s);
                dist = 0.0f;
            }

            // select wall if close, otherwise select side
            if (d < ed->map.wall_select_dist) {
                ptr = lptr_from(wall);
                dist = d;
            } else if (side < 0 && wall->sides[0]) {
                ptr = lptr_from(wall->sides[0]);
                dist = d;
            } else if (side >= 0 && wall->sides[1]) {
                ptr = lptr_from(wall->sides[1]);
                dist = d;
            }
        }
    }

    if (lptr_matches(ptr, LTF_SIDE | LTF_WALL) && !lptr_is_null(ptr)) { goto done; }

done:
    if (sector_out) { *sector_out = sector; }
    return ptr;
}

// draw sector filled with color
// if floor is enablcolor is layered on top of floor
static void sector_draw_filled(
        sector_t *sect,
        v4 color) {
    // check if sector is on screen at all
    const v2
        pmin = map_transform(sect->min),
        pmax = map_transform(sect->max);

    if (pmax.x < 0
        || pmax.y < 0
        || pmin.x >= ed->size.x
        || pmin.y >= ed->size.x) {
        return;
    }

    sgp_set_color(v4_spread(color));
    dynlist_each(sect->tris, it) {
        sgp_draw_filled_triangle(
            v2_spread(it.el->a->pos),
            v2_spread(it.el->b->pos),
            v2_spread(it.el->c->pos));
    }
}

// VISOPT_GRID
static void map_visopt_grid() {
    f32 gs = ed->map.grid_size;

    if (ed->map.scale <= 0.25f) {
        gs = max(gs, 0.5f);
    } else if (ed->map.scale <= 0.25f) {
        gs = max(gs, 0.25f);
    }

    v2
        s = map_snap_to_grid_for_size(v2_sub(ed->map.pos, v2_of(gs)), gs),
        bounds = screen_point_to_map(v2_from_i(ed->size));

    #define MAX_GRID_LINES 32768
    while (
        roundf((bounds.x - s.x) / gs) * roundf((bounds.y - s.y) / gs)
            >= MAX_GRID_LINES) {
        gs *= 2.0f;
        s =
            map_snap_to_grid_for_size(
                v2_sub(ed->map.pos, v2_of(gs)), gs),
        bounds = screen_point_to_map(v2_from_i(ed->size));
    }

// #define USE_POINT_GRID

#ifdef USE_POINT_GRID
    DYNLIST(sgp_rect) points = NULL, whole_points = NULL;
    dynlist_init(points, &g->frame_arena, MAX_GRID_LINES / 2);
    dynlist_init(whole_points, &g->frame_arena, MAX_GRID_LINES / 4);

    const f32 eps = (1.0f / 16.0f) / max(ed->map.scale, 1.0f);

    int n = 0;
    for (
        f32 y = s.y;
        n < MAX_GRID_LINES && y <= bounds.y;
        y += gs) {
        if (y < 0) { continue; }

        for (
            f32 x = s.x;
            n < MAX_GRID_LINES && x <= bounds.x;
            x += gs) {
            if (x < 0) { continue; }

            if (fract(x) < 0.001f && fract(y) < 0.01f) {
                *dynlist_push(whole_points) =
                    (sgp_rect) { x, y, 1.2f * eps, 1.2f * eps };
            } else {
                *dynlist_push(points) =
                    (sgp_rect) { x, y, 1.0f * eps, 1.0f * eps };
            }

            n++;
        }
    }

    sgp_set_color(v4_spread(MAP_GRID_COLOR));
    sgp_draw_filled_rects(points, dynlist_size(points));

    sgp_set_color(v4_spread(MAP_WHOLE_GRID_COLOR));
    sgp_draw_filled_rects(whole_points, dynlist_size(whole_points));

#else
    DYNLIST(sgp_line) *out = NULL, lines = NULL, whole_lines = NULL;

    dynlist_init(lines, &g->frame_arena, MAX_GRID_LINES / 2);
    dynlist_init(whole_lines, &g->frame_arena, MAX_GRID_LINES / 2);

    int n = 0;
    for (
        f32 y = s.y;
        n < MAX_GRID_LINES && y <= bounds.y;
        y += gs) {
        if (y < 0) { continue; }

        out = fract(y) < 0.01f ? &whole_lines : &lines;
        *dynlist_push(*out) = (sgp_line) {
            .a = { max(s.x, 0), y },
            .b = { max(s.x + (ed->size.x / ed->map.scale), 0), y },
        };
        n++;

        for (
            f32 x = s.x;
            n < MAX_GRID_LINES && x <= bounds.x;
            x += gs) {
            if (x < 0) { continue; }

            out = fract(x) < 0.01f ? &whole_lines : &lines;
            *dynlist_push(*out) = (sgp_line) {
                .a = { x, max(s.y, 0) },
                .b = { x, max(s.y + (ed->size.y / ed->map.scale), 0) },
            };
            n++;
        }
    }

    sgp_set_color(v4_spread(MAP_GRID_COLOR));
    sgp_draw_lines(lines, dynlist_size(lines));

    sgp_set_color(v4_spread(MAP_WHOLE_GRID_COLOR));

    if (ed->map.scale >= 1.0f) {
        sgp_ext_draw_thick_lines(
            whole_lines,
            dynlist_size(whole_lines),
            ed->map.line_thickness * 0.25f);
    } else {
        sgp_draw_lines(whole_lines, dynlist_size(whole_lines));
    }
#endif // ifdef USE_POINT_GRID

    // boundaries
    {
        const f32 t = 0.075f;
        sgp_set_color(1.0f, 0.2f, 0.2f, 0.5f);
        sgp_ext_draw_thick_line(0.0f, 0.0f, MAX_COORD, 0.0f, t);
        sgp_ext_draw_thick_line(MAX_COORD, 0.0f, MAX_COORD, MAX_COORD, t);
        sgp_ext_draw_thick_line(MAX_COORD, MAX_COORD, 0.0f, MAX_COORD, t);
        sgp_ext_draw_thick_line(0.0f, MAX_COORD, 0.0f, 0.0f, t);
    }
}

// VISOPT_HOVERSECT
static void map_visopt_hoversect() {
    if (!ed->cur.sector) { return; }

    sector_draw_filled(ed->cur.sector, MAP_SECTOR_HIGHLIGHT);
}

// VISOPT_GEO
static void map_visopt_geo() {
    level_each(sector_t, &ed->level->sectors, it) {
        sector_t *s = it.el;

        if ((g->visopt & VISOPT_HOVERSECT)
            && s != ed->cur.sector) {
            continue;
        }

        sgp_set_color(0.3f, 1.0f, 0.3f, 0.25f);
        dynlist_each(s->tris, it) {
            sgp_ext_draw_thick_line(
                v2_spread(it.el->a->pos), v2_spread(it.el->b->pos),
                ed->map.line_thickness * 0.5f);
            sgp_ext_draw_thick_line(
                v2_spread(it.el->b->pos), v2_spread(it.el->c->pos),
                ed->map.line_thickness * 0.5f);
            sgp_ext_draw_thick_line(
                v2_spread(it.el->c->pos), v2_spread(it.el->a->pos),
                ed->map.line_thickness * 0.5f);
        }

        llist_each(sector_node, &s->subs, it) {
            dynlist_each(it.el->lines, it_l) {
                sgp_set_color(0.6f, 0.7f, 1.0f, 0.5f);
                sgp_ext_draw_thick_line(
                    v2_spread(it_l.el->a->pos), v2_spread(it_l.el->b->pos),
                    ed->map.line_thickness * 0.5f);
            }

            sgp_set_color(0.5f, 0.5f, 0.5f, 0.7f);
            sgp_ext_draw_box2f(
                box2f_mm(it.el->min, it.el->max),
                ed->map.line_thickness * 0.5f);
        }
    }
}

// VISOPT_SUBNEIGHBORS
static void map_visopt_subneighbors() {
    if (!ed->cur.sector) { return; }

    subsector_t *sub =
        sector_find_subsector(
            ed->cur.sector,
            ed->cur.pos.map);


    dynlist_each(sub->neighbors, it) {
        dynlist_each(it.el->sub->lines, it_l) {
            if (sect_line_eq(it_l.el, it.el->line)) {
                // draw line vert a as yellow, b as pink

                const f32 radius = ed->map.vertex_size / 2.0f;

                sgp_set_color(1.0f, 1.0f, 0.0f, 1.0f);
                sgp_draw_filled_rect(
                    v2_spread(v2_sub(it.el->line->a->pos, v2_of(radius))),
                    v2_spread(v2_of(2 * radius)));

                sgp_set_color(1.0f, 0.0f, 1.0f, 1.0f);
                sgp_draw_filled_rect(
                    v2_spread(v2_sub(it.el->line->b->pos, v2_of(radius))),
                    v2_spread(v2_of(2 * radius)));

                sgp_set_color(1.0f, 1.0f, 1.0f, 1.0f);
            } else if (it.el->is_portal) {
                sgp_set_color(0.1f, 0.2f, 1.0f, 0.5f);
            } else {
                sgp_set_color(0.8f, 0.8f, 0.2f, 0.5f);
            }

            sgp_ext_draw_thick_line(
                v2_spread(it_l.el->a->pos), v2_spread(it_l.el->b->pos),
                ed->map.line_thickness * 0.5f);
        }
    }
}

// VISOPT_PVS / VISOPT_REACHABLE / VISOPT_NEAR / VISOPT_EVS
static void map_visopt_sector_matrix(
    const sector_matrix_t *matrix,
    v4 color) {
    if (!ed->cur.sector) { return; }

    sgp_set_color(v4_spread(color));

    DYNLIST(sector_t*) visible =
        dynlist_create(sector_t*, &g->frame_arena);

    sector_matrix_get(
        ed->level,
        matrix,
        ed->cur.sector,
        &visible,
        SECTOR_MATRIX_NO_FLAGS);

    dynlist_each(visible, it) {
        llist_each(sector_sides, &(*it.el)->sides, it) {
            sgp_ext_draw_thick_line(
                v2_spread(it.el->wall->v0->pos),
                v2_spread(it.el->wall->v1->pos),
                ed->map.line_thickness);
        }
    }
}

// VISOPT_SHOWSECT
// show each sector in a different highlight color
static void map_visopt_showsect() {
    const box2f_t screen_bounds =
        box2f_mm(
            screen_point_to_map(v2_of(0)),
            screen_point_to_map(v2_from_i(g->window_size)));

    level_each(sector_t, &ed->level->sectors, it) {
        sector_t *sect = it.el;

        if (!box2f_collides(
                screen_bounds,
                box2f_mm(sect->min, sect->max))) {
            continue;
        }

        const u32 color =
            0x50000000
            | (lptr_rand_abgr(lptr_from(sect)) & 0xFFFFFF);

        sector_draw_filled(
            sect,
            get_color_for_ptr(
                lptr_from(sect),
                V4_FROM_ABGR(color),
                MAP_SECTOR_SELECT,
                MAP_SECTOR_HIGHLIGHT));
    }
}

// VISOPT_BLOCKS
// show blocks, their contents
static void map_visopt_blocks() {
    const v2
        pmin = screen_point_to_map(v2_of(0)),
        pmax = screen_point_to_map(v2_from_i(ed->size));

    const v2i
        boffset = ed->level->blocks.offset,
        bsize = ed->level->blocks.size,
        bmin = v2i_of(pmin.x / BLOCK_SIZE, pmin.y / BLOCK_SIZE),
        bmax = v2i_of(pmax.x / BLOCK_SIZE, pmax.y / BLOCK_SIZE),
        bmcursor =
            v2i_of(
                ed->cur.pos.map.x / BLOCK_SIZE,
                ed->cur.pos.map.y / BLOCK_SIZE);

    for (int by = bmin.y; by <= bmax.y; by++) {
        for (int bx = bmin.x; bx <= bmax.x; bx++) {
            // only draw block if it exists and contains some walls
            if (bx < boffset.x
                || by < boffset.y
                || bx >= boffset.x + bsize.x
                || by >= boffset.y + bsize.y) {
                continue;
            }

            block_t *block =
                &ed->level->blocks.arr[
                    (by - boffset.y) * bsize.x + (bx - boffset.x)];

            const bool curbox = bx == bmcursor.x && by == bmcursor.y;

            sgp_set_color(v4_spread(V4_FROM_ABGR(curbox ? 0x80A0FFFF : 0x40A0A0A0)));
            sgp_ext_draw_box2f(
                box2f_mm(
                    v2_of((bx + 0) * BLOCK_SIZE, (by + 0) * BLOCK_SIZE),
                    v2_of((bx + 1) * BLOCK_SIZE, (by + 1) * BLOCK_SIZE)),
                ed->map.line_thickness * 0.5f);

            if (!curbox) {
                continue;
            }

            // show which walls are a part of block
            dynlist_each(block->walls, it) {
                wall_t *wall = *it.el;

                sgp_set_color(v4_spread(V4_FROM_ABGR(0xFFFFFF80)));
                sgp_ext_draw_thick_line(
                    v2_spread(wall->v0->pos),
                    v2_spread(wall->v1->pos),
                    ed->map.line_thickness);
            }

            // show subsectors
            dynlist_each(block->subsectors, it) {
                sgp_set_color(1.0f, 0.2f, 0.2f, 0.4f);

                subsector_t *sub =
                    blklist_ptr(
                        subsector_t,
                        &ed->level->subsectors,
                        *it.el);
                dynlist_each(sub->lines, it_l) {
                    sgp_ext_draw_thick_line(
                        v2_spread(it_l.el->a->pos),
                        v2_spread(it_l.el->b->pos),
                        ed->map.line_thickness);
                }
            }

            // show entities which are part of block
            dynlist_each(block->entities, it) {
                entity_t *ent = *it.el;
                sgp_set_color(v4_spread(V4_FROM_ABGR(0xFF80FF80)));
                sgp_draw_filled_rect(
                    v2_spread(v2_sub(ent->pos, v2_of(MAP_ENTITY_SIZE / 2))),
                    v2_spread(v2_of(MAP_ENTITY_SIZE)));
            }
        }
    }
}

// VISOPT_TRACE
static void map_visopt_trace() {
    if (!lptr_is(ed->cur.hover, LT_SIDE)) { return; }
    side_t *side = lptr_side(ed->level, ed->cur.hover);

    DYNLIST(side_t*) trace = dynlist_create(side_t*, &g->frame_arena);
    level_trace_sides(ed->level, side, &trace, NULL);

    sgp_set_color(0.5f, 0.5f, 1.0f, 0.5f);
    dynlist_each(trace, it) {
        const v2 mid = wall_midpoint((*it.el)->wall);

        const f32 r = 0.125f;
        sgp_draw_filled_rect(mid.x - r, mid.y - r, r * 2, r * 2);
    }
}

// VISOPT_SHOW_ID
static void map_visopt_show_id() {
    if (!lptr_is_valid(ed->level, ed->cur.hover)) {
        return;
    }

    const v2
        center_map = map_center_for_ptr(ed->cur.hover),
        center_screen = map_point_to_screen(center_map);

    igSetNextWindowPos(
        (ImVec2) { center_screen.x, g->window_size.y - center_screen.y - 1 },
        0,
        (ImVec2) { 0.5f, 0.5f });
    igBeginTooltip();
    igText("%d (gen %d)", ed->cur.hover.id, ed->cur.hover.handle.gen);
    igEndTooltip();
}

static f32 editor_tmp_path_cost(
    level_t *l,
    const subsector_t *a,
    const subsector_t *b,
    const sect_line_t *via,
    void*) {
    f32 base = v2_distance(a->center, b->center);

    if (via->side
        && via->side->portal
        && via->side->portal->portal) {
        if (!side_get_segments(l, via->side).middle.present
            || !side_get_segments(l, via->side->portal).middle.present) {
            return -1.0f;
        }

        const side_t *entry =
            via->side->subsector == a ? via->side : via->side->portal;

        const f32 step_height = 0.5f;
        const f32 z_diff =
            portal_relative_z(
                l,
                entry,
                entry->portal,
                wall_midpoint(via->side->wall)); // TODO: bad for slopes

        // cannot traverse if we can't move across the threshold
        if (fabsf(z_diff) > step_height) {
            return -1.0f;
        }
    }

    // check if we can pass through the line
    if (v2_distance(via->a->pos, via->b->pos) <= 0.5f) {
        return -1.0f;
    }

    return base;
}

static trace_resolve_result_e editor_tmp_path_trivial__resolve(
        level_t *l,
        trace_2d_seg_t *trace,
        const trace_hit_t *hit) {
    // non-portals are immediate stop
    if (!hit->side.ptr->portal) {
        return TRACE_RESOLVE_STOP;
    }

    const f32 cost =
        editor_tmp_path_cost(
            l,
            hit->side.ptr->subsector,
            hit->side.ptr->portal->subsector,
            hit->side.ptr->sect_line,
            trace->userdata);

    return cost < 0.0f ? TRACE_RESOLVE_STOP : TRACE_RESOLVE_CONTINUE;
}

static bool editor_tmp_path_trivial(
        level_t *l,
        const path_point_t *a,
        const path_point_t *b,
        const path_point_t *c,
        void *userdata) {
    trace_2d_seg_t trace = {
        .org = a->point,
        .dst = c->point,
        .radius = 0.5f,
        .types = LTF_SIDE,
        .flags = TRACE_FLAG_DISCONNECTED_PORTALS_STOP,
        .resolve_fn = editor_tmp_path_trivial__resolve,
        .userdata = userdata,
    };

    const trace_2d_seg_result_t result = trace_2d_seg(l, &trace);
    return result.result == TRACE_RESULT_NOT_STOPPED;
}

static void do_map_frame() {
    // all are ID_NONE if there is no hover allowed for current cursor mode
    if (CURSOR_MODES[ed->cur.mode].flags & CMF_NOHOVER) {
        ed->cur.hover = LPTR_NULL;
        ed->cur.hover_room = LPTR_NULL;
        ed->cur.sector = NULL;
    } else {
        ed->cur.hover = map_locate(ed->cur.pos.map, &ed->cur.sector);
        ed->cur.hover_room =
            lptr_matches(ed->cur.hover, LTF_ROOM) ? ed->cur.hover : LPTR_NULL;
    }

    sgp_set_blend_mode(SGP_BLENDMODE_BLEND);

    sgp_viewport(0, 0, ed->size.x, ed->size.y);
    sgp_project(
        0.0f,
        (EDITOR_BASE_SCALE / ed->map.scale) * (ed->size.x / (f32) ed->size.y),
        (EDITOR_BASE_SCALE / ed->map.scale),
        0.0f);

    sgp_set_color(0.0f, 0.0f, 0.0f, 1.0f);
    sgp_clear();

    sgp_push_transform();
    sgp_translate(v2_spread(v2_scale(ed->map.pos, -1)));

    if (g->visopt & VISOPT_GRID) {
        map_visopt_grid();
    }

    if (g->visopt & VISOPT_HOVERSECT) {
        map_visopt_hoversect();
    }

    // changed sectors get highlighted
    map_each(lptr_t, nstime_t, &ed->changed_ptr_to_time, it) {
        sector_t *sect;
        if ((sect = lptr_sector(ed->level, *it.key))) {
            sector_draw_filled(
                sect,
                v4_of(
                    v3_from(MAP_CHANGED_COLOR),
                    0.10f));
        }
    }

    const box2f_t screen_bounds =
        box2f_mm(
            screen_point_to_map(v2_of(0)),
            screen_point_to_map(v2_from_i(g->window_size)));

    // draw walls
    level_each(wall_t, &ed->level->walls, it) {
        wall_t *wall = it.el;

        if (!box2f_vs_line(
                screen_bounds,
                wall->v0->pos,
                wall->v1->pos)) {
            continue;
        }

        // primary wall line
        set_color_for_ptr(
            lptr_from(wall),
            MAP_WALL_COLOR,
            MAP_SELECT_COLOR,
            MAP_WALL_HOVER_COLOR);
        sgp_ext_draw_thick_line(
            v2_spread(wall->v0->pos),
            v2_spread(wall->v1->pos),
            ed->map.line_thickness);

        // draw side normal lines
        const v2 midpoint = wall_midpoint(wall);

        for (int i = 0; i < 2; i++) {
            side_t *s = wall->sides[i];
            if (!s) { continue; }

            const int sign = i == 0 ? 1 : -1;

            set_color_for_ptr(
                lptr_from(s),
                MAP_SIDE_NORMAL_COLOR,
                MAP_SIDE_SELECT_COLOR,
                MAP_SIDE_HOVER_COLOR);

            const v2 endpoint =
                v2_add(
                    midpoint,
                    v2_scale(
                        wall->normal,
                        sign * ed->map.normal_length));

            sgp_ext_draw_thick_line(
                v2_spread(midpoint),
                v2_spread(endpoint),
                ed->map.line_thickness);
        }
    }

    // draw vertices
    level_each(vertex_t, &ed->level->vertices, it) {
        if (!box2f_contains(screen_bounds, it.el->pos)) { continue; }

        const f32 radius = ed->map.vertex_size / 2.0f;

        set_color_for_ptr(
            lptr_from(it.el),
            MAP_VERTEX_COLOR,
            MAP_SELECT_COLOR,
            MAP_VERTEX_HOVER_COLOR);

        sgp_ext_fill_circle(it.el->pos, radius);
    }

    // TODO: bounds culling
    // draw decals
    level_each(decal_t, &ed->level->decals, it) {
        decal_t *decal = it.el;

        const v2 pos = v2_from(decal_worldpos(decal));
        v2 normal, endpoint;

        if (decal->is_on_side) {
            normal = side_normal(decal->side.ptr),
            endpoint = v2_add(pos, v2_scale(normal, MAP_DECAL_SIZE));
        } else {
            normal = pos;
            endpoint = pos;
        }

        set_color_for_ptr(
            lptr_from(decal),
            MAP_DECAL_COLOR,
            MAP_SELECT_COLOR,
            MAP_DECAL_HOVER_COLOR);

        sgp_draw_filled_rect(
            v2_spread(v2_sub(pos, v2_of(MAP_DECAL_SIZE / 2))),
            v2_spread(v2_of(MAP_DECAL_SIZE)));

        sgp_set_color(v4_spread(v4_of(1.0f)));
        sgp_ext_draw_thick_line(
            v2_spread(pos), v2_spread(endpoint), ed->map.line_thickness * 0.75f);
    }

    // TODO: bounds culling
    // draw entities
    level_each(entity_t, &ed->level->entities, it) {
        entity_t *ent = it.el;

        // base color is random from entity name
        // (so same entities have same color)
        u64 hash = 0x1235;
        hash = hash_add_str(hash, entity_type_to_str(ent->itype));
        rand_t r = rand_create(hash);

        v4 base_color =
            v4_of(
                v3_abs(rand_v3_dir(&r)),
                0.8f);

        if (ent->itype == ENTITY_TYPE_BOOKMARK) {
            base_color = v4_of(1.0f, 1.0f, 0.0f, 0.8f);
        }

        set_color_for_ptr(
            lptr_from(ent),
            base_color,
            v4_add(base_color, v4_of(v3_of(0.5f), 0.0f)),
            MAP_ENTITY_HOVER_COLOR);

        const v2 bl = v2_sub(ent->pos, v2_of(MAP_ENTITY_SIZE / 2));
        sgp_draw_filled_rect(
            v2_spread(bl),
            v2_spread(v2_of(MAP_ENTITY_SIZE)));

        if (ent->itype == ENTITY_TYPE_BOOKMARK) {
            // draw bookmarks as a bookmark shape
            sgp_draw_filled_triangle(
                v2_spread(bl),
                bl.x + (MAP_ENTITY_SIZE / 2), bl.y,
                bl.x, bl.y - (MAP_ENTITY_SIZE / 2));
            sgp_draw_filled_triangle(
                bl.x + (MAP_ENTITY_SIZE / 2), bl.y,
                bl.x + MAP_ENTITY_SIZE, bl.y,
                bl.x + MAP_ENTITY_SIZE, bl.y - (MAP_ENTITY_SIZE / 2));
        }

        if (lptr_eq(lptr_from(ent), ed->cur.hover)) {
            sgp_set_color(0.8f, 0.2f, 0.8f, 0.5f);
            sgp_ext_draw_thick_circle(
                ent->pos,
                entity_radius(ed->level, ent),
                ed->map.line_thickness * 0.5f);
        }
    }

    // draw camera pos in 2D
    sgp_set_color(0.2f, 1.0f, 0.2f, 0.5f);
    sgp_ext_draw_filled_box2f(
        box2f_center_on(box2f_m(v2_of(0.25f)), v2_from(g->cam.pos)));

    // draw connecting sectors for non-contiguous portals
    if (lptr_matches(ed->cur.hover, LTF_SIDE)) {
        side_t *side = lptr_side(ed->level, ed->cur.hover);
        if (side->is_disconnect) {
            // draw connection
            sgp_set_color(v4_spread(MAP_SIDE_PORTAL_DISCONNECT_COLOR));
            sgp_ext_draw_thick_line(
                v2_spread(side_normal_point(side)),
                v2_spread(side_normal_point(side->portal)),
                ed->map.line_thickness * 0.5f);
        }
    }

    if (g->visopt & VISOPT_GEO) {
        map_visopt_geo();
    }

    if (g->visopt & VISOPT_SUBNEIGHBORS) {
        map_visopt_subneighbors();
    }

    if ((g->visopt & VISOPT_PVS)) {
        map_visopt_sector_matrix(
            &ed->level->matrices.pvs,
            v4_of(0.2f, 0.6f, 1.0f, 0.4f));
    }

    if ((g->visopt & VISOPT_REACHABLE)) {
        map_visopt_sector_matrix(
            &ed->level->matrices.reachable,
            v4_of(0.8f, 0.8f, 0.1f, 0.4f));
    }

    if ((g->visopt & VISOPT_NEAR)) {
        map_visopt_sector_matrix(
            &ed->level->matrices.near,
            v4_of(0.1f, 0.8f, 0.1f, 0.4f));
    }

    if ((g->visopt & VISOPT_EVS)) {
        map_visopt_sector_matrix(
            &ed->level->matrices.evs,
            v4_of(0.8f, 0.1f, 0.8f, 0.4f));
    }

    if (g->visopt & VISOPT_SHOWSECT) {
        map_visopt_showsect();
    }

    if (g->visopt & VISOPT_SHOW_ID) {
        map_visopt_show_id();
    }

    // show rooms
    if (ed->rooms) {
        level_each(room_t, &ed->level->rooms, it) {
            const v4 color =
                get_color_for_ptr(
                    lptr_from(it.el),
                    v4_of(0.7f, 0.7f, 0.1f, 1.0f),
                    v4_of(1.0f, 1.0f, 0.6f, 1.0f),
                    v4_of(1.0f, 1.0f, 0.3f, 1.0f));

            // draw area
            sgp_set_color(v3_spread(color), 0.15f);
            const box2f_t area = box2f_from(it.el->bounds);
            sgp_ext_draw_filled_box2f(
                box2f_mm(
                    v2_add(area.min, v2_of(0.25f)),
                    v2_sub(area.max, v2_of(0.25f))));

            // draw min/max boxes
            sgp_set_color(v3_spread(color), 0.85f);
            sgp_ext_draw_filled_box2f(room_min_box(it.el));
            sgp_ext_draw_filled_box2f(room_max_box(it.el));
        }
    }

    if (g->visopt & VISOPT_BLOCKS) {
        map_visopt_blocks();
    }

    if (g->visopt & VISOPT_TRACE) {
        map_visopt_trace();
    }

    if (false) {
        DYNLIST(subsector_path_point_t) path =
            dynlist_create(subsector_path_point_t, &g->frame_arena);
        if (path_subsectors_to_goal(
                ed->level,
                ed->cur.pos.map,
                v2_from(g->cam.pos),
                NULL,
                &path,
                NULL)) {
            for (int i = 0, n = dynlist_size(path); i < n - 1; i++) {
                subsector_t *a = path[i].sub, *b = path[i + 1].sub;
                sgp_set_color(1.0f, 0.5f, 0.8f, 0.5f);
                sgp_ext_draw_thick_line(
                    v2_spread(a->center),
                    v2_spread(b->center),
                    ed->map.line_thickness);

            }
        }
    }

    if (false) {
        DYNLIST(path_point_t) path =
            dynlist_create(path_point_t, &g->frame_arena);
        if (path_to_goal(
                ed->level,
                ed->cur.pos.map,
                v2_from(g->cam.pos),
                NULL,
                NULL,
                &path,
                NULL,
                PATH_TO_GOAL_DISCONNECT_PROTRUDE)) {
            for (int i = 0, n = dynlist_size(path); i < n - 1; i++) {
                sgp_set_color(1.0f, 0.5f, 0.8f, 0.5f);
                sgp_ext_draw_thick_line(
                    v2_spread(path[i].point),
                    v2_spread(path[i + 1].point),
                    ed->map.line_thickness);

            }

            for (int i = 0, n = dynlist_size(path); i < n; i++) {
                if (path[i].is_on_disconnect) {
                    sgp_set_color(1.0f, 0.3f, 1.0f, 0.5f);
                } else {
                    sgp_set_color(0.3f, 1.0f, 0.3f, 0.5f);
                }

                sgp_ext_draw_thick_circle(
                    path[i].point,
                    0.1f,
                    ed->map.line_thickness * 0.5f);
            }
        }
    }

    if (false) {
        static DYNLIST(line3f_t) sightline;
        RELOAD_STATIC_VAR(sightline);
        if (!sightline) {
            sightline = dynlist_create(line3f_t, &g->arena);
        }

        if (input_get(g->input, "q") & INPUT_PRESS) {
            // compute sightline
            const rangef_t zs =
                level_point_zs(ed->level, ed->cur.pos.map);
            trace_sightline_3d(
                ed->level,
                g->cam.pos,
                v3_of(
                    ed->cur.pos.map,
                    zs.z0 + 1.0f),
                32.0f,
                &sightline);
        }

        dynlist_each(sightline, it) {
            sgp_set_color(1.0f, 0.5f, 0.8f, 0.5f);
            sgp_ext_draw_thick_line(
                v2_spread(it.el->a),
                v2_spread(it.el->b),
                ed->map.line_thickness);
        }

/*         if (g->time.frame.count % 1000 == 0) { */
/*             LOG("aah"); */
/*             dynlist_each(sightline, it) { */
/*                 debug_draw_line( */
/*                     &(debug_draw_line_t) { */
/*                         .a = it.el->a, */
/*                         .b = it.el->b, */
/*                         .color = v4_of(1.0f, 0.2f, 0.2f, 1.0f), */
/*                         .frames = 20000, */
/*                     }); */
/*             } */
/*         } */
    }

    if (!ed->ui_has_mouse && !ed->ui_has_keyboard) {
        const u8
            bs_select = ed->buttons[BUTTON_SELECT],
            bs_deselect = ed->buttons[BUTTON_DESELECT],
            bs_multi = ed->buttons[BUTTON_MULTI_SELECT],
            bs_edit = ed->buttons[BUTTON_EDIT],
            bs_cancel = ed->buttons[BUTTON_CANCEL];

        if (bs_select & INPUT_PRESS) {
            if (map_is_select(ed->cur.hover)) {
                map_deselect(ed->cur.hover);
            } else {
                map_select(ed->cur.hover);
            }
        }

        // click to edit
        // open on RELEASE so as not to conflict with MOVE_DRAG
        if (((ed->cur.mode == CM_DEFAULT
             && (ed->cur.start_mode != CM_MOVE_DRAG
                 || !ed->cur.mode_move_drag.moved))
             || (ed->cur.mode == CM_MOVE_DRAG
                 && !ed->cur.mode_move_drag.moved))
            && !lptr_is_null(ed->cur.hover)
            && (bs_edit & INPUT_RELEASE)) {
            editor_open(ed->cur.hover);
        }

        // multi-select for walls, vertices
        if (ed->cur.mode == CM_DEFAULT
            && lptr_matches(ed->cur.hover, LTF_WALL | LTF_VERTEX)
            && (bs_multi & INPUT_DOWN)) {

            if (bs_select & INPUT_PRESS) {
                map_select(ed->cur.hover);
            }

            if (bs_deselect & INPUT_PRESS) {
                map_deselect(ed->cur.hover);
            }
        }

        // cancel operation with CANCEL
        if (bs_cancel & INPUT_PRESS) {
            ed->rooms = false;

            map_clear_select();
            ed->cur.mode = CM_DEFAULT;
        }

        // if selected without hover AND click to cancel, cancel
        if ((CURSOR_MODES[ed->cur.mode].flags & CMF_CLICK_CANCEL)
            && lptr_is_null(ed->cur.hover)
            && (bs_select & INPUT_PRESS)
            && !(ed->buttons[BUTTON_SELECT_AREA] & INPUT_DOWN)) {
            map_clear_select();
            ed->cur.mode = CM_DEFAULT;
        }
    }

    // draw cursor mode
    const cursor_mode_t *cmd = &CURSOR_MODES[ed->cur.mode];
    if (cmd->render) { cmd->render(&ed->cur); }

    sgp_pop_transform();
}

static v2 map_snapped_cursor_pos(cursor_t *c, lptr_t *hover_out) {
    wall_t *snap_wall = NULL;

    v2 pos = c->pos.map;

    if (lptr_matches(c->hover, LTF_WALL)) {
        snap_wall = lptr_wall(ed->level, c->hover);
    } else if (ed->buttons[BUTTON_SNAP_WALL] & INPUT_DOWN) {
        // look for nearby walls
        side_t *side = level_nearest_side(ed->level, c->pos.map);
        if (side) { snap_wall = side->wall; }
    }

    if (ed->buttons[BUTTON_SNAP] & INPUT_DOWN) {
        pos = map_snap_to_grid(pos);
    }

    if (snap_wall) {
        pos =
            point_project_segment(
                pos,
                snap_wall->v0->pos,
                snap_wall->v1->pos);
    }

    if (hover_out) {
        *hover_out = map_locate(pos, NULL);
    }

    return pos;
}

static void map_to_cam() {
    map_center_on(v2_from(g->cam.pos));
}

static void cam_to_map() {
    const v2 pos =
        screen_point_to_map(v2_from_i(v2i_divs(ed->size, 2)));

    g->cam.pos =
        v3_of(
            pos,
            rangef_lerp(level_point_zs(ed->level, pos), 0.5f));
}

static bool is_ptr_changed(lptr_t ptr) {
    return map_contains(&ed->changed_ptr_to_time, ptr);
}

static const char *cm_default_status_line(cursor_t *cur) {
    return mem_strdup(&g->frame_arena, "NORMAL");
}

static const char *cm_select_id_status_line(cursor_t *cur) {
    return mem_strfmt(
        &g->frame_arena,
        "SELECT %s",
        level_types_to_str(ed->cur.mode_select_id.types, &g->frame_arena));
}

static void cm_select_id_update(cursor_t *cur) {
    // try to select hovered item when select is pressed
    if (!lptr_is_null(cur->mode_select_id.selected)) {
        cur->mode_select_id.selected = LPTR_NULL;
    }

    if (ed->buttons[BUTTON_SELECT] & INPUT_RELEASE) {
        map_clear_select();
        cur->mode_select_id.selected =
            ed->mode == EDITOR_MODE_MAP ? cur->hover : ed->cam.ptr;
    }
}

static void decal_placement_pos(
        v2 pos,
        side_t **pside,
        f32 *px,
        sector_t **psector,
        v2 *ppos,
        v2 *pworld) {
    // check if we are "on" a side
    DYNLIST(wall_t*) near_walls = dynlist_create(wall_t*, &g->frame_arena);
    level_walls_in_radius(
        ed->level,
        pos,
        MAP_DECAL_SIZE * 0.8f,
        &near_walls);

    side_t *side = NULL;
    f32 side_dist = 1e10;

    dynlist_each(near_walls, it) {
        wall_t *wall = *it.el;
        const v2 wall_to_pos = v2_sub(pos, wall_midpoint(wall));

        for (int i = 0; i < 2; i++) {
            if (!wall->sides[i]) { continue; }

            // check that pos is on this side of wall
            if (v2_dot(
                    side_normal(wall->sides[i]), wall_to_pos) < 0.0f) {
                continue;
            }

            const f32 dist =
                point_to_segment(pos, wall->v0->pos, wall->v1->pos);

            if (!side || dist < side_dist) {
                side = wall->sides[i];
                side_dist = dist;
            }
        }
    }

    if (side) {
        vertex_t *vs[2];
        side_get_vertices(side, vs);

        const f32 t =
            clamp(
                point_project_line_t(pos, vs[0]->pos, vs[1]->pos),
                0, 1);
        *pside = side;
        *px = t * side->wall->len;
        *pworld = v2_lerp(vs[0]->pos, vs[1]->pos, t);
    } else {
        sector_t *sector = NULL;
        if ((sector = level_find_point_sector(ed->level, pos, NULL))) {
            *psector = sector;
            *ppos = pos;
            *pworld = pos;
        }
    }

    dynlist_destroy(near_walls);
}

static bool cm_drag_trigger(cursor_t *cur) {
    if (CURSOR_MODES[cur->mode].flags & CMF_NODRAG) {
        return false;
    }

     if (!lptr_matches(cur->hover,  LTF_SECTOR)
        && (ed->buttons[BUTTON_SELECT] & INPUT_PRESS)) {
        cur->mode_drag.start = cur->pos.map;
        cur->mode_drag.start_map = (map_t) { 0 };
        return true;
    }

    return false;
}

static void cm_drag_update(cursor_t *cur) {
    // check if this is a room resize
    if (!map_valid(&cur->mode_drag.start_map)
        && lptr_matches(cur->hover, LTF_ROOM)
        && box2f_contains(
            room_max_box(lptr_room(ed->level, cur->hover)),
            cur->pos.map)) {
        cur->mode = CM_RESIZE_ROOM;
        cur->mode_resize_room.room = cur->hover;
        return;
    }

    const v2
        p = cur->pos.map,
        delta = v2_sub(p, cur->mode_drag.start);

    // create map if not already created
    if (!map_valid(&cur->mode_drag.start_map)) {
        map_init(
            &cur->mode_drag.start_map,
            &g->arena,
            sizeof(lptr_t),
            sizeof(v2),
            map_hash_bytes,
            map_cmp_bytes,
            NULL, NULL, NULL);
    }

    if (ed->buttons[BUTTON_SELECT] & INPUT_RELEASE) {
        // stop dragging
        cur->mode = CM_DEFAULT;

        map_destroy(&cur->mode_drag.start_map);

        // deselect UNLESS area drag is down
        if (!(ed->buttons[BUTTON_SELECT_AREA] & INPUT_DOWN)) {
            map_clear_select();
        }
    } else if (ed->buttons[BUTTON_SELECT] & INPUT_DOWN) {
        // accumulate list of things (vertices, entities, decals) to move
        DYNLIST(lptr_t) to_move = dynlist_create(lptr_t, &g->frame_arena);

        dynlist_each(ed->map.selected, it) {
            if (!lptr_is_valid(ed->level, *it.el)) { continue; }

            switch (lptr_type(*it.el)) {
            case LT_VERTEX:
            case LT_DECAL:
            case LT_ROOM:
            case LT_ENTITY: {
                *dynlist_push(to_move) = *it.el;
            } break;
            case LT_WALL: {
                wall_t *w = lptr_wall(ed->level, *it.el);
                *dynlist_push(to_move) = lptr_from(w->v0);
                *dynlist_push(to_move) = lptr_from(w->v1);
            } break;
            default: break;
            }
        }

        // only move each thing in list onced, don't move marked
        dynlist_each(to_move, it) {
            level_fields_t *level_fields = lptr_level_fields(ed->level, *it.el);
            if (level_fields->lflags.mark) { continue; }

            // lookup by pointer
            v2 *start = map_getp(v2, &cur->mode_drag.start_map, it.el);

            if (!start) {
                start = map_insert(&cur->mode_drag.start_map, *it.el, v2_of(0));

                switch (lptr_type_flag(*it.el)) {
                case LTF_VERTEX:
                    *start = lptr_vertex(ed->level, *it.el)->pos;
                    break;
                case LTF_DECAL:
                    *start =
                        v2_from(
                            decal_worldpos(lptr_decal(ed->level, *it.el)));
                    break;
                case LTF_ENTITY:
                    *start = lptr_entity(ed->level, *it.el)->pos;
                    break;
                case LTF_ROOM:
                    *start =
                        v2_from_i(lptr_room(ed->level, *it.el)->bounds.min);
                    break;
                default: ASSERT(false, "bad drag type %d", lptr_type_flag(*it.el));
                }
            }

            const v2
                raw_pos = v2_add(*start, delta),
                pos =
                    (ed->buttons[BUTTON_SNAP] & INPUT_DOWN) ?
                        map_snap_to_grid(raw_pos)
                        : raw_pos;

            // mark as moved
            level_fields->lflags.mark = true;
            switch (lptr_type_flag(*it.el)) {
            case LTF_VERTEX:
                lptr_vertex(ed->level, *it.el)->pos =
                    v2_maxv(pos, v2_of(0));
                break;
            case LTF_DECAL: {
                decal_t *decal = lptr_decal(ed->level, *it.el);

                side_t *side = NULL;
                f32 x;
                sector_t *sector = NULL;
                v2 sect_pos, world_pos;
                decal_placement_pos(
                    pos, &side, &x, &sector, &sect_pos, &world_pos);

                if (side) {
                    decal_set_side(ed->level, decal, side);
                    decal->side.offsets = v2_of(x, decal->side.offsets.y);
                } else if (sector) {
                    decal_set_sector(
                        ed->level, decal, sector, decal->sector.plane);
                    decal->sector.pos = world_pos;
                }
            } break;
            case LTF_ENTITY: {
                entity_try_move(ed->level, lptr_entity(ed->level, *it.el), pos);
            } break;
            case LTF_ROOM: {
                room_t *room = lptr_room(ed->level, *it.el);
                const v2i size = box2i_size(room->bounds);
                room->bounds = box2i_ps(v2i_from_v(pos), size);
            } break;
            default: ASSERT(false);
            }

            lptr_recalculate(ed->level, *it.el);
        }

        // clear marks
        dynlist_each(to_move, it) {
            lptr_level_fields(ed->level, *it.el)->lflags.mark = false;
        }

        dynlist_destroy(to_move);
    }
}

static const char *cm_drag_status_line(cursor_t *cur) {
    strbuf_t buf = strbuf_create(&g->frame_arena);
    strbuf_ap_fmt(&buf, "DRAG");

    dynlist_each(ed->map.selected, it) {
        strbuf_ap_fmt(
            &buf,
            "%s%s",
            it.i == 0 ? " " : ", ",
            lptr_to_str(ed->level, *it.el, &g->frame_arena));
    }

    return buf;
}

static void cm_drag_cancel(cursor_t *cur) {
    if (map_valid(&cur->mode_drag.start_map)) {
        map_destroy(&cur->mode_drag.start_map);
    }
}

static bool cm_resize_room_trigger(cursor_t *cur) {
    // only triggered by cm_drag
    return false;
}

static void cm_resize_room_update(cursor_t *cur) {
    room_t *room = lptr_room(ed->level, cur->mode_resize_room.room);

    if (!room) {
        cur->mode = CM_DEFAULT;
        return;
    } else if (ed->buttons[BUTTON_SELECT] & INPUT_RELEASE) {
        cur->mode = CM_DEFAULT;
        if (!(ed->buttons[BUTTON_SELECT_AREA] & INPUT_DOWN)) {
            map_clear_select();
        }
    } else if (ed->buttons[BUTTON_SELECT] & INPUT_DOWN) {
        room->bounds.max =
            v2i_of(
                max((int) cur->pos.map.x, room->bounds.min.x),
                max((int) cur->pos.map.y, room->bounds.min.y));
    }
}

static const char *cm_resize_room_status_line(cursor_t *cur) {
    return
        mem_strfmt(
            &g->frame_arena,
            "RESIZE ROOM %d",
            cur->mode_resize_room.room.id);
}


static bool cm_move_drag_trigger(cursor_t *cur) {
    if (cur->mode == CM_DEFAULT
        && (lptr_is_null(cur->hover) || lptr_matches(cur->hover, LTF_SECTOR))
        && (ed->buttons[BUTTON_MOVE_DRAG] & INPUT_PRESS)) {
        cur->mode_move_drag.last = cur->pos.screen;
        cur->mode_move_drag.moved = false;
        return true;
    }

    return false;
}

static void cm_move_drag_update(cursor_t *cur) {
    const u8 bs = ed->buttons[BUTTON_MOVE_DRAG];

    if (bs & INPUT_RELEASE) {
        cur->mode = CM_DEFAULT;
    } else if (bs & INPUT_DOWN) {
        const v2 delta =
            v2_sub(
                screen_point_to_map(cur->pos.screen),
                screen_point_to_map(cur->mode_move_drag.last));

        if (delta.x != 0 || delta.y != 0) {
            cur->mode_move_drag.moved = true;
        }

        ed->map.pos = v2_sub(ed->map.pos, delta);
        cur->mode_move_drag.last = cur->pos.screen;
    }
}

static const char *cm_move_drag_status_line(cursor_t *cur) {
    return "MOVE DRAG";
}

static bool cm_select_area_trigger(cursor_t *cur) {
    if (cur->mode == CM_DEFAULT
        && (lptr_is_null(cur->hover) || lptr_matches(cur->hover, LTF_SECTOR))
        && (ed->buttons[BUTTON_SELECT_AREA] & INPUT_DOWN)
        && (ed->buttons[BUTTON_SELECT] & INPUT_DOWN)) {
        cur->mode_select_area.start = cur->pos.map;
        cur->mode_select_area.end = cur->mode_select_area.start;
        return true;
    }

    return false;
}

static void cm_select_area_update(cursor_t *cur) {
    cur->mode_select_area.end = cur->pos.map;

    // append to selection list
    DYNLIST(lptr_t) ptrs = dynlist_create(lptr_t, &g->frame_arena);
    level_ptrs_in_area(
        ed->level,
        box2f_mm(cur->mode_select_area.start, cur->mode_select_area.end),
        LTF_VERTEX | LTF_WALL | LTF_SECTOR | LTF_ENTITY | LTF_DECAL
            | (ed->rooms ? LTF_ROOM : 0),
        &ptrs,
        LPIA_WHOLE_WALL | LPIA_WHOLE_SECT);

    if (!(ed->buttons[BUTTON_SELECT_AREA] & INPUT_DOWN)
        || !(ed->buttons[BUTTON_SELECT] & INPUT_DOWN)) {
        // select everything in area
        cur->mode = CM_DEFAULT;

        dynlist_each(ptrs, it) {
            map_select(*it.el);
        }
    }
}

static void cm_select_area_render(cursor_t *cur) {
    // render box
    sgp_set_color(1.0f, 1.0f, 1.0f, 0.3f);

    sgp_ext_draw_filled_box2f(
        box2f_sort(
            box2f_mm(
                cur->mode_select_area.start,
                cur->mode_select_area.end)));
}

static const char *cm_select_area_status_line(cursor_t *cur) {
    return
        mem_strfmt(
            &g->frame_arena,
            "SELECT (%f, %f) -> (%f, %f)",
            cur->mode_select_area.start.x,
            cur->mode_select_area.start.y,
            cur->mode_select_area.end.x,
            cur->mode_select_area.end.y);
}

static bool cm_wall_trigger(cursor_t *cur) {
    if (cur->mode == CM_DEFAULT
        && (ed->buttons[BUTTON_NEW_WALL]
                & INPUT_PRESS)) {
        cur->mode_wall.started = false;
        return true;
    }

    return false;
}

static void cm_wall_update(cursor_t *cur) {
    // stop after deselect
    if (ed->buttons[BUTTON_DESELECT] & INPUT_RELEASE) {
        cur->mode = CM_DEFAULT;
        return;
    }

    if (!(ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        return;
    }

    if (!cur->mode_wall.started) {
        cur->mode_wall.started = true;
        cur->mode_wall.start =
            map_snapped_cursor_pos(cur, &cur->mode_wall.start_ptr);
        return;
    }

    lptr_t endptr;
    v2 endpos = map_snapped_cursor_pos(cur, &endptr);

    // get or create two vertices from start to here
    const v2 ps[2] = { cur->mode_wall.start, endpos };
    lptr_t ptrs[2] = { cur->mode_wall.start_ptr, endptr };

    // pointers should either be vertex, wall, or nothing
    for (int i = 0; i < 2; i++) {
        switch (lptr_type_flag(ptrs[i])) {
        case LTF_VERTEX:
        case LTF_WALL:
            break;
        case LTF_SIDE:
            ptrs[i] = lptr_from(lptr_side(ed->level, ptrs[i])->wall);
            break;
        default:
            ptrs[i] = LPTR_NULL;
            break;
        }
    }

    vertex_t *vs[2] = { NULL, NULL };
    bool on_border[2] = { false, false };

    // get or create two end vertices
    for (int i = 0; i < 2; i++) {
        if (lptr_is_null(ptrs[i])) {
            vs[i] = vertex_new(ed->level, ps[i]);
            on_border[i] = false;
        } else {
            switch (lptr_type_flag(ptrs[i])) {
            case LTF_VERTEX: {
                vs[i] = lptr_vertex(ed->level, ptrs[i]);
                on_border[i] = true;
            } break;
            case LTF_WALL: {
                vs[i] =
                    wall_split(
                        ed->level,
                        lptr_wall(ed->level, ptrs[i]),
                        ps[i])->v0;
                on_border[i] = true;
            } break;
            default: ASSERT(false);
            }
        }
    }

    sector_t
        *share_sect = vertices_shared_sector(ed->level, vs[0], vs[1]),
        *inside_sect = NULL,
        *containing_sects[2] = {
            on_border[0] ?
                NULL : level_find_point_sector(ed->level, vs[0]->pos, NULL),
            on_border[1] ?
                NULL : level_find_point_sector(ed->level, vs[1]->pos, NULL),
        };

    // try to find sector which wall is being created inside
    // don't use vertices which were on existing borders (walls or vertices
    // themselves)
    for (int i = 0; i < 2 && !inside_sect; i++) {
        if (!on_border[i]) {
            inside_sect =
                level_find_point_sector(ed->level, vs[i]->pos, NULL);
        }
    }

    if (share_sect) { share_sect->lflags.do_not_recalc = true; }
    if (inside_sect) { inside_sect->lflags.do_not_recalc = true; }

    // make a wall between the two vertices
    wall_t *new_wall = wall_new(ed->level, vs[0], vs[1]);

    LOG("handle is %d/%d", new_wall->id, new_wall->handle.gen);
    ASSERT(genlist_present(&ed->level->walls, new_wall->handle));

    // find near side on vs[0] (where wall creation started)
    side_t *near_side = level_find_near_side(ed->level, vs[0], share_sect);

    // if there is a shared or inside sector, create a sides on both sides
    // otherwise only create a side on the right.
    // shared sectors also portal to each other
    for (int i = 0; i < 2; i++) {
        if (!share_sect && !inside_sect && i == 1) {
            // check that there is actually a sector here if we're creating the
            // opposite wall
            const v2 midpoint = wall_midpoint(new_wall);
            if (!level_find_point_sector(
                    ed->level,
                    v2_sub(midpoint, v2_sign(new_wall->normal)),
                    NULL)) {
                continue;
            }
        }

        side_t *new_side = side_new(ed->level, near_side);
        wall_set_side(ed->level, new_wall, i, new_side);
    }

    // if
    // - there is a shared sector
    // - OR both vertices are inside (contained in) the same sector
    // then portal sides to each other
    if (((containing_sects[0] && containing_sects[0] == containing_sects[1])
            || share_sect)
            && new_wall->sides[0]
            && new_wall->sides[1]) {
        new_wall->sides[0]->portal = new_wall->sides[1];
        new_wall->sides[1]->portal = new_wall->sides[0];
        side_recalculate(ed->level, new_wall->sides[1]);
        side_recalculate(ed->level, new_wall->sides[0]);
    }

    for (int i = 0; i < 2; i++) {
        if (new_wall->sides[i]) {
            level_update_side_sector(ed->level, new_wall->sides[i]);
        }
    }

    if (share_sect) {
        share_sect->lflags.do_not_recalc = false;
        sector_recalculate(ed->level, share_sect);
    }

    if (inside_sect) {
        inside_sect->lflags.do_not_recalc = false;
        sector_recalculate(ed->level, inside_sect);
    }

    // restart with wall on current vertex if shift pressed
    if (ed->buttons[BUTTON_MULTI_SELECT]
            & INPUT_DOWN) {
        cur->mode_wall.started = true;
        cur->mode_wall.start = endpos;
        cur->mode_wall.start_ptr = lptr_from(vs[1]);
    } else {
        cur->mode_wall.started = false;
    }
}

static void cm_wall_render(cursor_t *cur) {
    sgp_set_color(v4_spread(MAP_NEW_WALL_COLOR));
    const v2 cursor_pos = map_snapped_cursor_pos(cur, NULL);

    if (cur->mode_wall.started) {
        // vertex
        sgp_draw_filled_rect(
            v2_spread(
                v2_sub(
                    cur->mode_wall.start,
                    v2_of(ed->map.vertex_size / 2.0f))),
            v2_spread(v2_of(ed->map.vertex_size)));

        // wall
        sgp_ext_draw_thick_line(
            v2_spread(cur->mode_wall.start),
            v2_spread(cursor_pos),
            ed->map.line_thickness);
    }

    // draw potential vertex location
    sgp_draw_filled_rect(
        v2_spread(
            v2_sub(
                cursor_pos,
                v2_of(ed->map.vertex_size / 2.0f))),
        v2_spread(v2_of(ed->map.vertex_size)));
}

static const char *cm_wall_status_line(cursor_t *cur) {
    if (cur->mode_wall.started) {
        return
            mem_strfmt(
                &g->frame_arena,
                "WALL FROM (%f, %f)",
                cur->mode_wall.start.x,
                cur->mode_wall.start.y);
    } else {
        return "WALL";
    }
}

static bool cm_vertex_trigger(cursor_t *cur) {
    return cur->mode == CM_DEFAULT
        && (ed->buttons[BUTTON_NEW_VERTEX] & INPUT_PRESS);
}

static void cm_vertex_update(cursor_t *cur) {
    // stop after deselect
    if (ed->buttons[BUTTON_DESELECT] & INPUT_RELEASE) {
        cur->mode = CM_DEFAULT;
        return;
    }

    if (!(ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        return;
    }

    // add new vertex
    wall_t *wall = NULL;
    lptr_t hover = LPTR_NULL;
    const v2 pos = map_snapped_cursor_pos(cur, &hover);

    if (lptr_matches(hover, LTF_WALL)) {
        wall = lptr_wall(ed->level, hover);
    } else if (lptr_matches(hover, LTF_SIDE)) {
        wall = lptr_side(ed->level, hover)->wall;
    }

    if (wall) {
        wall_split(ed->level, wall, pos);
    } else {
        vertex_new(ed->level, pos);
    }
}

static void cm_vertex_render(cursor_t *cur) {
    const v2 pos = map_snapped_cursor_pos(cur, NULL);
    sgp_set_color(v4_spread(MAP_NEW_VERTEX_COLOR));
    sgp_draw_filled_rect(
        v2_spread(
            v2_sub(
                pos,
                v2_of(ed->map.vertex_size / 2.0f))),
        v2_spread(v2_of(ed->map.vertex_size)));
}

static const char *cm_vertex_status_line(cursor_t *cur) {
    strbuf_t buf = strbuf_create(&g->frame_arena);

    lptr_t hover;
    const v2 pos = map_snapped_cursor_pos(cur, &hover);
    wall_t *wall = lptr_wall(ed->level, hover);

    strbuf_ap_fmt(&buf, "VERTEX (%f, %f)", pos.x, pos.y);

    if (wall) {
        strbuf_ap_fmt(
            &buf,
            " (%s)",
            lptr_to_str(ed->level, lptr_from(wall), &g->frame_arena));
    }

    return buf;
}

static bool cm_side_trigger(cursor_t *cur) {
    if (cur->mode == CM_DEFAULT
        && (ed->buttons[BUTTON_NEW_SIDE] & INPUT_PRESS)) {
        cur->mode_side.started = false;
        return true;
    }

    return false;
}

static void cm_side_update(cursor_t *cur) {
    if (ed->buttons[BUTTON_SELECT] & INPUT_PRESS) {
        cur->mode_side.started = true;
        cur->mode_side.start = cur->pos.map;
    }

    if (!(cur->mode_side.started
        && (ed->buttons[BUTTON_SELECT] & INPUT_RELEASE))) {
        return;
    }

    // find sides we intersect
    wall_t *walls[64];
    side_t **sides[64];
    const int n =
        level_intersect_walls_on_line(
            ed->level,
            cur->mode_side.start,
            cur->pos.map,
            &walls[0],
            &sides[0],
            16);

    for (int i = 0; i < n; i++) {
        if (*sides[i]) { continue; }

        // create side like other side
        const int index = sides[i] == &walls[i]->sides[0] ? 0 : 1;

        side_t *new_side = side_new(ed->level, walls[i]->sides[1 - index]);
        wall_set_side(ed->level, walls[i], index, new_side);
        side_recalculate(ed->level, new_side);
    }

    // return to default state unless shift is held
    if (!(ed->buttons[BUTTON_MULTI_SELECT]
            & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }

    cur->mode_side.started = false;
}

static void cm_side_render(cursor_t *cur) {
    if (!cur->mode_side.started) { return; }

    sgp_set_color(v4_spread(MAP_SIDE_ARROW_COLOR));
    sgp_ext_draw_thick_line(
        v2_spread(cur->mode_side.start),
        v2_spread(cur->pos.map),
        ed->map.line_thickness);
}

static const char *cm_side_status_line(cursor_t *cur) {
    return "SIDE";
}

static bool cm_ezportal_trigger(cursor_t *cur) {
    if (cur->mode == CM_DEFAULT
        && (ed->buttons[BUTTON_EZPORTAL] & INPUT_RELEASE)) {
        cur->mode_ezportal.started = false;
        return true;
    }

    return false;
}

static void cm_ezportal_update(cursor_t *cur) {
    if (ed->buttons[BUTTON_SELECT]
            & INPUT_PRESS) {
        cur->mode_ezportal.started = true;
        cur->mode_ezportal.start = cur->pos.map;
    }

    if (!(cur->mode_ezportal.started
          && (ed->buttons[BUTTON_SELECT] & INPUT_RELEASE))) {
        return;
    }

    // get walls in area and make portals between all sides, making sides if
    // they do not already exist
    DYNLIST(lptr_t) ptrs = dynlist_create(lptr_t, &g->frame_arena);
    level_ptrs_in_area(
            ed->level,
            box2f_mm(cur->mode_ezportal.start, cur->pos.map),
            LTF_WALL,
            &ptrs,
            LPIA_NONE);

    dynlist_each(ptrs, it) {
        wall_t *wall = lptr_wall(ed->level, *it.el);

        // must be a sector on either side of wall
        sector_t *sects[2];
        for (int i = 0; i < 2; i++) {
            if (wall->sides[i]) {
                sects[i] = wall->sides[i]->sector;
            } else {
                const int sign = i == 0 ? 1 : -1;
                const v2 midpoint = wall_midpoint(wall);
                sects[i] =
                    level_find_point_sector(
                        ed->level,
                        v2_add(
                            midpoint,
                            v2_scale(
                                wall->normal,
                                0.001f * sign)),
                        NULL);

                // if no sector is found, try to add a side and create one
                side_t *new_side =
                    side_new(
                        ed->level,
                        wall->sides[1 - i]);

                wall_set_side(ed->level, wall, i, new_side);

                sects[i] = level_update_side_sector(ed->level, new_side);

                // if we couldn't create sector, remove side
                if (!sects[i]) {
                    side_delete(ed->level, new_side);
                }
            }
        }

        // must have side on both sides now
        if (!wall->sides[0] || !wall->sides[1]) {
            continue;
        }

        wall->sides[0]->portal = wall->sides[1];
        wall->sides[1]->portal = wall->sides[0];

        side_recalculate(ed->level, wall->sides[0]);
        side_recalculate(ed->level, wall->sides[1]);

        if (wall->sides[0]->sector) {
            sector_recalculate(ed->level, wall->sides[0]->sector);
        }

        if (wall->sides[1]->sector) {
            sector_recalculate(ed->level, wall->sides[1]->sector);
        }
    }

    dynlist_destroy(ptrs);

    // only continue on shift
    if (!(ed->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }

    cur->mode_ezportal.started = false;
}

static void cm_ezportal_render(cursor_t *cur) {
    if (!cur->mode_ezportal.started) { return; }

    sgp_set_color(0.5f, 1.0f, 0.6f, 0.3f);
    sgp_ext_draw_filled_box2f(
        box2f_mm(
            cur->mode_ezportal.start,
            cur->pos.map));
}

static const char *cm_ezportal_status_line(cursor_t *cur) {
    return "EZPORTAL";
}

static bool cm_fixer_trigger(cursor_t *cur) {
    return cur->mode == CM_DEFAULT
        && (ed->buttons[BUTTON_FIXER] & INPUT_RELEASE);
}

static void cm_fixer_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_PRESS)) {
        return;
    }

    // find nearest side to cursor and repair its sector
    side_t *nearest = level_nearest_side(ed->level, cur->pos.map);

    if (!nearest) {
        goto done;
    }

    level_update_side_sector(ed->level, nearest);

    DYNLIST(side_t*) trace = dynlist_create(side_t*, &g->frame_arena);
    if (level_trace_sides(ed->level, nearest, &trace, NULL)) {
        dynlist_each(trace, it) {
            level_update_side_sector(ed->level, *it.el);
        }
    }

    if (!nearest->sector) {
        goto done;
    }

    const box2f_t box = box2f_mm(nearest->sector->min, nearest->sector->max);

    // enqueue side updates for *all* contained sides
    DYNLIST(wall_t*) walls = dynlist_create(wall_t*, &g->frame_arena);
    level_walls_in_area(ed->level, box, &walls);

    dynlist_each(walls, it) {
        for (int i = 0; i < 2; i++) {
            if (!(*it.el)->sides[i]) { continue; }
            level_push_dirty_sect_side(ed->level, (*it.el)->sides[i]);
        }
    }

    sector_recalculate(ed->level, nearest->sector);

done:
    if (!(ed->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }
}

static void cm_fixer_render(cursor_t *cur) {
    // show nearest side which would be affected
    side_t *nearest = level_nearest_side(ed->level, cur->pos.map);

    if (nearest) {
        sgp_set_color(v4_spread(MAP_SELECT_BORDER_COLOR));
        sgp_ext_draw_thick_line(
            v2_spread(side_normal_point(nearest)),
            v2_spread(cur->pos.map),
            ed->map.line_thickness);
    }
}

static const char *cm_fixer_status_line(cursor_t *cur) {
    return "SECTOR FIXER";
}

static bool cm_delete_trigger(cursor_t *cur) {
    if (cur->mode == CM_DEFAULT
        && (ed->buttons[BUTTON_DELETE] & INPUT_RELEASE)
        && (ed->mode != EDITOR_MODE_CAM || !ed->mouse_grab)) {
        cur->mode_delete.drag = false;
        return true;
    }

    return false;
}

static void cm_delete_update(cursor_t *cur) {
    if (ed->mode == EDITOR_MODE_CAM) {
        if (!ed->mouse_grab
            && (ed->buttons[BUTTON_SELECT] & INPUT_PRESS)) {
            if (!lptr_is_null(ed->cam.ptr)) {
                if (!(lptr_is(ed->cam.ptr, LT_ENTITY)
                      || lptr_is(ed->cam.ptr, LT_DECAL))) {
                    WARN(
                        "can't delete type %s in camera mode",
                         level_type_to_str(lptr_type(ed->cam.ptr)));
                } else {
                    // try to delete hover
                    level_enqueue_delete(ed->level, ed->cam.ptr);
                }

                ed->cam.ptr = LPTR_NULL;
            }
        }

        return;
    }

    if ((ed->buttons[BUTTON_SELECT] & INPUT_PRESS)) {
        if (ed->buttons[BUTTON_SELECT_AREA] & INPUT_DOWN) {
            cur->mode_delete.drag = true;
            cur->mode_delete.drag_start = cur->pos.map;
        } else if (!lptr_is_null(ed->cur.hover)) {
            // only delete sectors if left control is also down
            if (lptr_type(ed->cur.hover) != LT_SECTOR
                || (ed->buttons[BUTTON_DELETE_SECTOR] & INPUT_DOWN)) {
                // try to delete hover
                lptr_delete(ed->level, ed->cur.hover);
            } else {
                WARN("not deleting sector because lctrl is not pressed");
            }
        }
    }

    if (cur->mode_delete.drag
        && (ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        // delete in area
        DYNLIST(lptr_t) ptrs = dynlist_create(lptr_t, &g->frame_arena);
        level_ptrs_in_area(
            ed->level,
            box2f_mm(cur->mode_delete.drag_start, cur->pos.map),
            LTF_SIDE | LTF_WALL | LTF_VERTEX | LTF_DECAL | LTF_ENTITY,
            &ptrs,
            LPIA_WHOLE_WALL);

        dynlist_each(ptrs, it) {
            lptr_delete(ed->level, *it.el);
        }

        cur->mode_delete.drag = false;
    }
}

static void cm_delete_render(cursor_t *cur) {
    if (!cur->mode_delete.drag) {
        return;
    }

    sgp_set_color(1.0f, 0.2f, 0.2f, 0.2f);
    sgp_ext_draw_filled_box2f(
        box2f_sort(
            box2f_mm(
                cur->mode_delete.drag_start,
                cur->pos.map)));
}

static const char *cm_delete_status_line(cursor_t *cur) {
    strbuf_t buf = strbuf_create(&g->frame_arena);
    strbuf_ap_fmt(&buf, "DELETE");

    const lptr_t ptr = ed->mode == EDITOR_MODE_CAM ? ed->cam.ptr : cur->hover;

    if (!lptr_is_null(ptr)) {
        strbuf_ap_fmt(&buf, " %s", lptr_to_str(ed->level, ptr, &g->frame_arena));
    }

    return buf;
}

static bool cm_fuse_trigger(cursor_t *cur) {
    if (cur->mode == CM_DEFAULT
        && (ed->buttons[BUTTON_FUSE] & INPUT_RELEASE)) {
        cur->mode_fuse.first = LPTR_NULL;
        return true;
    }

    return false;
}

static void cm_fuse_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        return;
    } else if (!lptr_matches(cur->hover, LTF_VERTEX | LTF_SECTOR)) {
        return;
    }

    if (lptr_is_null(cur->mode_fuse.first)) {
        // grab first thing
        cur->mode_fuse.first = cur->hover;
        return;
    } else if (lptr_type_flag(cur->mode_fuse.first) != lptr_type_flag(cur->hover)) {
        // don't grab second thing if not of same type
        WARN(
            "must select a %s",
            level_type_to_str(lptr_type(cur->mode_fuse.first)));
        return;
    }

    ASSERT(!lptr_is_null(cur->mode_fuse.first));
    ASSERT(!lptr_is_null(cur->hover));
    ASSERT(lptr_type_flag(cur->mode_fuse.first) == lptr_type_flag(cur->hover));

    if (lptr_type_flag(cur->hover) == LTF_VERTEX) {
        vertex_t *vs[2] = {
            lptr_vertex(ed->level, cur->mode_fuse.first),
            lptr_vertex(ed->level, cur->hover)
        };

        // if there is a wall connecting the vertices directly, remove it
        wall_t *share_wall = vertices_shared_wall(ed->level, vs[0], vs[1]);
        if (share_wall) {
            wall_delete(ed->level, share_wall);
        }

        // change all vertex connections from vx -> v0 to vx -> v1
        DYNLIST(wall_t*) walls = dynlist_create(wall_t*, &g->frame_arena);
        dynlist_copy_from(walls, vs[0]->walls);

        dynlist_each(walls, it) {
            wall_t *wall = *it.el;

            // remove wall if vertices are already vx -> v1
            const int vindex = vs[0] == wall->v0 ? 0 : 1;

            // check if there already exists a wall from the other vertex to the
            // vertex we are fusing with
            if (vertices_shared_wall(
                    ed->level,
                    wall->vertices[1 - vindex],
                    vs[1])) {
                wall_delete(ed->level, wall);
            } else {
                wall_set_vertex(
                    ed->level,
                    wall,
                    vindex,
                    vs[1]);
            }
        }

        dynlist_destroy(walls);

        ASSERT(dynlist_size(vs[0]->walls) == 0);
        vertex_delete(ed->level, vs[0]);
    } else if (lptr_type_flag(cur->hover) == LTF_SECTOR) {
        sector_t *sects[2] = {
            lptr_sector(ed->level, cur->mode_fuse.first),
            lptr_sector(ed->level, cur->hover)
        };

        // if there is a wall connecting the vertices directly, remove it
        DYNLIST(wall_t*) shared_walls =
            dynlist_create(wall_t*, &g->frame_arena);
        sector_shared_walls(sects[0], sects[1], &shared_walls);

        if (dynlist_size(shared_walls) == 0) {
            show_editor_error("no shared walls");
            goto done;
        }

        // remove walls
        dynlist_each(shared_walls, it) {
            wall_delete(ed->level, *it.el);
        }

        // fuse sides from first sector into second
        DYNLIST(side_t*) sides = dynlist_create(side_t*, &g->frame_arena);
        sector_get_sides(ed->level, sects[0], &sides);

        dynlist_each(sides, it) {
            sector_remove_side(ed->level, sects[0], *it.el);
        }

        // first sector should no longer exist, second should be OK
        ASSERT(!lptr_is_valid(ed->level, cur->mode_fuse.first));
        ASSERT(lptr_is_valid(ed->level, cur->hover));

        // add sides into other sector
        dynlist_each(sides, it) {
            sector_add_side(ed->level, sects[1], *it.el);
        }

        // recalc remaining sector
        sector_recalculate(ed->level, sects[1]);
    }

done:
    if (!(ed->buttons[BUTTON_MULTI_SELECT]
          & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }

    cur->mode_fuse.first = LPTR_NULL;
}


static void cm_fuse_render(cursor_t *cur) {
    if (lptr_is_null(cur->mode_fuse.first)) {
        return;
    }

    sgp_set_color(0.3f, 1.0f, 0.85f, 0.85f);

    const v2 start = map_center_for_ptr(cur->mode_fuse.first);
    sgp_ext_draw_thick_line(
        v2_spread(start),
        v2_spread(ed->cur.pos.map),
        ed->map.line_thickness);
}

static const char *cm_fuse_status_line(cursor_t *cur) {
    strbuf_t buf = strbuf_create(&g->frame_arena);
    strbuf_ap_fmt(&buf, "FUSE");

    if (!lptr_is_null(cur->mode_fuse.first)) {
        strbuf_ap_fmt(
            &buf,
            " (%s WITH...)",
            lptr_to_str(ed->level, cur->mode_fuse.first, &g->frame_arena));
    }

    return buf;
}

static bool cm_split_trigger(cursor_t *cur) {
    return cur->mode == CM_DEFAULT
        && (ed->buttons[BUTTON_SPLIT] & INPUT_RELEASE);
}

static void cm_split_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        return;
    } else if (!lptr_matches(cur->hover, LTF_VERTEX)) {
        return;
    }

    vertex_t *va = lptr_vertex(ed->level, cur->hover);

    // try to find a valid location to split to which is:
    // * >0.025 units away
    // * doesn't already have a near a vertex
    // * not in a sector

    const f32 unit = 0.15f;

    v2i
        units =
            v2i_from_v(v2_maxv(v2_divs(va->pos, unit), v2_of(0.0f))),
        ok_units = v2i_of(-1, -1);

    sector_t *last_sect = NULL;

    // search in a spiral pattern
    int i = 0, leg = 0, layer = 4;
    while (i < 128) {
        const v2 pos = v2_scale(v2_from_i(units), unit);
        f32 nearest_dist;
        level_nearest_vertex(ed->level, pos, &nearest_dist);

        if (nearest_dist < unit) {
            ok_units = units;

            if (!(last_sect =
                    level_find_point_sector(
                        ed->level, pos, last_sect))) {
                // no sector -> good spot
                goto done;
            }
        }

        switch (leg) {
        case 0:
            units.y++;
            if (+units.y == layer) { leg++; }
            break;
        case 1:
            units.x++;
            if (+units.x == layer) { leg++; }
            break;
        case 2:
            units.y--;
            if (-units.y == layer || units.y < 0) { leg++; }
            break;
        case 3:
            units.x--;
            if (-units.x == layer || units.x < 0) { leg = 0; layer++; }
            break;
        }

        units = v2i_clampv(units, v2i_of(0), v2i_of(INT_MAX));
        i++;
    }
done:;
    if (ok_units.x == -1 || ok_units.y == -1) {
        WARN("could not find good spot for vertex");
        cur->mode = CM_DEFAULT;
        return;
    }

    // create new vertex at okpos
    vertex_t *vb =
        vertex_new(ed->level, v2_scale(v2_from_i(units), unit));

    // create wall between v and vnew, create sides where there is already a
    // sector
    wall_t *new_wall = wall_new(ed->level, va, vb);

    // calculate wall midpoint and normal
    const v2
        midpoint = wall_midpoint(new_wall),
        normal = new_wall->normal;

    for (int i = 0; i < 2; i++){
        sector_t *sect =
            level_find_point_sector(
                ed->level,
                v2_add(
                    midpoint,
                    v2_scale(normal, (i == 0 ? 1 : -1) * 0.01f)),
                NULL);

        side_t *side =
            sect ?
                side_new(ed->level, level_find_near_side(ed->level, va, sect))
                : NULL;

        if (side) {
            wall_set_side(ed->level, new_wall, i, side);
            sector_add_side(ed->level, sect, side);
        }
    }

    DYNLIST(wall_t*) to_move = dynlist_create(wall_t*, &g->frame_arena);

    // move walls which would be shorted going to vb from va -> vb
    dynlist_each(va->walls, it) {
        if (*it.el == new_wall) {
            continue;
        }

        // check if len(other -> vb) < len(other -> va)
        const int i = va == (*it.el)->v0 ? 1 : 0;
        const f32 lenb =
            v2_norm(
                v2_sub(
                    (*it.el)->vertices[i]->pos,
                    vb->pos));

        if (lenb < (*it.el)->len) {
            *dynlist_push(to_move) = *it.el;
        }
    }

    dynlist_each(to_move, it) {
        wall_set_vertex(ed->level, *it.el, va == (*it.el)->v0 ? 0 : 1, vb);
    }

    dynlist_destroy(to_move);

    if (!(ed->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }
}

static const char *cm_split_status_line(cursor_t *cur) {
    strbuf_t buf = strbuf_create(&g->frame_arena);
    strbuf_ap_fmt(&buf, "SPLIT");

    if (!lptr_is_null(cur->hover) && lptr_is(cur->hover, LT_VERTEX)) {
        strbuf_ap_fmt(
            &buf,
            " %s",
            lptr_to_str(ed->level, cur->hover, &g->frame_arena));
    }

    return buf;
}

// gets either side if ptr is side OR single side from wall with only one side
// side must have sector
// otherwise returns NULL
static side_t *cm_connect__side_from_ptr(lptr_t ptr) {
    side_t *side = NULL;

    if (lptr_matches(ptr, LTF_SIDE)) {
        side = lptr_side(ed->level, ptr);
    } else if (lptr_matches(ptr, LTF_WALL)) {
        wall_t *wall = lptr_wall(ed->level, ptr);

        if (wall->sides[0] && !wall->sides[1]) {
            side = wall->sides[0];
        } else if (wall->sides[1] && !wall->sides[0]) {
            side = wall->sides[1];
        }
    }

    return side && side->sector ? side : NULL;
}

static bool cm_connect_trigger(cursor_t *cur) {
    if (ed->buttons[BUTTON_CONNECT] & INPUT_PRESS) {
        cur->mode_connect.first = NULL;
        return true;
    }

    return false;
}

static void cm_connect_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        return;
    }

    side_t *side = cm_connect__side_from_ptr(cur->hover);

    if (!side) {
        return;
    }

    if (!cur->mode_connect.first) {
        cur->mode_connect.first = side;
        return;
    }

    // connect first -> ptr
    cur->mode_connect.first->portal = side;
    side->portal = cur->mode_connect.first;

    cur->mode_connect.first->version++;
    side->version++;

    side_recalculate(ed->level, cur->mode_connect.first);
    editor_mark_ptr_change(lptr_from(cur->mode_connect.first));
    side_recalculate(ed->level, side);
    editor_mark_ptr_change(lptr_from(side));

    if (!(ed->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }
}

static void cm_connect_render(cursor_t *cur) {
    if (!cur->mode_connect.first) {
        return;
    }

    side_t *hoverside = cm_connect__side_from_ptr(cur->hover);

    const v4 color = V4_FROM_ABGR(hoverside ? 0xFF20FF20 : 0xFF2020FF);
    sgp_set_color(v4_spread(color));
    sgp_ext_draw_thick_line(
        v2_spread(side_normal_point(cur->mode_connect.first)),
        v2_spread(cur->pos.map),
        ed->map.line_thickness);
}

static const char *cm_connect_status_line(cursor_t *cur) {
    strbuf_t buf = strbuf_create(&g->frame_arena);
    strbuf_ap_fmt(&buf, "CONNECT");

    if (cur->mode_connect.first) {
        strbuf_ap_fmt(
            &buf,
            " %s WITH",
            lptr_to_str(
                ed->level,
                lptr_from(cur->mode_connect.first),
                &g->frame_arena));

        const side_t *hover_side = cm_connect__side_from_ptr(cur->hover);
        if (hover_side) {
            strbuf_ap_fmt(
                &buf,
                " %s",
                lptr_to_str(
                    ed->level,
                    lptr_from(hover_side),
                    &g->frame_arena));
        } else {
            strbuf_ap_fmt(&buf, "...");

        }
    } else {
        strbuf_ap_fmt(&buf, "...");
    }

    return buf;
}

static bool cm_decal_trigger(cursor_t *cur) {
    if (ed->buttons[BUTTON_NEW_DECAL] & INPUT_RELEASE) {
        cur->mode_decal.clone = LPTR_NULL;
        return true;
    }

    return false;
}

static void cm_decal_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        return;
    }

    side_t *side = NULL;
    f32 x;
    sector_t *sector = NULL;
    v2 pos, world_pos;

    if (ed->mode == EDITOR_MODE_CAM) {
        if (ed->cam.side) {
            pos = ed->cam.side_pos;
            side = ed->cam.side;
        } else if (ed->cam.sect) {
            pos = ed->cam.sect_pos;
            sector = ed->cam.sect;
        } else {
            return;
        }
    } else {
        decal_placement_pos(
            cur->pos.map, &side, &x, &sector, &pos, &world_pos);

        pos =
            v2_of(
                x,
                side && side->sector ?
                    ((side->sector->ceil.z - side->sector->floor.z) / 2)
                    : 0);
    }

    decal_t *d = NULL;

    if (side) {
        d = decal_new(ed->level, NULL);
        d->side.offsets = pos;
        decal_set_side(ed->level, d, side);
    } else if (sector) {
        d = decal_new(ed->level, NULL);
        d->sector.pos = pos;
        decal_set_sector(ed->level, d, sector, PLANE_TYPE_FLOOR);
    } else {
        return;
    }

    if (d) {
        if (lptr_is_valid(ed->level, cur->mode_decal.clone)) {
            decal_t *clone_from = lptr_decal(ed->level, cur->mode_decal.clone);
            d->type = clone_from->type;
            d->tex = clone_from->tex;
            d->tex_offsets = clone_from->tex_offsets;
        }

        decal_recalculate(ed->level, d);
    }

    if (!(ed->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }
}

static void cm_decal_render(cursor_t *cur) {
    side_t *side = NULL;
    f32 x;
    sector_t *sector = NULL;
    v2 pos, world_pos;
    decal_placement_pos(cur->pos.map, &side, &x, &sector, &pos, &world_pos);

    if (side || sector) {
        sgp_set_color(v4_spread(MAP_DECAL_COLOR));
    } else {
        sgp_set_color(0.8f, 0.2f, 0.2f, 0.6f);
    }

    sgp_draw_filled_rect(
        v2_spread(v2_sub(world_pos, v2_of(MAP_DECAL_SIZE / 2))),
        v2_spread(v2_of(MAP_DECAL_SIZE)));
}

static const char *cm_decal_status_line(cursor_t *cur) {
    return "NEW DECAL";
}

static bool cm_entity_trigger(cursor_t *cur) {
    if (ed->buttons[BUTTON_NEW_ENTITY] & INPUT_RELEASE) {
        cur->mode_entity.clone = LPTR_NULL;
        cur->mode_entity.bookmark = 0;
        return true;
    }

    return false;
}

static void cm_entity_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        return;
    }

    const entity_t *clone = lptr_entity(ed->level, cur->mode_entity.clone);
    v2 pos;

    if (ed->mode == EDITOR_MODE_CAM) {
        if (clone && clone->ptype->is_attach) {
            // allow attaching to sides, sectors
            if (!lptr_matches(ed->cam.ptr, LTF_SIDE | LTF_SECTOR)) {
                return;
            }

            // ignored
            pos = v2_of(0);
        } else {
            // only allow sectors otherwise
            if (!lptr_is(ed->cam.ptr, LT_SECTOR)) {
                return;
            }

            pos = ed->cam.sect_pos;
        }
    } else {
        pos = map_snapped_cursor_pos(cur, NULL);
    }

    // create entity
    entity_t *ent = entity_new(ed->level, NULL);

    if (!v2_eqv_eps(pos, v2_of(0))) {
        entity_try_move(ed->level, ent, pos);
    }

    if (clone) {
        entity_set_type(ed->level, ent, clone->itype);

        if (clone->ptype->is_attach) {
            // attach to selected point
            if (lptr_is(ed->cam.ptr, LT_SECTOR)) {
                entity_attach_sector(
                    ed->level,
                    ent,
                    ed->cam.sect,
                    ed->cam.sect_pos,
                    ed->cam.plane);
            } else if (lptr_is(ed->cam.ptr, LT_SIDE)) {
                entity_attach_side(
                    ed->level,
                    ent,
                    ed->cam.side,
                    ed->cam.side_pos);
            }
        }

    } else if (cur->mode_entity.bookmark) {
        entity_set_type(ed->level, ent, ENTITY_TYPE_BOOKMARK);
        ent->bookmark_index = cur->mode_entity.bookmark;
    }

    if (!(ed->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }
}

static void cm_entity_render(cursor_t *cur) {
    const v2 pos = map_snapped_cursor_pos(cur, NULL);
    sgp_set_color(v4_spread(MAP_ENTITY_COLOR));
    sgp_draw_filled_rect(
        v2_spread(v2_sub(pos, v2_of(MAP_ENTITY_SIZE / 2))),
        v2_spread(v2_of(MAP_ENTITY_SIZE)));
}

static const char *cm_entity_status_line(cursor_t *cur) {
    return "NEW ENTITY";
}

static bool cm_move_decal_trigger(cursor_t *cur) {
    if (ed->mode == EDITOR_MODE_CAM
        && (ed->buttons[BUTTON_SELECT] & INPUT_PRESS)
        && lptr_matches(ed->cam.ptr, LTF_DECAL)) {
        cur->mode_move_decal.decal = lptr_decal(ed->level, ed->cam.ptr);
        return true;
    }

    return false;
}

static void cm_move_decal_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
    }

    if (!lptr_matches(ed->cam.ptr, LTF_SECTOR | LTF_SIDE | LTF_DECAL)) {
        return;
    }

    decal_t *decal = cur->mode_move_decal.decal;

    if (ed->cam.side) {
        if (!decal->is_on_side || ed->cam.side != decal->side.ptr) {
            decal_set_side(ed->level, decal, ed->cam.side);
        }

        decal->side.offsets = ed->cam.side_pos;
    } else if (ed->cam.sect) {
        decal_set_sector(
            ed->level,
            decal,
            ed->cam.sect,
            ed->cam.plane);

        decal->sector.pos = ed->cam.sect_pos;
    }

    decal_recalculate(ed->level, decal);
}

static const char *cm_move_decal_status_line(cursor_t *cur) {
    return "MOVE DECAL";
}

static bool cm_move_entity_trigger(cursor_t *cur) {
    if (ed->mode == EDITOR_MODE_CAM
        && (ed->buttons[BUTTON_SELECT] & INPUT_PRESS)
        && lptr_matches(ed->cam.ptr, LTF_ENTITY)) {
        cur->mode_move_entity.entity = lptr_entity(ed->level, ed->cam.ptr);
        return true;
    }

    return false;
}

static void cm_move_entity_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_DOWN)) {
        cur->mode = CM_DEFAULT;
        return;
    }

    entity_t *ent = cur->mode_move_entity.entity;

    if (ent->ptype->is_attach) {
        if (lptr_is(ed->cam.ptr, LT_SIDE)) {
            entity_attach_side(
                ed->level, ent,
                ed->cam.side,
                ed->cam.side_pos);
        } else if (lptr_is(ed->cam.ptr, LT_SECTOR)) {
            entity_attach_sector(
                ed->level, ent,
                ed->cam.sect,
                ed->cam.sect_pos,
                ed->cam.plane);
        } else {
            return;
        }
    } else {
        if (!lptr_matches(ed->cam.ptr, LTF_SECTOR)) {
            return;
        }

        // get world point from sector
        entity_try_move(ed->level, ent, ed->cam.sect_pos);
    }

    ent->version++;
    ed->level->version++;
}

static const char *cm_move_entity_status_line(cursor_t *cur) {
    return "MOVE ENTITY";
}

static bool cm_tex_mod_trigger(cursor_t *cur) {
    if ((ed->buttons[BUTTON_TEX_MOD] & INPUT_RELEASE)
            && cur->start_mode != CM_TEX_MOD
            && ed->mode == EDITOR_MODE_CAM
            && !ed->mouse_grab) {
        memset(&cur->mode_tex_mod, 0, sizeof(cur->mode_tex_mod));
        return true;
    }

    return false;
}

static void cm_tex_mod_update(cursor_t *cur) {
    if ((ed->buttons[BUTTON_TEX_MOD] & INPUT_RELEASE)
        || (ed->buttons[BUTTON_CANCEL] & INPUT_RELEASE)) {
        cur->mode = CM_DEFAULT;
        return;
    }

    const lptr_t hover_ptr = ed->cam.ptr;
    plane_type_e hover_plane = ed->cam.plane;
    if (!lptr_matches(hover_ptr, LTF_SIDE | LTF_SECTOR)) {
        cur->mode_tex_mod.last_ptr = hover_ptr;
        return;
    }

    bool change = false;

    const bool snap = ed->buttons[BUTTON_TEX_MOD_SNAP] & INPUT_DOWN;

    if (ed->buttons[BUTTON_TEX_MOD_RESET] & INPUT_PRESS) {
        // full reset of scale/offsets
        if (lptr_matches(hover_ptr, LTF_SIDE)) {
            side_t *side = lptr_side(ed->level, hover_ptr);
            side->mat.offsets = v2i_of(0);
            change = true;
        } else {
            sector_t *sector = lptr_sector(ed->level, hover_ptr);
            sector->mat.offsets[hover_plane] = v2i_of(0);
            change = true;
        }
    }

    if (ed->buttons[BUTTON_SELECT] & INPUT_PRESS) {
        cur->mode_tex_mod.drag_ptr = hover_ptr;

        if (lptr_matches(hover_ptr, LTF_SIDE)) {
            const side_t *side = lptr_side(ed->level, hover_ptr);
            cur->mode_tex_mod.drag_start = ed->cam.side_pos;
            cur->mode_tex_mod.drag_start_offsets = side->mat.offsets;
        } else {
            const sector_t *sector = lptr_sector(ed->level, hover_ptr);
            cur->mode_tex_mod.drag_start = ed->cam.sect_pos;
            cur->mode_tex_mod.drag_plane = ed->cam.plane;
            cur->mode_tex_mod.drag_start_offsets =
                sector->mat.offsets[hover_plane];
        }
    } else if (
        (ed->buttons[BUTTON_SELECT] & INPUT_DOWN)
        && lptr_is_valid(ed->level, cur->mode_tex_mod.drag_ptr)) {
        if (!lptr_eq(ed->cam.ptr, cur->mode_tex_mod.drag_ptr)) {
            // do nothing, don't allow to drag outside
        } else if (lptr_matches(cur->mode_tex_mod.drag_ptr, LTF_SIDE)) {
            side_t *side = lptr_side(ed->level, hover_ptr);
            v2i diff =
                v2i_from_v(
                    v2_scale(
                        v2_sub(
                            ed->cam.side_pos, cur->mode_tex_mod.drag_start),
                    PX_PER_UNIT));

            if (snap) {
                diff = v2i_scale(v2i_divs(diff, 8), 8);
            }

            // UVs are revered from position since they are done right to left
            // even though side vertices have left front faces
            diff.x *= -1;

            side->mat.offsets =
                v2i_add(cur->mode_tex_mod.drag_start_offsets, diff);
            change = true;
        } else {
            sector_t *sector = lptr_sector(ed->level, hover_ptr);
            v2i diff =
                v2i_from_v(
                    v2_scale(
                        v2_sub(
                            ed->cam.sect_pos, cur->mode_tex_mod.drag_start),
                    PX_PER_UNIT));

            if (snap) {
                diff = v2i_scale(v2i_divs(diff, 8), 8);
            }

            sector->mat.offsets[hover_plane] =
                v2i_add(cur->mode_tex_mod.drag_start_offsets, diff);
            change = true;
        }
    }

    if (change) {
        lptr_recalculate(ed->level, hover_ptr);
    }

    cur->mode_tex_mod.last_ptr = hover_ptr;
}

static const char *cm_tex_mod_status_line(cursor_t *cur) {
    lptr_t ptr = ed->cam.ptr;
    if (!lptr_matches(ptr, LTF_SIDE | LTF_SECTOR)) { ptr = LPTR_NULL; }

    return
        mem_strfmt(
            &g->frame_arena,
            "TEX MOD %s",
            lptr_is_null(ptr) ? "" : lptr_to_str(ed->level, ptr, &g->frame_arena));
}

static bool cm_new_room_trigger(cursor_t *cur) {
    return ed->mode == EDITOR_MODE_MAP
        && ed->cur.mode == CM_DEFAULT
        && (ed->buttons[BUTTON_NEW_ROOM] & INPUT_PRESS);
}

static void cm_new_room_update(cursor_t *cur) {
    if (!(ed->buttons[BUTTON_SELECT] & INPUT_RELEASE)) {
        return;
    } else if (cur->pos.map.x < 0.0f || cur->pos.map.y < 0.0f) {
        return;
    }

    const v2i pos = v2i_from_v(v2_floor(cur->pos.map));

    room_t *r = room_new(ed->level);
    r->bounds = box2i_mm(pos, pos);
    LOG("Allocated room %d", r->id);

    if (!(ed->buttons[BUTTON_MULTI_SELECT] & INPUT_DOWN)) {
        ed->cur.mode = CM_DEFAULT;
    }
}

static void cm_new_room_render(cursor_t *cur) {
    if (cur->pos.map.x < 0.0f || cur->pos.map.y < 0.0f) {
        return;
    }

    const v2i pos = v2i_from_v(v2_floor(cur->pos.map));
    const v2 center = v2_add(v2_from_i(pos), v2_of(0.5f));

    sgp_set_color(0.7f, 0.7f, 0.1f, 0.5f);
    sgp_draw_filled_rect(center.x - 0.25f, center.y - 0.25f, 0.5f, 0.5f);
}

static const char *cm_new_room_status_line(cursor_t *cur) {
    return "NEW ROOM";
}

/* CURSOR MODE TEMPLATE
 *
 * static bool cm_replaceme_trigger(cursor_t *cur) {
 *     return false;
 * }
 *
 * static void cm_replaceme_update(cursor_t *cur) {
 *
 * }
 *
 * static void cm_replaceme_render(cursor_t *cur) {
 *
 * }
 *
 * static const char *cm_replaceme_status_line(cursor_t *cur) {
 *
 * }
 */

static cursor_mode_t CURSOR_MODES[CM_COUNT] = {
    [CM_DEFAULT] = {
        .mode = CM_DEFAULT,
        .flags = CMF_CLICK_CANCEL | CMF_MAP_AND_CAM,
        .priority = 0,
        .trigger = NULL,
        .update = NULL,
        .render = NULL,
        .status_line = cm_default_status_line,
        .cancel = NULL
    },
    [CM_SELECT] = {
        .mode = CM_SELECT,
        .flags = CMF_EXPLICIT_CANCEL | CMF_MAP_AND_CAM,
        .priority = 1,
        .trigger = NULL,
        .update = cm_select_id_update,
        .render = NULL,
        .status_line = cm_select_id_status_line,
        .cancel = NULL
    },
    [CM_DRAG] = {
        .mode = CM_DRAG,
        .flags = CMF_CLICK_CANCEL | CMF_MAP,
        .priority = -2,
        .trigger = cm_drag_trigger,
        .update = cm_drag_update,
        .render = NULL,
        .status_line = cm_drag_status_line,
        .cancel = cm_drag_cancel
    },
    [CM_MOVE_DRAG] = {
        .mode = CM_MOVE_DRAG,
        .flags = CMF_MAP,
        .priority = -2,
        .trigger = cm_move_drag_trigger,
        .update = cm_move_drag_update,
        .render = NULL,
        .status_line = cm_move_drag_status_line,
        .cancel = NULL
    },
    [CM_SELECT_AREA] = {
        .mode = CM_SELECT_AREA,
        .flags = CMF_NOHOVER | CMF_MAP,
        .priority = -1,
        .trigger = cm_select_area_trigger,
        .update = cm_select_area_update,
        .render = cm_select_area_render,
        .status_line = cm_select_area_status_line,
        .cancel = NULL
    },
    [CM_WALL] = {
        .mode = CM_WALL,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_wall_trigger,
        .update = cm_wall_update,
        .render = cm_wall_render,
        .status_line = cm_wall_status_line,
        .cancel = NULL
    },
    [CM_VERTEX] = {
        .mode = CM_VERTEX,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_vertex_trigger,
        .update = cm_vertex_update,
        .render = cm_vertex_render,
        .status_line = cm_vertex_status_line,
        .cancel = NULL
    },
    [CM_SIDE] = {
        .mode = CM_SIDE,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_side_trigger,
        .update = cm_side_update,
        .render = cm_side_render,
        .status_line = cm_side_status_line,
        .cancel = NULL
    },
    [CM_EZPORTAL] = {
        .mode = CM_EZPORTAL,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_ezportal_trigger,
        .update = cm_ezportal_update,
        .render = cm_ezportal_render,
        .status_line = cm_ezportal_status_line,
        .cancel = NULL
    },
    [CM_FIXER] = {
        .mode = CM_FIXER,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_fixer_trigger,
        .update = cm_fixer_update,
        .render = cm_fixer_render,
        .status_line = cm_fixer_status_line,
        .cancel = NULL
    },
    [CM_DELETE] = {
        .mode = CM_DELETE,
        .flags = CMF_NODRAG | CMF_MAP_AND_CAM,
        .priority = 10,
        .trigger = cm_delete_trigger,
        .update = cm_delete_update,
        .render = cm_delete_render,
        .status_line = cm_delete_status_line,
        .cancel = NULL
    },
    [CM_FUSE] = {
        .mode = CM_FUSE,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_fuse_trigger,
        .update = cm_fuse_update,
        .render = cm_fuse_render,
        .status_line = cm_fuse_status_line,
        .cancel = NULL
    },
    [CM_SPLIT] = {
        .mode = CM_SPLIT,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_split_trigger,
        .update = cm_split_update,
        .render = NULL,
        .status_line = cm_split_status_line,
        .cancel = NULL
    },
    [CM_CONNECT] = {
        .mode = CM_CONNECT,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_connect_trigger,
        .update = cm_connect_update,
        .render = cm_connect_render,
        .status_line = cm_connect_status_line,
        .cancel = NULL
    },
    [CM_DECAL] = {
        .mode = CM_DECAL,
        .flags = CMF_NODRAG | CMF_MAP_AND_CAM,
        .priority = 0,
        .trigger = cm_decal_trigger,
        .update = cm_decal_update,
        .render = cm_decal_render,
        .status_line = cm_decal_status_line,
        .cancel = NULL
    },
    [CM_ENTITY] = {
        .mode = CM_ENTITY,
        .flags = CMF_NODRAG | CMF_MAP_AND_CAM,
        .priority = 0,
        .trigger = cm_entity_trigger,
        .update = cm_entity_update,
        .render = cm_entity_render,
        .status_line = cm_entity_status_line,
        .cancel = NULL
    },
    [CM_MOVE_DECAL] = {
        .mode = CM_MOVE_DECAL,
        .flags = CMF_NODRAG | CMF_MAP_AND_CAM,
        .priority = 0,
        .trigger = cm_move_decal_trigger,
        .update = cm_move_decal_update,
        .render = NULL,
        .status_line = cm_move_decal_status_line,
        .cancel = NULL
    },
    [CM_MOVE_ENTITY] = {
        .mode = CM_MOVE_ENTITY,
        .flags = CMF_NODRAG | CMF_MAP_AND_CAM,
        .priority = 0,
        .trigger = cm_move_entity_trigger,
        .update = cm_move_entity_update,
        .render = NULL,
        .status_line = cm_move_entity_status_line,
        .cancel = NULL
    },
    [CM_TEX_MOD] = {
        .mode = CM_TEX_MOD,
        .flags = CMF_NODRAG | CMF_CAM,
        .priority = 0,
        .trigger = cm_tex_mod_trigger,
        .update = cm_tex_mod_update,
        .render = NULL,
        .status_line = cm_tex_mod_status_line,
        .cancel = NULL
    },
    [CM_NEW_ROOM] = {
        .mode = CM_NEW_ROOM,
        .flags = CMF_NODRAG | CMF_MAP,
        .priority = 0,
        .trigger = cm_new_room_trigger,
        .update = cm_new_room_update,
        .render = cm_new_room_render,
        .status_line = cm_new_room_status_line,
        .cancel = NULL
    },
    [CM_RESIZE_ROOM] = {
        .mode = CM_RESIZE_ROOM,
        .flags = CMF_CLICK_CANCEL | CMF_MAP,
        .priority = 0,
        .trigger = cm_resize_room_trigger,
        .update = cm_resize_room_update,
        .status_line = cm_resize_room_status_line,
        .cancel = NULL
    },
};

/// BEGIN EDITOR UI ///

// edit flags, pass to "flags" of "edit_*"
enum {
    EF_NONE                 = 0,
    EF_NEW                  = 1 << 0,
    EF_SELECTED             = 1 << 1,
    EF_VERTEX_LIST_WALLS    = 1 << 2,
};

static bool edit_light_params(
        light_params_t *l,
        lptr_t parent,
        plane_type_e parent_plane) {
    bool change = false;
    igPushID_Ptr(l);

    change |=
        igColorEdit3(
            "##color", l->color.raw,
            ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoInputs);

    igSetNextItemWidth(64);
    sameline();
    change |=
        igSliderFloat("AMB", &l->ambient, 0.0f, 1.0f, "%.2f", 0);

    igSetNextItemWidth(89);
    sameline();
    change |=
        input_f32_clamped(
            "PW", &l->power, 0.01f, 0.05f, "%.2f", 0, -100.0f, 100.0f);

    sameline();
    if (igButton("ZERO", (ImVec2) { 0, 0 })) {
        memset(l, 0, sizeof(*l));
        change = true;
    }

    sameline();
    const lptr_t copy_ptr =
        button_select_lt(
            "CPY##lp",
            LTF_SECTOR | LTF_SIDE | LTF_ENTITY,
            BUTTON_SELECT_LT_NONE);

    if (!lptr_is_null(copy_ptr)) {
        const light_params_t *copy_src = NULL;

        if (lptr_is(copy_ptr, LT_SIDE)) {
            copy_src = &lptr_side(ed->level, copy_ptr)->light;
        } else if (lptr_is(copy_ptr, LTF_SECTOR)) {
            const sector_t *copy_sect = lptr_sector(ed->level, copy_ptr);

            if (lptr_matches(parent, LT_SIDE)) {
                copy_src =
                    &copy_sect->planes[
                        v3_eqv_eps(
                            copy_sect->planes[0].light.color, v3_of(0)) ? 1 : 0]
                        .light;
            } else {
                copy_src = &copy_sect->planes[parent_plane].light;
            }
        } else if (lptr_is(copy_ptr, LT_ENTITY)) {
            const entity_t *copy_ent = lptr_entity(ed->level, copy_ptr);

            if (copy_ent->itype == ENTITY_TYPE_FILL_LIGHT) {
                copy_src = &copy_ent->light;
            }
        }

        if (copy_src) {
            *l = *copy_src;
            change = true;
        }
    }

    sameline();
    const lptr_t apply_ptr =
        button_select_lt(
            "APL##lp",
            LTF_SECTOR | LTF_SIDE,
            BUTTON_SELECT_LT_ALLOW_MULTI);

    if (!lptr_is_null(apply_ptr)) {
        editor_mark_ptr_change(apply_ptr);

        light_params_t *apply_dst = NULL;

        if (lptr_matches(apply_ptr, LTF_SIDE)) {
            apply_dst = &lptr_side(ed->level, apply_ptr)->light;
        } else if (lptr_matches(apply_ptr, LTF_SECTOR)) {
            sector_t *apply_sect = lptr_sector(ed->level, apply_ptr);

            if (lptr_matches(parent, LTF_SIDE)) {
                apply_dst =
                    &apply_sect->planes[
                        v3_eqv_eps(
                            apply_sect->planes[0].light.color,
                            v3_of(0)) ? 1 : 0].light;
            } else {
                apply_dst = &apply_sect->planes[parent_plane].light;
            }
        }

        if (apply_dst) {
            *apply_dst = *l;
            lptr_recalculate(ed->level, apply_ptr);
        }
    }

    igPushItemWidth(90);
    change |=
        input_f32_clamped(
            "ATT", &l->attenuation, 1.0f, 5.0f, "%.2f", 0, 0, 1e10f);
    sameline();
    change |=
        input_f32_clamped(
            "C1", &l->c1, 0.1f, 1.0f, "%.2f", 0, 0, 1e10f);
    sameline();
    change |=
        input_f32_clamped(
            "C2", &l->c2, 0.1f, 1.0f, "%.2f", 0, 0, 1e10f);
    igPopItemWidth();

    igSetNextItemWidth(90);
    change |=
        input_f32_clamped(
            "Z_A", &l->z_attenuation, 0.1f, 1.0f, "%.2f", 0, 0, 1e10f);

    sameline();
    input_checkbox_bit("NO SHDW", &l->flags, ctz(LIGHT_FLAG_NO_SHADOWS));
    sameline();
    input_checkbox_bit("IGN", &l->flags, ctz(LIGHT_FLAG_IGNORE_NEAR));
    sameline();
    input_checkbox_bit("OFF", &l->flags, ctz(LIGHT_FLAG_DISABLE));

    sameline();
    igBeginDisabled(l->flags & LIGHT_FLAG_NO_SHADOWS);
    igButton("SM...", (ImVec2) {});
    if (igIsItemHovered(ImGuiHoveredFlags_None)) {
        igBeginTooltip();

        // get lights associated with parent
        DYNLIST(light_info_t*) lights =
            dynlist_create(light_info_t*, &g->frame_arena);

        renderer_lights_for(parent, &lights);

        dynlist_each(lights, it) {
            light_info_t *info = *it.el;
            igText("LIGHT 0x%08x", info->desc.id);

            if (info->shadow.index == -1) {
                igText("  (no shadow map present)");
                continue;
            }


            const int
                vx = info->shadow.index % SHADOW_MAP_SIZE_LIGHTS,
                vy = info->shadow.index / SHADOW_MAP_SIZE_LIGHTS;

            const f32 uv_scale = 1.0f / SHADOW_MAP_SIZE;
            const box2f_t uv =
                box2f_ps(
                    v2_of(
                        (vx * SHADOW_MAP_PER_LIGHT) * uv_scale,
                        (vy * SHADOW_MAP_PER_LIGHT) * uv_scale),
                    v2_of(SHADOW_MAP_PER_LIGHT * uv_scale));

            igImage(
                (ImTextureID) (uintptr_t) g_passes.shadow.image.id,
                (ImVec2) { 384, 384 },
                (ImVec2) { uv.min.x, uv.max.y, },
                (ImVec2) { uv.max.x, uv.min.y },
                (ImVec4) { 1, 1, 1, 1 },
                (ImVec4) { 1, 1, 1, 1 });
        }

        igEndTooltip();
    }
    igEndDisabled();

    igPopID();
    return change;
}

static bool edit_vertex(
    vertex_t *v,
    int flags) {
    bool change = false;
    igPushID_Ptr(v);
    igPushItemWidth(INPUT_WIDTH_INT);
    {
        change |= igInputFloat("##x", &v->pos.x, 0.25f, 1.0f, "%.3f", 0);
        sameline();
        change |= igInputFloat("##y", &v->pos.y, 0.25f, 1.0f, "%.3f", 0);
    }
    igPopItemWidth();
    igPopID();

    if (flags & EF_VERTEX_LIST_WALLS) {
        if (igTreeNode_Str("WALLS")) {
            dynlist_each(v->walls, it) {
                wall_t *wall = *it.el;
                igPushID_Ptr(wall);

                igAlignTextToFramePadding();
                igText("%d", wall->id);

                sameline();
                if (igButton("EDIT", (ImVec2) { 0, 0 })) {
                    editor_open(lptr_from(wall));
                }

                sameline();
                if (igButton("SEL", (ImVec2) { 0, 0 })) {
                    try_select_ptr(lptr_from(wall));
                }

                igPopID();
            }

            igTreePop();
        }
    }

    if (change) {
        editor_mark_ptr_change(lptr_from(v));
        vertex_recalculate(ed->level, v);
    }

    return change;
}

static bool edit_sidemat_data(
        side_t *side,
        sidemat_data_t *data,
        int flags) {
    igPushID_Ptr(data);
    bool change = false;

    igBeginDisabled(side && side_get_like(side));
    {
        for (int i = 0; i < ARRLEN(data->texs); i++) {
            const char *label = ((const char*[3]){ "slow", "smid", "shigh" })[i];
            if (i != 0) { sameline(); }
            change |=
                texture_select(label, &data->texs[i], TEXTURE_SELECT_PICKER);
        }

        sameline();
        change |=
            input_button_hsva_offsets("##hsva", &data->hsva, INPUT_HSVA_PICKER);
    }
    igEndDisabled();

    igSpacing();
    change |=
        texture_select(
            "overlay", &data->tex_overlay, TEXTURE_SELECT_PICKER);

    sameline();
    igSetNextItemWidth(125.0f);
    change |=
        igSliderFloat("##alpha", &data->overlay_alpha, -1.0f, 1.0f, "%.3f", 0);

    sameline();
    if (igButton("ZERO", (ImVec2) {})) {
        data->overlay_alpha = 0.0f;
        change = true;
    }

    sameline();
    igAlignTextToFramePadding();
    igText("ALPHA");

    igSpacing();
    igPushItemWidth(INPUT_WIDTH_INT);
    igBeginDisabled(side && side_get_like(side));
    {
        igText("SPLITS ");
        sameline();
        change |=
            igInputFloat("##sb", &data->split_bottom, 0.1f, 0.25f, "%.3f", 0);
        sameline();
        change |=
            igInputFloat("##st", &data->split_top, 0.1f, 0.25f, "%.3f", 0);

        sameline();
        if (igButton("ZERO##splits", (ImVec2) {})) {
            data->split_bottom = 0.0f;
            data->split_top = 0.0f;
            change = true;
        }

        igText("OFFSETS");
        sameline();
        change |=
            igInputInt("##ox", &data->offsets.x, 1, 32, 0);
        sameline();
        change |=
            igInputInt("##oy", &data->offsets.y, 1, 32, 0);

        sameline();
        if (igButton("ZERO##offs", (ImVec2) {})) {
            data->offsets = v2i_of(0);
            change = true;
        }
    }
    igEndDisabled();
    igPopItemWidth();
    igPushItemWidth(80);
    {
        change |=
            input_flags(
                &data->flags,
                (SDMF_TRUE_COLOR << 1) - 1,
                // must correspond with definitions in shared_defs.h
                ((const char *[]) {
                    "B_ABS",
                    "T_ABS",
                    "EZPORT",
                    "PEG",
                    "MNS",
                    "MID",
                    "SC_H",
                    "SC_V",
                    "SKY",
                    "EZX",
                    "OV_SC_H",
                    "OV_SC_V",
                    "OV_SCRY",
                    "T_COL",
                }),
                ((const int []) { 4, 4, 4, 2 }), 4);
    }
    igPopItemWidth();

    igPopID();
    return change;
}

static bool edit_side(
        side_t *side,
        wall_t *wall,
        int flags) {
    igPushID_Ptr(side);
    bool change = false;

    const ImVec4 color =
        (flags & EF_SELECTED) ?
            (ImVec4) { 1, 1, 0.2, 1 }
            : (ImVec4) { 1, 1, 1, 1 };

    if (!side) {
        igAlignTextToFramePadding();
        igTextColored(color, "%s", "SIDE");

        sameline();
        if (igButton("CREATE##side", (ImVec2) { 0, 0 })) {
            ASSERT(!wall->sides[0] || !wall->sides[1]);

            side =
                side_new(
                    ed->level,
                    wall->sides[0] ? wall->sides[1] : wall->sides[0]);

            wall_set_side(ed->level, wall, wall->sides[0] ? 1 : 0, side);
            change = true;
        }

        goto done;
    }

    igAlignTextToFramePadding();
    igTextColored(
        color,
        "SIDE (%d)%s",
        side->id,
        (flags & EF_SELECTED) ? " (SELECTED)" : "");

    if (igButton("SEL", (ImVec2) { 0, 0 })) {
        try_select_ptr(lptr_from(side));
    }

    sameline();
    const lptr_t copy_ptr =
        button_select_lt(
            "CPY##pr",
            LTF_SIDE,
            BUTTON_SELECT_LT_NONE);

    side_t *copy_sect;
    if ((copy_sect = lptr_side(ed->level, copy_ptr))) {
        side_copy_props(ed->level, side, copy_sect);
        change = true;
    }

    sameline();
    const lptr_t apply_ptr =
        button_select_lt("APL##pr", LTF_SIDE, BUTTON_SELECT_LT_ALLOW_MULTI);

    side_t *apply_sect;
    if ((apply_sect = lptr_side(ed->level, apply_ptr))) {
        editor_mark_ptr_change(apply_ptr);
        side_copy_props(ed->level, apply_sect, side);
        side_recalculate(ed->level, apply_sect);
    }

    // "like" side
    sameline();
    igBeginGroup();
    {
        igAlignTextToFramePadding();
        igBeginDisabled(!side->like);
        if (igButton(
                mem_strfmt(
                    tlscratch(),
                    "LIKE: %d###like_id",
                    side->like ? side->like->id : -1),
                (ImVec2) {})) {
            editor_open(lptr_from(side->like));
        }
        igEndDisabled();

        sameline();

        const lptr_t like_ptr =
            button_select_lt("SEL", LTF_SIDE, BUTTON_SELECT_LT_NONE);
        if (igIsItemHovered(0)) { ed->highlight.ptr = lptr_from(side->like); }

        side_t *like_side;
        if ((like_side = lptr_side(ed->level, like_ptr))) {
            if (side_try_set_like(ed->level, side, like_side)) {
                change = true;
            } else {
                ERROR("error: like loop");
            }
        }

        sameline();
        if (igButton("CLR##like", (ImVec2) { 0 })) {
            side->like = NULL;
            change = true;
        }
    
        sameline();
        const lptr_t like_copy_ptr = button_select_lt("CPY##like", LTF_SIDE, 0);
        const side_t *like_copy_side;
        if ((like_copy_side = lptr_side(ed->level, like_copy_ptr))) {
            side_try_set_like(ed->level, side, like_copy_side->like);
            change = true;
        }

        sameline();
        const lptr_t like_apply_ptr =
            button_select_lt(
                "APL##like", LTF_SIDE, BUTTON_SELECT_LT_ALLOW_MULTI);

        side_t *like_apply_side;
        if ((like_apply_side = lptr_side(ed->level, like_apply_ptr))) {
            if (side_try_set_like(
                    ed->level,
                    like_apply_side,
                    side->like ? side->like : side)) {
                like_apply_side->version++;
                editor_mark_ptr_change(like_apply_ptr);
            } else {
                ERROR("error: like loop");
            }
        }
    }
    igEndGroup();

    igSeparator();

    if ((flags & EF_NEW) && (flags & EF_SELECTED)) {
        igTreeNodeSetOpen(igGetID_Str("PROPS"), true);
    }

    if (igTreeNode_Str("PROPS")) {
        igPushID_Str("sector");
        igBeginGroup();
        {
            sector_t *sect = side->sector;

            igAlignTextToFramePadding();
            igText(lptr_to_str(ed->level, lptr_from(sect), &g->frame_arena));

            sameline();
            if (igButton("EDIT", (ImVec2) { 0, 0 })) {
                editor_open(lptr_from(sect));
            }

            if (sect != side->sector) {
                if (side->sector) {
                    sector_remove_side(ed->level, side->sector, side);
                }

                if (sect) {
                    sector_add_side(ed->level, sect, side);
                }

                change = true;
            }
        }
        igEndGroup();
        igPopID();

        // portal
        igPushID_Str("portal");
        igBeginGroup();
        {
            side_t *portal = side->portal;

            const lptr_t new_ptr =
                input_lptr("PORTAL", LTF_SIDE, lptr_from(side->portal), 0);
            if (!lptr_is_null(new_ptr)) {
                portal = lptr_side(ed->level, new_ptr);
            }

            sameline();
            if (igButton("MAKE", (ImVec2) { 0, 0 })) {
                side_t *other = side_other(side);

                // if no other side, make it
                if (!other) {
                    other = side_new(ed->level, side);
                    wall_set_side(
                        ed->level, wall, side == wall->sides[0] ? 1 : 0, other);
                    other->portal = side;
                    level_push_dirty_sect_side(ed->level, other);
                }

                portal = other;
            }

            sameline();
            if (igButton("DEL", (ImVec2) { 0, 0 })) {
                portal = NULL;
            }

            if (portal != side->portal) {
                side_t *old_portal = side->portal;
                side->portal = portal;
                if (old_portal) {
                    side_recalculate(ed->level, old_portal);
                }
                change = true;
            }
        }
        igEndGroup();
        igPopID();

        igTreePop();
    }

    if (igTreeNodeEx_Str(
            "LIGHT",
            !v3_eqv_eps(side->light.color, v3_of(0)) ?
                ImGuiTreeNodeFlags_DefaultOpen
                : 0)) {
        change |= edit_light_params(&side->light, lptr_from(side), 0);
        igTreePop();
    }

    if ((flags & EF_NEW) && (flags & EF_SELECTED)) {
        igTreeNodeSetOpen(igGetID_Str("MAT"), true);
    }

    igAlignTextToFramePadding();
    const bool mat_treenode = igTreeNodeEx_Str("MAT", ImGuiTreeNodeFlags_AllowOverlap);

    sameline();
    const side_t *other = side_other(side);
    sameline();
    igBeginDisabled(!other);
    if (igButton("SAME", (ImVec2) { 0, 0 })) {
        side->mat = other->mat;
        change = true;
    }
    igEndDisabled();

    // copy function
    sameline();
    lptr_t copy =
        button_select_lt("CPY##cs", LTF_SIDE, BUTTON_SELECT_LT_NONE);
    if (!lptr_is_null(copy)) {
        side_t *copy_side = lptr_side(ed->level, copy);
        side->mat = copy_side->mat;
        side->flags = copy_side->flags;
        side->mat = copy_side->mat;
        change = true;
    }

    // apply function
    sameline();
    lptr_t apply =
        button_select_lt("APL##cs", LTF_SIDE, BUTTON_SELECT_LT_ALLOW_MULTI);
    if (!lptr_is_null(apply)) {
        editor_mark_ptr_change(apply);
        side_t *apply_side = lptr_side(ed->level, apply);
        apply_side->mat = side->mat;
        apply_side->flags = side->flags;
        apply_side->mat = side->mat;
        side_recalculate(ed->level, apply_side);
        change = true;
    }

    if (mat_treenode) {
        change |= edit_sidemat_data(side, &side->mat, EF_NONE);
        igTreePop();
    }
done:
    igPopID();

    if (side && change) {
        side_recalculate(ed->level, side);
    }

    return change;
}

static bool edit_wall(
        wall_t *wall,
        side_t *side,
        int flags) {
    bool change = false;

    igPushID_Ptr(wall);
    igBeginGroup();

    igAlignTextToFramePadding();
    igText("%s", lptr_to_str(ed->level, lptr_from(wall), &g->frame_arena));

    igAlignTextToFramePadding();
    igText("%s: A", lptr_to_str(ed->level, lptr_from(wall->v0), &g->frame_arena));
    sameline();
    change |= edit_vertex(wall->v0, flags);

    igAlignTextToFramePadding();
    igText("%s: B", lptr_to_str(ed->level, lptr_from(wall->v1), &g->frame_arena));
    sameline();
    change |= edit_vertex(wall->v1, flags);

    bool side_change[2] = { false, false };
    igSeparator();
    side_change[0] =
        edit_side(
            wall->sides[0],
            wall,
            flags | ((side && side == wall->sides[0]) ? EF_SELECTED : EF_NONE));
    igSeparator();
    side_change[1] =
        edit_side(
            wall->sides[1],
            wall,
            flags | ((side && side == wall->sides[1]) ? EF_SELECTED : EF_NONE));
    igEndGroup();
    igPopID();

    for (int i = 0; i < 2; i++) {
        if (!wall->sides[i]) { continue; }

        change |= side_change[i];

        if (side_change[i]) {
            level_each(side_t, &ed->level->sides, it) {
                const side_t *like = side_get_like(it.el);

                if (like == wall->sides[i]) {
                    it.el->version++;
                }
            }
        }
    }

    if (change) {
        wall_recalculate(ed->level, wall);
    }

    return change;
}

// ZERO and SAME buttons for sectmat each-plane data
// "x" is x location, "i" is plane index, p/size are data
static bool edit_sectmat_data__zero_same(
    int x,
    int i,
    const range_t *range) {
    bool change = false;

    igPushID_Ptr(range->ptr);

    igSameLine(x, -1);
    if (igButton("SAME", (ImVec2) { 0, 0 })) {
        void *other = range->ptr + (i == 1 ? -range->size : +range->size);
        memcpy(range->ptr, other, range->size);
        change = true;
    }

    sameline();
    if (igButton("ZERO", (ImVec2) { 0, 0 })) {
        memset(range->ptr, 0, range->size);
        change = true;
    }

    igPopID();

    return change;
}

static bool edit_plane(
        plane_t *plane,
        plane_type_e type,
        sector_t *sector,
        int flags) {
    igPushID_Ptr(plane);
    igBeginGroup();
    bool change = false;

    char label[64];
    snprintf(
        label, sizeof(label),
        "%s",
        type == PLANE_TYPE_CEIL ? "CEIL" : "FLOOR");

    igSetNextItemWidth(INPUT_WIDTH_INT);
    change |= igInputFloat("##z", &plane->z, 0.25f, 1.0f, "%.3f", 0);
    sameline();
    if (igButton("SNAP", (ImVec2) { 0, 0 })) {
        plane->z = roundf(plane->z / 0.125f) * 0.125f;
        change = true;
    }
    sameline();
    change |=
        input_copy_apply(
            "",
            lptr_from(sector),
            &plane->z,
            sizeof(plane->z),
            (level_type_e[]) { LT_SECTOR },
            (int[]) { ((u8*) &plane->z) - ((u8*) sector) },
            1);

    sameline();
    igText("%s", label);

    igEndGroup();
    igPopID();
    return change;
}

static bool edit_sector(
    sector_t *sect,
    int flags) {
    bool change = false;

    igPushID_Ptr(sect);
    igBeginGroup();

    if (igButton("SEL", (ImVec2) { 0, 0 })) {
        try_select_ptr(lptr_from(sect));
    }

    sameline();
    if (igButton("DEL", (ImVec2) { 0, 0 })) {
        sector_delete(ed->level, sect);
        goto done;
    }

    sameline();
    const lptr_t copy_ptr =
        button_select_lt("CPY##pr", LTF_SECTOR, BUTTON_SELECT_LT_NONE);

    sector_t *copy_sect;
    if ((copy_sect = lptr_sector(ed->level, copy_ptr))) {
        sector_copy_props(ed->level, sect, copy_sect);
        change = true;
    }

    sameline();
    const lptr_t apply_ptr =
        button_select_lt("APL##pr", LTF_SECTOR, BUTTON_SELECT_LT_ALLOW_MULTI);

    sector_t *apply_sect;
    if ((apply_sect = lptr_sector(ed->level, apply_ptr))) {
        editor_mark_ptr_change(apply_ptr);
        sector_copy_props(ed->level, apply_sect, sect);
        sector_recalculate(ed->level, apply_sect);
    }

    sameline();

    // "like" sector
    igBeginGroup();
    {
        igAlignTextToFramePadding();

        igBeginDisabled(!sect->like);
        if (igButton(
                mem_strfmt(
                    tlscratch(),
                    "LIKE: %d###like_id",
                    sect->like ? sect->like->id : -1),
                (ImVec2) {})) {
            editor_open(lptr_from(sect->like));
        }
        igEndDisabled();

        sameline();

        const lptr_t like_ptr =
            button_select_lt("SEL", LTF_SECTOR, BUTTON_SELECT_LT_NONE);
        if (igIsItemHovered(0)) { ed->highlight.ptr = lptr_from(sect->like); }

        sector_t *like_sect;
        if ((like_sect = lptr_sector(ed->level, like_ptr))) {
            if (sector_try_set_like(ed->level, sect, like_sect)) {
                change = true;
            } else {
                ERROR("error: like loop");
            }
        }

        sameline();
        if (igButton("CLR##like", (ImVec2) {})) {
            sect->like = NULL;
            change = true;
        }

        sameline();
        const lptr_t like_copy_ptr =
            button_select_lt("CPY##like", LTF_SECTOR, 0);
        const sector_t *like_copy_sect;
        if ((like_copy_sect = lptr_sector(ed->level, like_copy_ptr))) {
            sector_try_set_like(ed->level, sect, like_copy_sect->like);
            change = true;
        }

        sameline();
        const lptr_t like_apply_ptr =
            button_select_lt(
                "APL##like", LTF_SECTOR, BUTTON_SELECT_LT_ALLOW_MULTI);

        sector_t *like_apply_sect;
        if ((like_apply_sect = lptr_sector(ed->level, like_apply_ptr))) {
            if (sector_try_set_like(
                    ed->level,
                    like_apply_sect,
                    sect->like ? sect->like : sect)) {
                like_apply_sect->version++;
                editor_mark_ptr_change(like_apply_ptr);
            } else {
                ERROR("error: like loop");
            }
        }
    }
    igEndGroup();

    igSeparator();

    // flags
    // change |= input_checkbox_bit("SKY", sect, bitoffsetof(sector_t, is_sky));
    // igSeparator();

    change |= input_enum("TYPE", &sect->type, sector_type_desc());

    if (igTreeNodeEx_Str(
            "DATA",
            sect->type != SECTOR_TYPE_DEFAULT ?
                ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        igSetNextItemWidth(180);
        change |=
            igInputFloat(
                "LIQUID HEIGHT", &sect->liquid_offset, 0.25f, 1.0f, "%.3f", 0);

        change |= input_button_hsv_offsets("LIQUID HSV", &sect->liquid_hsv);

        igSameLine(204.0f, 0.0f);
        igAlignTextToFramePadding();
        igText("LIQUID HSV");

        static char buf[256];
        snprintf(
            buf,
            sizeof(buf),
            "%s",
            sect->teleport_exit_level ? sect->teleport_exit_level : "");

        igSetNextItemWidth(180);
        if (igInputText("TP LEVEL", buf, sizeof(buf), 0, NULL, NULL)) {
            if (sect->teleport_exit_level) {
                mem_free(&ed->level->arena, sect->teleport_exit_level);
                sect->teleport_exit_level = NULL;
            }

            if (buf[0]) {
                sect->teleport_exit_level = mem_strdup(&ed->level->arena, buf);
            }
        }

        snprintf(
            buf,
            sizeof(buf),
            "%s",
            sect->approach_name ? sect->approach_name : "");

        igSetNextItemWidth(180);
        if (igInputText("APPROACH NAME", buf, sizeof(buf), 0, NULL, NULL)) {
            if (sect->approach_name) {
                mem_free(&ed->level->arena, sect->approach_name);
                sect->approach_name = NULL;
            }

            if (buf[0]) {
                sect->approach_name = mem_strdup(&ed->level->arena, buf);
            }
        }

        igTreePop();
    }

    igSeparator();

    igText("PLANES");
    igSpacing();
    change |= edit_plane(&sect->floor, PLANE_TYPE_FLOOR, sect, flags);
    change |= edit_plane(&sect->ceil, PLANE_TYPE_CEIL, sect, flags);
    igSeparator();

    igBeginGroup();
    if (igTreeNode_Str("SLOPES")) {
        for (int i = 0; i < 2; i++) {
            const f32 slope_unit = PI / 64.0f;

            igBeginGroup();
            igPushID_Int(i);

            lptr_t slope_side =
                input_lptr(
                    "SIDE", LTF_SIDE,
                    lptr_from(sect->planes[i].slope_side), 0);

            if (!lptr_is_null(slope_side)) {
                side_t *ss = lptr_side(ed->level, slope_side);

                // try the other side
                if (ss->sector != sect) {
                    ss = side_other(ss);
                }

                if (!ss || ss->sector != sect) {
                    show_editor_error(
                        "slope side must be in parent sector");
                } else {
                    sect->planes[i].slope_side = ss;
                    change = true;
                }
            }

            sameline();
            if (igButton("CLR", (ImVec2) { 0, 0 })) {
                sect->planes[i].slope_side = NULL;
                change = true;
            }

            sameline();
            if (igButton("SAME", (ImVec2) { 0, 0 })) {
                sect->planes[i].slope_side = sect->planes[1 - i].slope_side;
                change = true;
            }

            igSetNextItemWidth(INPUT_WIDTH_INT);
            change |=
                input_f32_clamped(
                    "##slope",
                    &sect->planes[i].slope,
                    slope_unit,
                    slope_unit * 2.0f,
                    "%.3f",
                    0,
                    -PI_2, PI_2);

            sameline();
            if (igButton("SNAP", (ImVec2) { 0, 0 })) {
                sect->planes[i].slope =
                    clamp(
                        roundf(sect->planes[i].slope / slope_unit)
                            * slope_unit,
                        -PI_2, PI_2);
                change = true;
            }

            sameline();
            if (igButton("ZERO", (ImVec2) { 0, 0 })) {
                sect->planes[i].slope = 0.0f;
                change = true;
            }

            sameline();
            lptr_t reach_sector_ptr =
                button_select_lt("REACH", LTF_SECTOR, BUTTON_SELECT_LT_NONE);
            if (!lptr_is_null(reach_sector_ptr)
                && sect->planes[i].slope_side
                && lptr_sector(ed->level, reach_sector_ptr) != sect) {
                sector_t *reach_sector =
                    lptr_sector(ed->level, reach_sector_ptr);

                // find shared side
                DYNLIST(wall_t*) shared_walls =
                    dynlist_create(wall_t*, &g->frame_arena);
                sector_shared_walls(sect, reach_sector, &shared_walls);

                // pick the best wall: should be most opposite slope_side
                wall_t *wall = NULL;
                f32 wall_dot = -1.0f;

                dynlist_each(shared_walls, it) {
                    const v2 dir =
                        v2_normalize(
                            v2_sub(
                                wall_midpoint(*it.el),
                                wall_midpoint(sect->planes[i].slope_side->wall)));

                    const f32 dot =
                        v2_dot(
                            side_normal(sect->planes[i].slope_side),
                            dir);

                    if (dot > wall_dot) { wall = *it.el; wall_dot = dot; }
                }

                if (wall) {
                    // calculate our slope such that when the plane hits the
                    // shared wall, it has the same z as the other sector there

                    // using tan(theta) = dx/dy
                    // dx is the distance between two points on the the slope
                    // side and the wall
                    // dy is the desired height change from our plane to the
                    // target

                    wall_t *wall = shared_walls[0];
                    const f32
                        z_target =
                            sector_point_zs(
                                reach_sector,
                                wall_midpoint(wall))
                                    .zs[i],
                        z_base = sect->planes[i].z;

                    // use distance on greatest axis as dx
                    // this doesn't work for non-colinear walls, but why else
                    // would you use the reach tool?
                    const v2
                        p0 = wall_midpoint(wall),
                        p1 = wall_midpoint(sect->planes[i].slope_side->wall);

                    const f32
                        pdx = fabsf(p1.x - p0.x),
                        pdy = fabsf(p1.y - p0.y),
                        dx = max(pdx, pdy);

                    const f32 dy = z_target - z_base;

                    sect->planes[i].slope =
                        clamp(
                            ifnaninf(atanf(dy / dx), 0, 0),
                            -PI, PI);
                }

                dynlist_destroy(shared_walls);
                change = true;
            }

            sameline();
            if (igButton("SAME", (ImVec2) { 0, 0 })) {
                sect->planes[i].slope = sect->planes[1 - i].slope;
                change = true;
            }

            sameline();
            change |=
                input_copy_apply(
                    "",
                    lptr_from(sect),
                    &sect->planes[i].slope,
                    sizeof(sect->planes[i].slope),
                    (level_type_e[]) { LT_SECTOR },
                    (int[]) {
                        i == 0 ?
                            offsetof(sector_t, planes[0].slope)
                            : offsetof(sector_t, planes[1].slope)
                    },
                    1);

            sameline();
            igText(i == 0 ? "FLOOR" : "CEIL");
            igPopID();
            igEndGroup();
        }

        igTreePop();
    } // slopes
    igEndGroup();

    igSeparator();

    if (igTreeNodeEx_Str(
            "LIGHTS",
            !v3_eqv_eps(sect->floor.light.color, v3_of(0))
                || !v3_eqv_eps(sect->ceil.light.color, v3_of(0)) ?
                ImGuiTreeNodeFlags_DefaultOpen
                : 0)) {
        change |=
            edit_light_params(
                &sect->planes[PLANE_TYPE_FLOOR].light,
                lptr_from(sect), PLANE_TYPE_FLOOR);
        change |=
            edit_light_params(
                &sect->planes[PLANE_TYPE_CEIL].light,
                lptr_from(sect), PLANE_TYPE_CEIL);
        igTreePop();
    }

    igSeparator();

    igAlignTextToFramePadding();
    const bool mat_treenode =
        igTreeNodeEx_Str("MAT", ImGuiTreeNodeFlags_AllowOverlap);

    // mat copy
    sameline();
    lptr_t copy =
        button_select_lt("CPY##sm", LTF_SECTOR, BUTTON_SELECT_LT_NONE);
    if (!lptr_is_null(copy)) {
        sector_t *copy_sector = lptr_sector(ed->level, copy);
        sect->mat = copy_sector->mat;
        sect->flags = copy_sector->flags;
        change = true;
    }

    // mat apply
    sameline();
    lptr_t mat_apply_ptr =
        button_select_lt(
            "APL##sm",
            LTF_SECTOR,
            BUTTON_SELECT_LT_ALLOW_MULTI);
    if (!lptr_is_null(mat_apply_ptr)) {
        editor_mark_ptr_change(mat_apply_ptr);
        sector_t *mat_apply_sect = lptr_sector(ed->level, mat_apply_ptr);
        mat_apply_sect->mat = sect->mat;
        mat_apply_sect->flags = sect->flags;
        sector_recalculate(ed->level, mat_apply_sect);
    }

    if (mat_treenode) {
        igBeginGroup();
        igSpacing();

        sectmat_data_t *mat = &sect->mat;
        bool sectmat_change = false;

        const bool has_like = sect && !!sector_get_like(sect);

        for (int i = 0; i < 2; i++) {
            if (i != 0) { igSpacing(); }

            igPushID_Int(i);
            igPushItemWidth(INPUT_WIDTH_INT);
            {
                const int btn_x = 294;

                igBeginDisabled(has_like);
                {
                    igAlignTextToFramePadding();
                    igText("TEX    ");
                    sameline();
                    sectmat_change |=
                        texture_select(
                            "tex", &mat->texs[i], TEXTURE_SELECT_PICKER);

                    sameline();
                    sectmat_change |=
                        input_button_hsva_offsets(
                            "##hsva", &mat->hsva[i], INPUT_HSVA_PICKER);

                    sectmat_change |=
                        edit_sectmat_data__zero_same(
                            btn_x, i, RANGE_REF(mat->hsva[i]));
                }
                igEndDisabled();

                igAlignTextToFramePadding();
                igText("OVERLAY");
                sameline();
                change |=
                    texture_select(
                        "overlay", &mat->overlays[i], TEXTURE_SELECT_PICKER);

                sameline();
                igSetNextItemWidth(135.0f);
                sectmat_change |=
                    igSliderFloat(
                        "##alpha",
                        &mat->overlay_alphas[i],
                        -1.0f,
                        1.0f,
                        "%.3f",
                        0);

                sectmat_change |=
                    edit_sectmat_data__zero_same(
                        btn_x, i,
                        RANGE_REF(mat->overlay_alphas[i]));

                igBeginDisabled(has_like);
                {
                    igAlignTextToFramePadding();
                    igText("OFFSETS");
                    sameline();
                    sectmat_change |=
                        igInputInt("##ox", &mat->offsets[i].x, 1, 32, 0);
                    sameline();
                    sectmat_change |=
                        igInputInt("##oy", &mat->offsets[i].y, 1, 32, 0);

                    sectmat_change |=
                        edit_sectmat_data__zero_same(
                            btn_x, i, RANGE_REF(mat->offsets[i]));
                }
                igEndDisabled();
            }
            igPopItemWidth();
            igPopID();
        }

        igAlignTextToFramePadding();
        igText("VIS/FOG");

        igPushItemWidth(80);
        {
            change |=
                input_flags(
                    &mat->flags,
                    (SCMF_SCROLL_INV << 1) - 1,
                    // must correspond with definitions in shared_defs.h
                    ((const char*[]) {
                        "SKY",
                        "SC_H",
                        "SC_V",
                        "OV_SC_H",
                        "OV_SC_V",
                        "SC_INV",
                    }),
                    ((const int []) { 3, 3 }), 2);
        }
        igPopItemWidth();

        if (sectmat_change) {
            // TODO: same for sides
            level_each(sector_t, &ed->level->sectors, it) {
                if (it.el->like == sect) {
                    it.el->version++;
                }
            }
        }

        change |= sectmat_change;
        igEndGroup();
        igTreePop();
    }

    igSeparator();

    if (igTreeNode_Str("SIDES")) {
        ImVec2 avail;
        igGetContentRegionAvail(&avail);
        igBeginChild_Str(
            "SIDES##child",
            (ImVec2){ 256, 128 },
            false,
            ImGuiWindowFlags_None);

        llist_each(sector_sides, &sect->sides, it) {
            side_t
                *side = it.el,
                *sides[2] = { side, side_other(side) };

            igBeginGroup();
            igPushID_Ptr(side);
            igAlignTextToFramePadding();
            igText("%d", side->id);
            igSameLine(60, -1);
            for (int i = 0; i < 2; i++) {
                if (i != 0) { sameline(); }
                igBeginGroup();
                igBeginDisabled(!sides[i]);
                sameline();
                if (igButton(
                        i == 0 ? "EDT" : "EDT (O)", (ImVec2) { 0, 0 })) {
                    editor_open(lptr_from(sides[i]));
                }

                sameline();
                if (igButton(
                        i == 0 ? "SEL" : "SEL (O)", (ImVec2) { 0, 0 })) {
                    try_select_ptr(lptr_from(sides[i]));
                }
                igEndDisabled();
                igEndGroup();

                if (sides[i] && igIsItemHovered(ImGuiHoveredFlags_None)) {
                    ed->highlight.ptr = lptr_from(sides[i]);
                }
            }
            igEndGroup();
            igPopID();
        }

        igEndChild();
        igTreePop();
    }

    if (change) {
        sector_recalculate(ed->level, sect);
    }

done:
    igEndGroup();
    igPopID();

    return change;
}

static bool edit_decal(decal_t *decal, int flags) {
    igPushID_Ptr(decal);
    igBeginGroup();
    bool change = false;

    if (igButton("SEL", (ImVec2) { 0, 0 })) {
        try_select_ptr(lptr_from(decal));
    }

    sameline();
    if (igButton("DEL", (ImVec2) { 0, 0 })) {
        *dynlist_push(ed->level->delete_ptrs) = lptr_from(decal);
        goto done;
    }

    sameline();
    if (igButton("CLONE", (ImVec2) { 0, 0 })) {
        ed->cur.mode = CM_DECAL;
        ed->cur.mode_decal.clone = lptr_from(decal);
    }

    change |= texture_select("TEXTURE", &decal->tex, TEXTURE_SELECT_NONE);

    sameline();
    decal_type_e type = decal->type;
    if (input_enum("TYPE", &type, decal_type_desc())) {
        decal_set_type(ed->level, decal, type);
        change = true;
    }

    igSpacing();

    igPushItemWidth(INPUT_WIDTH_INT);
    {
        igText("TEX OFFSETS");
        sameline();
        change |= igInputInt("##tx", &decal->tex_offsets.x, 1, 16, 0);
        sameline();
        change |= igInputInt("##ty", &decal->tex_offsets.y, 1, 16, 0);

        igSpacing();

        const char *pos_title;
        v2 *pos;
        if (decal->is_on_side) {
            pos_title = "POS OFFSETS";
            pos = &decal->side.offsets;
        } else {
            pos_title = "WORLD POS  ";
            pos = &decal->sector.pos;
        }

        igText(pos_title);
        sameline();
        change |= igInputFloat("##ox", &pos->x, 0.25f, 1.0f, "%.3f", 0);
        sameline();
        change |= igInputFloat("##oy", &pos->y, 0.25f, 1.0f, "%.3f", 0);
    }
    igPopItemWidth();

    igBeginDisabled(decal->is_on_side);
    {
        igText("PLANE      ");
        sameline();

        plane_type_e p = decal->is_on_side ? 0 : decal->sector.plane;
        change |= input_plane("", &p);
        if (!decal->is_on_side) { decal->sector.plane = p; }
    }
    igEndDisabled();

    const lptr_t new_ptr =
        input_lptr(
            "PARENT",
            LTF_SIDE | LTF_SECTOR,
            decal->is_on_side ?
                lptr_from(decal->side.ptr)
                : lptr_from(decal->sector.ptr),
                0);

    if (!lptr_is_null(new_ptr)) {
        if (lptr_type_flag(new_ptr) == LTF_SIDE) {
            decal_set_side(ed->level, decal, lptr_side(ed->level, new_ptr));
        } else {
            decal_set_sector(
                ed->level,
                decal,
                lptr_sector(ed->level, new_ptr),
                PLANE_TYPE_FLOOR);
        }

        change = true;
    }

    change |= igInputFloat("ROTATION", &decal->rotation, 0.05f, PI_4, "%.3f", 0);

done:
    if (change) {
        decal_recalculate(ed->level, decal);
    }

    igEndGroup();
    igPopID();

    return change;
}

static bool edit_entity_funcdata(entity_t *ent) {
    bool change = false;
    igText("FUNCDATA");

    switch (ent->itype) {
    case ENTITY_TYPE_BOOKMARK: {
        char label[64];
        snprintf(label, sizeof(label), "%d", ent->bookmark_index);
        igSetNextItemWidth(80);
        if (igBeginCombo("NUM", label, ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < EDITOR_MAX_BOOKMARKS; i++) {
                if (i == 0
                    || ed->bookmarks[i] == ent
                    || !ed->bookmarks[i]) {
                    snprintf(label, sizeof(label), "%d", i);
                    if (igSelectable_Bool(
                            label,
                            i == ent->bookmark_index,
                            ImGuiSelectableFlags_None,
                            (ImVec2) {})) {
                        ed->bookmarks[ent->bookmark_index] = NULL;
                        if (i != 0) { ed->bookmarks[i] = ent; }
                        ent->bookmark_index = i;
                        change = true;
                    }
                }
            }
            igEndCombo();
        }
    } break;
    case ENTITY_TYPE_FILL_LIGHT: {
        change |=
            edit_light_params(&ent->light, lptr_from(ent), PLANE_TYPE_FLOOR);
    } break;
    case ENTITY_TYPE_SPAWN_POINT: {
        igSetNextItemWidth(INPUT_WIDTH_INT);
        change |=
            igInputFloat(
                "SPAWN POINT ANGLE",
                &ent->spawn_point_angle,
                PI_6,
                PI_2,
                "%.3f",
                0);
        ent->spawn_point_angle = angle_wrap_tau(ent->spawn_point_angle);
    } break;

    default: break;
    }

    return change;
}

static bool edit_entity(entity_t *ent, int flags) {
    igPushID_Ptr(ent);
    igBeginGroup();
    bool change = false;

    if (igButton("SEL", (ImVec2) { 0, 0 })) {
        try_select_ptr(lptr_from(ent));
    }

    sameline();
    if (igButton("DEL", (ImVec2) { 0, 0 })) {
        *dynlist_push(ed->level->delete_ptrs) = lptr_from(ent);
        change = true;
        goto done;
    }

    sameline();
    if (igButton("CLONE", (ImVec2) { 0, 0 })) {
        ed->cur.mode = CM_ENTITY;
        ed->cur.mode_entity.clone = lptr_from(ent);
        ed->cur.mode_entity.bookmark = 0;
    }

    igText(
        "%s",
        lptr_to_str(ed->level, lptr_from(ent->sector), &g->frame_arena));

    texture_image_by_name(ent->ptype->sprite, NULL);
    sameline();

    entity_type_e itype = ent->itype;
    if (input_enum("TYPE", &itype, entity_type_desc())) {
        entity_set_type(ed->level, ent, itype);
        change = true;
    }

    // copy type from another entity
    sameline();
    lptr_t copy =
        button_select_lt("CPY##type", LTF_ENTITY, BUTTON_SELECT_LT_NONE);
    if (!lptr_is_null(copy)) {
        entity_set_type(
            ed->level, ent, lptr_entity(ed->level, copy)->itype);
        change = true;
    }

    // apply type to another entity
    sameline();
    lptr_t type_apply_ptr =
        button_select_lt(
            "APL##type",
            LTF_ENTITY,
            BUTTON_SELECT_LT_ALLOW_MULTI);

    if (!lptr_is_null(type_apply_ptr)) {
        editor_mark_ptr_change(type_apply_ptr);
        entity_t *type_apply_ent = lptr_entity(ed->level, type_apply_ptr);
        entity_set_type(ed->level, type_apply_ent, ent->itype);
        type_apply_ent->version++;
        editor_mark_ptr_change(lptr_from(type_apply_ent));
    }

    igSeparator();
    igBeginGroup();
    igPushID_Int(ent->itype);
    edit_entity_funcdata(ent);
    igPopID();
    igEndGroup();
    igSeparator();

    igPushItemWidth(INPUT_WIDTH_INT);
    {
        bool pos_change = false;
        v2 pos = ent->pos;
        struct { const char *label; f32 *v; } values[2] =
            {{ "##tx", &pos.x }, { "##ty", &pos.y }};

        for (int i = 0; i < 2; i++) {
            if (i != 0) { sameline(); }
            pos_change |=
                input_f32_clamped(
                    values[i].label,
                    values[i].v,
                    0.1f, 0.5f,
                    "%.3f",
                    ImGuiInputTextFlags_None,
                    0, 1e10f);
        }

        sameline();
        change |=
            input_f32_clamped(
                "##z",
                &ent->z,
                0.1f, 0.5f,
                "%.3f",
                ImGuiInputTextFlags_None,
                -1e10f, 1e10f);

        sameline();
        igText("%s", "POS");

        if (pos_change) {
            entity_try_move(ed->level, ent, pos);
            change = true;
        }
    }
    igPopItemWidth();

    if (change) {
        ent->version++;
    }

done:
    igEndGroup();
    igPopID();
    return change;
}

static bool edit_room(
        room_t *room,
        int flags) {
    igPushID_Ptr(room);
    igBeginGroup();

    bool change = false;

    if (igButton("SEL", (ImVec2) { 0, 0 })) {
        try_select_ptr(lptr_from(room));
    }

    sameline();
    if (igButton("DEL", (ImVec2) { 0, 0 })) {
        *dynlist_push(ed->level->delete_ptrs) = lptr_from(room);
        change = true;
        goto done;
    }

    igCheckbox("ENTRY", &room->is_entry);

    for (int i = 0; i < 2; i++) {
        igPushID_Int(i);
        igPushItemWidth(INPUT_WIDTH_INT);
        igInputInt("##x", &room->bounds.vs[i].x, 1, 5, 0);
        sameline();
        igPushItemWidth(INPUT_WIDTH_INT);
        igInputInt("##y", &room->bounds.vs[i].y, 1, 5, 0);
        igAlignTextToFramePadding();
        sameline();
        igText(i == 0 ? "MIN" : "MAX");
        igPopID();
    }

    room->bounds = box2i_sort(room->bounds);

done:
    igEndGroup();
    igPopID();
    return change;
}

// igText, but flashes yellow briefly when contents are changed
// "current" is pointer to buffer, "last" is a backup of buffer
// saves change tick in *changetime
static void change_highlight_text(
    const char *label,
    const char *current,
    char *last,
    usize n,
    u64 *change_time,
    bool colortext) {

    if (strcmp(current, last)) {
        *change_time = g->time.total_ns;
    }

    u32
        col_bg = IM_COL32(64, 64, 64, 64),
        col_text =
            colortext ?
                IM_COL32(255, 255, 32, 255) : IM_COL32(255, 255, 255, 255);

    // flash on change
    if (*change_time != 0) {
        if ((g->time.total_ns - *change_time) <= 6000) {
            col_bg = IM_COL32(64, 64, 64, 240);
        }
    }

    igPushID_Ptr(label);
    igPushStyleColor_U32(ImGuiCol_TableRowBg, col_bg);
    igPushStyleColor_U32(ImGuiCol_Text, col_text);
    if (igBeginTable(
            label, 1, ImGuiTableFlags_RowBg,
            (ImVec2) { 0, 0 }, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);
        igText("%s", current);
        igEndTable();
    }
    igPopStyleColor(2);
    igPopID();

    snprintf(last, n, "%s", current);
}

static void show_status() {
    igPushID_Str("status");

    if (igBeginMenuBar()) {
        if (igMenuItem_Bool("NEW", NULL, false, true)) {
            ed->modal.new = true;
        }

        if (igMenuItem_Bool("OPEN", NULL, false, true)) {
            ed->modal.open = true;
        }
        if (igMenuItem_Bool("SAVE", NULL, false, true)) {
            const char *errmsg;
            if (!editor_try_save_level(g->level_path, &errmsg)) {
                show_editor_error("error saving level: %s", errmsg);
            }
        }

        if (igMenuItem_Bool("SAVE AS", NULL, false, true)) {
            ed->modal.save_as = true;
        }

        igEndMenuBar();
    }

    // file status
    const char *file_status =
        mem_strfmt(
            &g->frame_arena,
            "%s%s \"%s\"",
            g->level_path,
            ed->saved_version == ed->level->version ? "" : "*",
            ed->level->name);

    change_highlight_text(
        "file_status",
        file_status,
        ed->file_status.last,
        sizeof(ed->file_status.last),
        &ed->file_status.change_tick,
        ed->saved_version != ed->level->version);

    // PVS progress
    const bool is_pvsing = ed->level->matrices.last_calc;

    igPushStyleColor_U32(ImGuiCol_TableRowBg, IM_COL32(64, 64, 64, 64));
    igPushStyleColor_U32(
        ImGuiCol_Text,
        is_pvsing ?
            IM_COL32(255, 255, 32, 255)
            : IM_COL32(255, 255, 255, 255));
    if (igBeginTable(
            "##pvs", 1, ImGuiTableFlags_RowBg,
            (ImVec2) { 0, 0 }, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);
        igText(
            "MATRICES: %d / %d",
            ed->level->matrices.progress,
            ed->level->sectors.data.size * 4);
        igEndTable();
    }
    igPopStyleColor(2);

    igPushStyleColor_U32(ImGuiCol_TableRowBg, IM_COL32(64, 64, 64, 64));
    igPushStyleColor_U32(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (igBeginTable(
            "##blocks", 1, ImGuiTableFlags_RowBg, (ImVec2) { 0, 0 }, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);
        igText(
            "BLOCKS:   %dx%d (%d)",
            ed->level->blocks.size.x,
            ed->level->blocks.size.y,
            ed->level->blocks.size.x * ed->level->blocks.size.y);
        igEndTable();
    }
    igPopStyleColor(2);

    // cursor pos
    igPushStyleColor_U32(ImGuiCol_TableRowBg, IM_COL32(64, 64, 64, 64));
    igPushStyleColor_U32(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (igBeginTable(
            "cursor_pos", 1, ImGuiTableFlags_RowBg,
            (ImVec2) { 0, 0 }, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);

        v3 pos;

        if (ed->mode == EDITOR_MODE_MAP) {
            pos = v3_of(ed->cur.pos.map, 0);
        } else if (ed->cam.side) {
            pos =
                v3_of(
                    side_x_to_point(ed->cam.side, ed->cam.side_pos.x),
                    ed->cam.side_pos.y);
        } else if (ed->cam.sect) {
            pos =
                v3_of(
                    ed->cam.sect_pos,
                    sector_point_zs(ed->cam.sect, ed->cam.sect_pos)
                        .zs[ed->cam.plane]);
        } else {
            pos = v3_of(NAN);
        }

        igText("CURSOR:   %" PRIv3, FMTv3(pos));
        igEndTable();
    }

    if (igBeginTable(
            "sector", 1, ImGuiTableFlags_RowBg,
            (ImVec2) { 0, 0 }, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);

        const sector_t *cam_sect = lptr_sector(ed->level, g->cam.sector);
        igText(
            "SECTOR:   %d / %d",
            cam_sect ? cam_sect->id : -1,
            cam_sect ? cam_sect->handle.gen : -1);
        igEndTable();
    }

    igPopStyleColor(2);

    // status line, truncated
    char *line =
        mem_strdup(
            &g->frame_arena,
            CURSOR_MODES[ed->cur.mode].status_line(&ed->cur));
    str_trunc_suffix(line, 32, "...");

    change_highlight_text(
        "status_line",
        line,
        ed->status_line.last,
        sizeof(ed->status_line.last),
        &ed->status_line.change_tick,
        ed->cur.mode != CM_DEFAULT);

    // current hover
    igPushStyleColor_U32(ImGuiCol_TableRowBg, IM_COL32(64, 64, 64, 64));
    igPushStyleColor_U32(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (igBeginTable(
            "hover", 1, ImGuiTableFlags_RowBg,
            (ImVec2) { 0, 0 }, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);

        const lptr_t ptr =
            ed->mode == EDITOR_MODE_MAP ? ed->cur.hover : ed->cam.ptr;

        igText("HOVER: %s", lptr_to_fancy_str(ed->level, ptr, &g->frame_arena));
        igEndTable();
    }
    igPopStyleColor(2);

    igPushStyleColor_U32(ImGuiCol_TableRowBg, IM_COL32(64, 64, 64, 64));
    igPushStyleColor_U32(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (igBeginTable(
            "texture", 1, ImGuiTableFlags_RowBg,
            (ImVec2) { 0, 0 }, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);

        const v4 color =
            v4_of(
                color_offset_with_hsv(v3_of(1, 0, 1), v3_from(ed->cam.hsva)),
                ed->cam.hsva.a);
        if (igColorButton(
                "##b",
                (ImVec4) { color.r, color.g, color.b, color.a },
                0,
                (ImVec2) { 20, 20 })) {
            igOpenPopup_Str("edit", 0);
        }

        sameline();
        texture_image_by_id(ed->cam.texture, NULL);
        sameline();
        igAlignTextToFramePadding();
        igText("%s", tex_atlas_entry_by_id(ed->cam.texture)->name);

        igEndTable();
    }
    igPopStyleColor(2);

    // palette editor
    igPushID_Str("palette");
    {
        for (int i = 0; i < ARRLEN(ed->level->palette); i++) {
            const char *desc = mem_strfmt(tlscratch(), "##%d", i);

            igColorEdit3(
                desc,
                &ed->level->palette[i].raw[0],
                ImGuiColorEditFlags_NoInputs);

            if (i != ARRLEN(ed->level->palette) - 1) {
                igSameLine(0.0f, 0.0f);
            }
        }
    }
    igPopID();

    igPushItemWidth(INPUT_WIDTH_INT);
    {
        igInputFloat("##gs", &ed->map.grid_size, 0.0f, 0.0f, "%.3f", 0);

        sameline();
        igSetCursorPosX(igGetCursorPosX() - 4);
        if (igButton("-", (ImVec2) { 19, 0 })) {
            // snap to nearest POT
            ed->map.grid_size = round_to_potf(ed->map.grid_size);
            ed->map.grid_size /= 2.0;
        }

        sameline();
        igSetCursorPosX(igGetCursorPosX() - 4);
        if (igButton("+", (ImVec2) { 19, 0 })) {
            // snap to nearest POT
            ed->map.grid_size = round_to_potf(ed->map.grid_size);
            ed->map.grid_size *= 2.0;
        }

        ed->map.grid_size = clamp(ed->map.grid_size, 0.125f, 16.0f);

        sameline();
        igSetCursorPosX(igGetCursorPosX() - 4);
        igText("GRID SIZE");

        f32 scale = ed->map.scale;
        igSetNextItemWidth(INPUT_WIDTH_INT);
        igInputFloat(
            "##scale", &scale, 0.0f, 0.0f,
            "%.3f", ImGuiInputTextFlags_None);

        igSameLine(0, igGetStyle()->ItemInnerSpacing.x);
        if (igButton("-", (ImVec2) { igGetFrameHeight(), igGetFrameHeight() })) {
            scale /= 2.0f;
        }

        igSameLine(0, igGetStyle()->ItemInnerSpacing.x);
        if (igButton("+", (ImVec2) { igGetFrameHeight(), igGetFrameHeight() })) {
            scale *= 2.0f;
        }

        // snap to nearest power of two
        int exp;
        frexp(scale, &exp);
        scale = powf(2.0f, exp - 1);

        if (fabsf(scale - ed->map.scale) > 0.0001f) {
            map_set_scale(scale);
        }

        sameline();
        igSetCursorPosX(igGetCursorPosX() - 4);
        igText("SCALE");
    }
    igPopItemWidth();


    igPushItemWidth(71);
    {
        igInputFloat("##camx", &ed->map.pos.x, 0, 0, "%.3f", 0);
        sameline();
        igSetCursorPosX(igGetCursorPosX() - 4);
        igInputFloat("##camy", &ed->map.pos.y, 0, 0, "%.3f", 0);
        sameline();
        igSetCursorPosX(igGetCursorPosX() - 4);
        igText("CAMERA POS");

        sameline();
        if (igButton("RESET", (ImVec2) {})) {
            g->cam.pos = v3_of(0);
            ed->map.pos = v2_of(0);
        }
    }
    igPopItemWidth();

    if (igButton("M2C", (ImVec2) { 0, 0 })) {
        map_to_cam();
    }

    sameline();
    if (igButton("C2M", (ImVec2) { 0, 0 })) {
        cam_to_map();
    }

    sameline();
    if (igButton("LTEXTS", (ImVec2) {})) {
        ed->ltexts_open = !ed->ltexts_open;
    }

    static v3 shift;

    // shifts the entire map/selection by a set amount
    sameline();
    if (igButton("SHIFT", (ImVec2) { 0, 0 })) {
        shift = v3_of(0);
        igOpenPopup_Str("SHIFT", 0);
    }

    if (igBeginPopupModal("SHIFT", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool use_selected = dynlist_size(ed->map.selected) != 0;
        if (use_selected) {
            igText(
                "SHIFTING %d SELECTED ELEMENTS",
                dynlist_size(ed->map.selected));
        } else {
            igText("SHFITING ENTIRE LEVEL");
        }

        igInputFloat("X", &shift.x, 0.25f, 1.0f, "%.3f", 0);
        igInputFloat("Y", &shift.y, 0.25f, 1.0f, "%.3f", 0);
        igInputFloat("Z", &shift.z, 0.25f, 1.0f, "%.3f", 0);


        if (igButton("GO", (ImVec2) { 0, 0 })) {
            level_shift(
                ed->level, shift, use_selected ? &ed->map.selected : NULL);
            igCloseCurrentPopup();
        }

        sameline();
        if (igButton("NOPE", (ImVec2) { 0, 0 })) {
            igCloseCurrentPopup();
        }

        igEndPopup();
    }

    // rotates the entire map/selection about a bookmark
    static f32 angle = 0.0f;
    static int rotate_bookmark = 0;

    sameline();
    if (igButton("ROTATE", (ImVec2) { 0, 0 })) {
        angle = 0.0f;
        rotate_bookmark = 0;
        igOpenPopup_Str("ROTATE", 0);
    }

    if (igBeginPopupModal("ROTATE", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool use_selected = dynlist_size(ed->map.selected) != 0;
        if (use_selected) {
            igText(
                "ROTATING %d SELECTED ELEMENTS",
                dynlist_size(ed->map.selected));
        } else {
            igText("ROTATING ENTIRE LEVEL");
        }

        if (igButton("SNAP", (ImVec2) {})) {
            angle = roundf(angle / PI_16) * PI_16;
        }

        sameline();

        igSetNextItemWidth(100.0f);
        igInputFloat("ANGLE", &angle, PI_16, PI_4, "%.3f", 0);

        char label[32];
        snprintf(label, sizeof(label), "%d", rotate_bookmark);

        igSetNextItemWidth(80);
        if (igBeginCombo("##num", label, ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < EDITOR_MAX_BOOKMARKS; i++) {
                if (i == 0 || ed->bookmarks[i]) {
                    snprintf(label, sizeof(label), "%d", i);
                    if (igSelectable_Bool(
                            label,
                            i == rotate_bookmark,
                            ImGuiSelectableFlags_None,
                            (ImVec2) {})) {
                        rotate_bookmark = i;
                    }
                }
            }
            igEndCombo();
        }

        sameline();
        igText("BOOKMARK");

        if (ed->bookmarks[rotate_bookmark]) {
            igText("ORG %" PRIv2, FMTv2(ed->bookmarks[rotate_bookmark]->pos));
        } else {
            igText("INVALID");
        }

        igBeginDisabled(!ed->bookmarks[rotate_bookmark]);
        if (igButton("GO", (ImVec2) { 0, 0 })) {
            level_rotate(
                ed->level,
                ed->bookmarks[rotate_bookmark]->pos,
                angle,
                use_selected ? &ed->map.selected : NULL);
            igCloseCurrentPopup();
        }
        igEndDisabled();

        sameline();
        if (igButton("NOPE", (ImVec2) { 0, 0 })) {
            igCloseCurrentPopup();
        }

        igEndPopup();
    }

    sameline();
    if (igButton("FIND", (ImVec2) { 0, 0 })) {
        igOpenPopup_Str("FIND", 0);
    }

    if (igBeginPopupModal("FIND", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static struct { u32 type; const char *name; } find_options[] = {
            { LT_VERTEX, "VERTEX" },
            { LT_WALL, "WALL" },
            { LT_SIDE, "SIDE" },
            { LT_SECTOR, "SECTOR" },
            { LT_DECAL, "DECAL" },
            { LT_ENTITY, "ENTITY" },
        };

        static int index = 0, id = 0;

        igSetNextItemWidth(120);
        if (igBeginCombo("TYPE", find_options[index].name, 0)) {
            for (int i = 0; i < ARRLEN(find_options); i++) {
                if (igSelectable_Bool(
                        find_options[i].name, index == i, 0, (ImVec2) {})) {
                    index = i;
                }
            }
            igEndCombo();
        }

        igSetNextItemWidth(60);
        igInputInt("ID", &id, 0, 0, 0);

        // try to find
        const lptr_t ptr =
            lptr_from_nogen(
                ed->level,
                (lptr_nogen_t) {
                    .type = find_options[index].type,
                    .index = id
                });

        const bool valid = lptr_is_valid(ed->level, ptr);

        igBeginDisabled(!valid);
        if (igButton("GO", (ImVec2) { 0, 0 })) {
            map_center_on(map_center_for_ptr(ptr));
            igCloseCurrentPopup();
        }
        igEndDisabled();

        sameline();
        if (igButton("NOPE", (ImVec2) { 0, 0 })) {
            igCloseCurrentPopup();
        }

        igEndPopup();
    }

    igCheckbox("VSYNC", &g->vsync);
    sameline();
    igCheckbox("NO AI", &g->debug.no_ai);

    igBeginDisabled(g->vsync);
    {
        igSetNextItemWidth(138);
        igSliderInt("MAX FPS", &g->max_fps, 0, 500, "%d", 0);
        g->max_fps = (g->max_fps / 5) * 5;
    }
    igEndDisabled();

    igSetNextItemWidth(86);
    igSliderFloat(
        "##gamma_slider", &g->gamma, 0.0f, 3.0f, "%.2f", 0);

    sameline();
    if (igButton("RESET##gamma", (ImVec2) {})) {
        g->gamma = 1.0;
    }

    sameline();
    igText("GAMMA");

    igSetNextItemWidth(86);
    igSliderFloat(
        "##volume", &g_sound->volume, 0.0f, 1.0f, "%.2f", 0);

    sameline();
    if (igButton("CLEAR##sound", (ImVec2) {})) {
        sound_stop_all();
    }

    sameline();
    igText("VOLUME");

    {
        static char buf[1024];
        snprintf(buf, sizeof(buf), "%s", ed->level->name);

        igSetNextItemWidth(138);
        if (igInputText("NAME", buf, sizeof(buf), 0, NULL, 0)) {
            level_set_name(ed->level, buf);
        }
    }

    input_checkbox_bit("HUB", ed->level, bitoffsetof(level_t, is_hub));

    if (igTreeNode_Str("FOG")) {
        igInputFloat("##dist", &ed->level->fog.dist, 1.0f, 5.0f, "%.3f", 0);
        ed->level->fog.dist = max(ed->level->fog.dist, 10.0f);
        sameline();
        igSetCursorPosX(igGetCursorPosX() - 4);
        igText("DIST");
        igTreePop();
    }

    if (igTreeNodeEx_Str("BOOKMARKS", 0)) {
        for (int i = 1; i < EDITOR_MAX_BOOKMARKS; i++) {
            igPushID_Int(i);
            igPushStyleColor_U32(
                ImGuiCol_Text,
                ed->bookmarks[i] ?
                    IM_COL32(255, 255, 255, 255)
                    : IM_COL32(128, 128, 128, 255));

            igText("[%d]", i);

            sameline();
            const ImVec2 button_size = { 32.0f, 0.0f };
            if (ed->bookmarks[i]) {
                if (igButton("GO", button_size)) {
                    go_to_bookmark(i);
                }
            } else {
                if (igButton("NEW", button_size)) {
                    ed->cur.mode = CM_ENTITY;
                    ed->cur.mode_entity.clone = LPTR_NULL;
                    ed->cur.mode_entity.bookmark = i;
                }
            }

            sameline();
            if (ed->bookmarks[i]) {
                igText(
                    "%d %" PRIv2,
                    ed->bookmarks[i]->id,
                    FMTv2(ed->bookmarks[i]->pos));
            } else {
                igText("...");
            }

            igPopStyleColor(1);
            igPopID();
        }
        igTreePop();
    }

    if (igTreeNodeEx_Str("VISOPTS", ImGuiTreeNodeFlags_None)) {
        input_flags(
            &g->visopt,
            VISOPT_MASK,
            visopt_desc()->names,
            NULL,
            0);
        igTreePop();
    }

    igPushStyleColor_U32(ImGuiCol_TableRowBg, IM_COL32(64, 64, 64, 64));
    if (igBeginTable(
            "details", 1, ImGuiTableFlags_RowBg,
            (ImVec2) { 0, 0 }, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);
        igText(
            "F %.2f | F2S %.2f | FPS %d | TPS %d",
            ns_to_ms(g->time.frame.avg_duration_ns),
            ns_to_ms(g->time.frame_to_swapchain_avg_ns),
            g->time.frame.count_per_second,
            g->time.tick.count_per_second);
        igEndTable();
    }
    igPopStyleColor(1);
    igPopID();
}

static void edit_ltexts() {
    allocator_t *allocator = &ed->level->arena;

    dynlist_each(ed->level->ltexts, it) {
        igAlignTextToFramePadding();
        igText("lt_");

        sameline();
        igSetNextItemWidth(80);
        input_text(
            mem_strfmt(tlscratch(), "##name%d", it.i),
            allocator, &it.el->name, 0, NULL, NULL);

        if (strlen(it.el->name) == 0) {
            mem_free(allocator, it.el->name);
            it.el->name = mem_strfmt(allocator, "%d", it.i);
        }

        sameline();
        igSetNextItemWidth(200);
        input_text(
            mem_strfmt(tlscratch(), "##text%d", it.i),
            allocator, &it.el->text, 0, NULL, NULL);

        sameline();
        if (igButton(mem_strfmt(tlscratch(), "-##%d", it.i), (ImVec2) {})) {
            mem_free(allocator, it.el->name);
            mem_free(allocator, it.el->text);
            dynlist_remove_it(ed->level->ltexts, it);
            continue;
        }
    }

    if (igButton("NEW", (ImVec2) {})) {
        int i = 0;
        while (tex_atlas_contains(mem_strfmt(&g->frame_arena, "lt_%d", i))) {
            i++;
        }

        *dynlist_push(ed->level->ltexts) =
            (ltext_t) {
                .name = mem_strfmt(allocator, "%d"),
                .text = mem_strdup(allocator, ""),
            };
    }

    /* static int quick_edit = 0; */
    /* bool quick_edit_go = false; */

    /* igAlignTextToFramePadding(); */
    /* igText("QUICK EDIT"); */
    /* sameline(); */
    /* igSetNextItemWidth(40); */
    /* if (igInputInt( */
    /*         "##qe", &quick_edit, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) { */
    /*     quick_edit_go = true; */
    /* } */
    /* quick_edit = clamp(quick_edit, 0, MAX_VTEXTS - 1); */

    /* sameline(); */
    /* quick_edit_go |= igButton("GO", (ImVec2) {}); */
    /* igSpacing(); */
    /* igAlignTextToFramePadding(); */
    /* igText("RES"); */

    /* igSameLine(58, -1.0f); */
    /* igAlignTextToFramePadding(); */
    /* igText("JT?"); */

    /* igSameLine(120, -1.0f); */
    /* igAlignTextToFramePadding(); */
    /* igText("TEXT"); */

    /* ImVec2 avail; */
    /* igGetContentRegionAvail(&avail); */

    /* igBeginChild_Str("child", avail, false, 0); */
    /* for (int i = 0; i < MAX_VTEXTS; i++) { */
    /*     static char buf[1024]; */
    /*     vtext_t *vtext = &ed->level->vtexts[i]; */

    /*     igPushID_Int(i); */

    /*     igAlignTextToFramePadding(); */
    /*     igText("vt_%d", i); */

    /*     igSameLine(50, -1.0f); */
    /*     input_checkbox_mask("##jt", RANGE_REF(vtext->flags), VTEXT_JUST_TEXT); */

    /*     sameline(); */
    /*     if (igButton("CLR", (ImVec2) {})) { */
    /*         mem_free(&ed->level->arena, vtext->str); */
    /*         *vtext = (vtext_t) { 0 }; */
    /*     } */

    /*     if (vtext->str) { */
    /*         // copy, convert chars (\ -> \ + \, \n -> \ + n) */
    /*         char *dst = buf, *src = vtext->str; */
    /*         while (*src) { */
    /*             if (*src == '\n') { */
    /*                 *dst = '\\'; dst++; */
    /*                 *dst = 'n';  dst++; */
    /*             } else if (*src == '\\') { */
    /*                 *dst = '\\'; dst++; */
    /*                 *dst = '\\'; dst++; */
    /*             } else { */
    /*                 *dst = *src; */
    /*                 dst++; */
    /*             } */

    /*             src++; */
    /*         } */
    /*         *dst = '\0'; */
    /*     } else { */
    /*         buf[0] = '\0'; */
    /*     } */

    /*     sameline(); */
    /*     igSetNextItemWidth(avail.x - 110); */
    /*     if (igInputText("##text", buf, sizeof(buf), 0, NULL, NULL)) { */
    /*         const int len = strlen(buf); */

    /*         // realloc str on change */
    /*         mem_free(&ed->level->arena, vtext->str); */

    /*         if (len) { */
    /*             vtext->str = mem_alloc(&ed->level->arena, len + 1); */
    /*             vtext->flags |= VTEXT_DIRTY; */

    /*             // copy, convert chars (\\ -> \, \ + n -> \n) */
    /*             char *dst = vtext->str, *src = buf; */
    /*             while (*src) { */
    /*                 if (*src == '\\') { */
    /*                     if (*(src + 1) == '\\') { */
    /*                         *dst = '\\'; */
    /*                         src++; */
    /*                     } else if (*(src + 1) == 'n') { */
    /*                         *dst = '\n'; */
    /*                         src++; */
    /*                     } else { */
    /*                         WARN( */
    /*                             "unrecognized control char %d", */
    /*                             (int) *(src + 1)); */
    /*                     } */
    /*                 } else { */
    /*                     *dst = *src; */
    /*                 } */

    /*                 dst++; */
    /*                 src++; */
    /*             } */
    /*             *dst = '\0'; */
    /*         } else { */
    /*             *vtext = (vtext_t) { 0 }; */
    /*         } */
    /*     } */

    /*     if (quick_edit_go && i == quick_edit) { */
    /*         igSetScrollHereY(0.5f); */
    /*     } */

    /*     igPopID(); */
    /* } */
    /* igEndChild(); */
}

static void show_editor_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ed->err_msg, sizeof(ed->err_msg), fmt, ap);
    va_end(ap);
    ERROR("(editor error message): %s", ed->err_msg);
    igOpenPopup_Str("ERROR", ImGuiPopupFlags_None);
}

// does open/save as/new modals
static void do_modal_ui() {
    static char path[PATH_MAX];
    const char *errmsg;

    enum { MODAL_NEW, MODAL_OPEN, MODAL_SAVE_AS, MODAL_COUNT };
    const char *names[3] = { "NEW", "OPEN", "SAVE AS", };
    bool *is_open[3] = { &ed->modal.new, &ed->modal.open, &ed->modal.save_as, };
    bool just_opened[3] = { 0 };

    for (int i = 0; i < MODAL_COUNT; i++) {
        if (*is_open[i] && !igIsPopupOpen_Str(names[i], 0)) {
            just_opened[i] = true;
            igOpenPopup_Str(names[i], 0);
            snprintf(path, sizeof(path), "%s", g->level_path);
        }
    }

    const int win_flags =
        ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoMove;

    for (int m = 0; m < MODAL_COUNT; m++) {
        if (!igBeginPopupModal(names[m], is_open[m], win_flags)) {
            continue;
        }

        igText("PATH");
        igSetNextItemWidth(200);

        if (just_opened[m]) {
            igSetKeyboardFocusHere(0);
        }
        const bool entered =
            igInputText(
                "##path",
                path,
                sizeof(path),
                ImGuiInputTextFlags_EnterReturnsTrue,
                NULL,
                NULL);

        const char *msg;
        bool ok;

        switch (m) {
        case MODAL_NEW: {
            const bool exists = file_exists(path);
            ok =
                !exists
                && path[0]
                && path[0] != '.'
                && str_is_suffixed_by(path, ".json");

            if (exists) {
                msg = "EXISTS";
            } else if (ok) {
                msg = "OK";
            } else {
                msg = "NEEDS <file>.json";
            }
        } break;
        case MODAL_OPEN: {
            const bool is_dir = file_isdir(path);

            ok = file_exists(path) && !is_dir;

            if (is_dir) {
                msg = "DIR";
            } else {
                msg = ok ? "OK" : "NOT OK";
            }
        } break;
        case MODAL_SAVE_AS: {
            const bool exists = file_exists(path);
            const bool path_ok =
                path[0]
                && path[0] != '.'
                && str_is_suffixed_by(path, ".json");

            ok = path_ok;

            if (ok) {
                msg = "OK";
            } else if (exists) {
                msg = "FILE EXISTS";
            } else {
                msg = "WEIRD PATH";
            }
        } break;
        }

        igAlignTextToFramePadding();
        igText("%s", msg);

        sameline();
        igSetCursorPosX(185);
        igBeginDisabled(!ok);
        if (igButton("GO", (ImVec2) {}) || entered) {
            snprintf(
                ed->reload.level_path,
                sizeof(ed->reload.level_path),
                "%s",
                path);

            switch (m) {
            case MODAL_NEW:
                ed->reload.on = true;
                ed->reload.is_new = true;
                break;
            case MODAL_OPEN:
                ed->reload.on = true;
                break;
            case MODAL_SAVE_AS:
                ed->saved_version = -1;
                if (!editor_try_save_level(path, &errmsg)) {
                    show_editor_error(
                        "error saving level as %s: %s",
                        path,
                        errmsg);
                }
                break;
            }

            *is_open[m] = false;
        }

        if (input_get(g->input, "escape") & INPUT_RELEASE) {
            *is_open[m] = false;
        }

        igEndDisabled();
        igEndPopup();
    }
}

static void do_editor_ui() {
    do_modal_ui();

    // window to close when we hit it
    ImGuiWindow *close_window = NULL;

    // close most recent window with ESC
    ImGuiContext *ctx = igGetCurrentContext();
    if (ctx
        && ed->cur.start_mode == CM_DEFAULT
        && (ed->buttons[BUTTON_CANCEL] & INPUT_PRESS)
        && ctx->WindowsFocusOrder.Size > 0) {
        int i = ctx->WindowsFocusOrder.Size - 1;
        while (
            i != -1
            && !strcmp(ctx->WindowsFocusOrder.Data[i]->Name, "STATUS")) {
            i--;
        }

        if (i >= 0) {
            close_window = ctx->WindowsFocusOrder.Data[i];
        }
    }

    dynlist_each(ed->to_open_ptrs, it) {
        *dynlist_push(ed->open_ptrs) = *it.el;
    }
    dynlist_clear(ed->to_open_ptrs);

    // show all open editors
    dynlist_each(ed->open_ptrs, it) {
        // remove from open list if closed, deleted or invalid
        if (!lptr_is_valid(ed->level, *it.el)) {
            dynlist_remove_it(ed->open_ptrs, it);
            continue;
        }

        bool is_new = false;
        dynlist_each(ed->front_ptrs, it_btf) {
            if (lptr_eq(*it.el, *it_btf.el)) {
                igSetNextWindowFocus();
                is_new = true;
                break;
            }
        }

        bool title_color_changed = false;

        // true if window color should be highlighted
        bool highlight_color = lptr_eq(*it.el, ed->highlight.ptr);

        if (ed->mode == EDITOR_MODE_MAP) {
            highlight_color |= lptr_eq(*it.el, ed->cur.hover);
        } else if (ed->mode == EDITOR_MODE_CAM) {
            highlight_color |= !ed->mouse_grab && lptr_eq(*it.el, ed->cam.ptr);
        }

        if (is_ptr_changed(*it.el)) {
            title_color_changed = true;

            const ImVec4 color = {
                1.0f,
                0.6f,
                0.03f,
                1.0f,
            };

            igPushStyleColor_Vec4(ImGuiCol_TitleBg, color);
            igPushStyleColor_Vec4(ImGuiCol_TitleBgActive, color);
            igPushStyleColor_Vec4(ImGuiCol_TitleBgCollapsed, color);
        } else if (highlight_color) {
            title_color_changed = true;

            const ImVec4 color = {
                ed->highlight.alpha,
                0.03f,
                0.03f,
                1.0f,
            };

            igPushStyleColor_Vec4(ImGuiCol_TitleBg, color);
            igPushStyleColor_Vec4(ImGuiCol_TitleBgActive, color);
            igPushStyleColor_Vec4(ImGuiCol_TitleBgCollapsed, color);
        }

        bool in_window = true;

        const int win_flags =
            ImGuiWindowFlags_AlwaysAutoResize;

        if (is_new) {
            const ImVec2 mouse_pos = igGetIO()->MousePos;
            igSetNextWindowPos(
                (ImVec2) { mouse_pos.x + 8.0f, mouse_pos.y - 16.0f },
                0,
                (ImVec2) {});
        }

        bool is_open = true, changed = false;
        igBegin(
            lptr_to_str(ed->level, *it.el, &g->frame_arena),
            &is_open,
            win_flags);

        if (igIsWindowFocused(ImGuiFocusedFlags_None)) {
            ed->highlight.ptr = *it.el;
        }

        // keep window inside of viewport bounds
        const ImGuiWindow *win = igGetCurrentWindow();
        const ImVec2
            viewport_size = igGetMainViewport()->Size,
            target_pos = {
                clamp(win->Pos.x, 0.0f, viewport_size.x - win->Size.x),
                clamp(win->Pos.y, 0.0f, viewport_size.y - win->Size.y),
            };

        if (target_pos.x != win->Pos.x || target_pos.y != win->Pos.y) {
            igSetWindowPos_Vec2(target_pos, ImGuiCond_Always);
        }

        // set EF_NEW if window has just been opened
        const int flags = is_new ? EF_NEW : EF_NONE;

        if (lptr_matches(*it.el, LTF_VERTEX)) {
            changed |=
                edit_vertex(
                    lptr_vertex(ed->level, *it.el),
                    flags | EF_VERTEX_LIST_WALLS);
        } else if (lptr_matches(*it.el, LTF_WALL | LTF_SIDE)) {
            side_t *side = lptr_side(ed->level, *it.el);
            wall_t *wall = lptr_wall(ed->level, *it.el);

            if (side) {
                wall = side->wall;
            } else {
                side = NULL;
            }

            changed |= edit_wall(wall, side, flags);
        } else if (lptr_matches(*it.el, LTF_SECTOR)) {
            sector_t *sect = lptr_sector(ed->level, *it.el);
            const bool sector_changed = edit_sector(sect, flags);
            changed |= sector_changed;

            if (sector_changed) {
                level_each(sector_t, &ed->level->sectors, it) {
                    const sector_t *like = sector_get_like(it.el);
                    if (like == sect) {
                        it.el->version++;
                    }
                }
            }
        } else if (lptr_matches(*it.el, LTF_DECAL)) {
            changed |= edit_decal(lptr_decal(ed->level, *it.el), flags);
        } else if (lptr_matches(*it.el, LTF_ENTITY)) {
            changed |= edit_entity(lptr_entity(ed->level, *it.el), flags);
        } else if (lptr_matches(*it.el, LTF_ROOM)) {
            changed |= edit_room(lptr_room(ed->level, *it.el), flags);
        } else {
            ASSERT(false, "trying to edit invalid lptr_t");
        }

        if (close_window == igGetCurrentWindow()) {
            // close for this ptr
            is_open = false;
        }

        if (in_window) {
            igEnd();
        }

        if (title_color_changed) {
            igPopStyleColor(3);
        }

        if (changed && lptr_is_valid(ed->level, *it.el)) {
            editor_mark_ptr_change(*it.el);
            lptr_level_fields(ed->level, *it.el)->version++;
            lptr_recalculate(ed->level, *it.el);
        }

        if (in_window && !is_open) {
            // remove if no longer open
            dynlist_remove_it(ed->open_ptrs, it);
        }
    }

    // reset bring to front pointers
    dynlist_resize(ed->front_ptrs, 0);

    igSetNextWindowSizeConstraints(
        (ImVec2) { 300, 0, },
        (ImVec2) { 300, 1e10 },
        NULL, NULL);

    const bool is_rooms = ed->rooms && ed->mode == EDITOR_MODE_MAP;

    // change status color if room mode
    const int col_indices[] = {
        ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed
    };

    for (int i = 0; i < ARRLEN(col_indices); i++) {
        igPushStyleColor_Vec4(
            col_indices[i],
            is_rooms ?
                (ImVec4) { 0.7f, 0.7f, 0.1f, 1.0f }
                : *igGetStyleColorVec4(col_indices[i]));
    }

    char name[256];
    snprintf(
        name,
        sizeof(name),
        "%s###STATUS",
        is_rooms ? "STATUS (ROOMS)" : "STATUS");

    igBegin(
        name,
        NULL,
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_AlwaysAutoResize);
    {
        show_status();

        igSetWindowPos_Vec2(
            (ImVec2) {
                g->window_size.x - igGetCurrentWindow()->Size.x,
                0.0f
            }, ImGuiCond_Always);
    }
    igEnd();

    igPopStyleColor(ARRLEN(col_indices));

    if (ed->ltexts_open) {
        igBegin("LTEXTS", &ed->ltexts_open, ImGuiWindowFlags_None);
        {
            igSetWindowSize_Vec2((ImVec2) { 300, 200 }, ImGuiCond_Once);
            edit_ltexts();
        }
        igEnd();
    }
}

/// END EDITOR UI ///
