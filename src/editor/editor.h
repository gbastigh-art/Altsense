#pragma once

#include "ext/cimgui.h"

// cimgui is missing some imgui macros :(
#ifndef IM_COL32
#define IM_COL32(R, G, B, A)         \
    ((((u32) ((A) & 0xFF)) << 24) |  \
     (((u32) ((B) & 0xFF)) << 16) |  \
     (((u32) ((G) & 0xFF)) <<  8) |  \
     (((u32) ((R) & 0xFF)) <<  0))
#endif // ifndef IM_COL32

#include "defs.h"
#include "level/level_types.h"
#include "level/lptr.h"
#include "util/map.h"
#include "util/color.h"

// global editor instance
extern editor_t *g_editor;

// for keys 1-9, bookmark 0 is invalid
#define EDITOR_MAX_BOOKMARKS 9

#define EDITOR_BASE_SCALE 16.0f

// map configuration
#define MAP_DEFAULT_GRIDSIZE 0.25f

//#define MAP_VERTEX_SIZE 0.20f
#define MAP_DECAL_SIZE 0.45f
#define MAP_ENTITY_SIZE 0.45f

#define MAP_SCALE_MAX 16.0f
#define MAP_SCALE_MIN 0.125f

// 2D map view colors
#define MAP_WHOLE_GRID_COLOR             V4_FROM_ABGR(0x30404040)
#define MAP_GRID_COLOR                   V4_FROM_ABGR(0x30232323)
#define MAP_VERTEX_COLOR                 V4_FROM_ABGR(0xFFDD3311)
#define MAP_VERTEX_HOVER_COLOR           V4_FROM_ABGR(0xFFFF9944)
#define MAP_SELECT_COLOR                 V4_FROM_ABGR(0xFFCCCCCC)
#define MAP_WALL_COLOR                   V4_FROM_ABGR(0xFF3333AA)
#define MAP_WALL_HOVER_COLOR             V4_FROM_ABGR(0xFF8080FF)
#define MAP_WALL_CONNECT_COLOR           V4_FROM_ABGR(0xFF44CC22)
#define MAP_SIDE_NORMAL_COLOR            V4_FROM_ABGR(0xFF606060)
#define MAP_SIDE_HOVER_COLOR             V4_FROM_ABGR(0xFFFFFFFF)
#define MAP_SIDE_SELECT_COLOR            V4_FROM_ABGR(0xFFFFFFFF)
#define MAP_SIDE_PORTAL_COLOR            V4_FROM_ABGR(0xFF40B040)
#define MAP_SIDE_PORTAL_DISCONNECT_COLOR V4_FROM_ABGR(0xFFFF22FF)
#define MAP_SIDE_PORTAL_BAD_COLOR        V4_FROM_ABGR(0xFF4040FF)
#define MAP_SELECT_BORDER_COLOR          V4_FROM_ABGR(0xFFFFFFFF)
#define MAP_NEW_WALL_COLOR               V4_FROM_ABGR(0x80999999)
#define MAP_NEW_VERTEX_COLOR             V4_FROM_ABGR(0xFF999999)
#define MAP_SECTOR_HIGHLIGHT             V4_FROM_ABGR(0x2A33CAFA)
#define MAP_SECTOR_SELECT                V4_FROM_ABGR(0xAAAAAAAA)
#define MAP_SIDE_ARROW_COLOR             V4_FROM_ABGR(0xFFFF44FF)
#define MAP_DECAL_COLOR                  V4_FROM_ABGR(0xFFFFAA66)
#define MAP_DECAL_HOVER_COLOR            V4_FROM_ABGR(0xFFFFDD99)
#define MAP_ENTITY_COLOR                 V4_FROM_ABGR(0xFF66AAFF)
#define MAP_ENTITY_HOVER_COLOR           V4_FROM_ABGR(0xFF99DDFF)
#define MAP_FRIEND_COLOR                 V4_FROM_ABGR(0xFF4080FF)
#define MAP_CHANGED_COLOR                V4_FROM_ABGR(0x551090FF)

// controls
enum {
    BUTTON_NONE = 0,
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_EDIT,
    BUTTON_SELECT,
    BUTTON_DESELECT,
    BUTTON_ZOOM_IN,
    BUTTON_ZOOM_OUT,
    BUTTON_SNAP,
    BUTTON_SNAP_WALL,
    BUTTON_MULTI_SELECT,
    BUTTON_CANCEL,
    BUTTON_MOVE_DRAG,
    BUTTON_SELECT_AREA,
    BUTTON_SWITCH_MODE,
    BUTTON_NEW_WALL,
    BUTTON_NEW_SIDE,
    BUTTON_NEW_VERTEX,
    BUTTON_EZPORTAL,
    BUTTON_FIXER,
    BUTTON_DELETE,
    BUTTON_FUSE,
    BUTTON_SPLIT,
    BUTTON_CONNECT,
    BUTTON_PLANE_UP,
    BUTTON_PLANE_DOWN,
    BUTTON_PLANE_SLOPE_UP,
    BUTTON_PLANE_SLOPE_DOWN,
    BUTTON_SECTOR_UP,
    BUTTON_SECTOR_DOWN,
    BUTTON_BIGADJUST,
    BUTTON_SAVE,
    BUTTON_NEW_DECAL,
    BUTTON_NEW_ENTITY,
    BUTTON_MOUSEMODE,
    BUTTON_MAP_TO_CAM,
    BUTTON_CAM_TO_MAP,
    BUTTON_CLOSE_ALL,
    BUTTON_TEX_MOD,
    BUTTON_TEX_MOD_RESET,
    BUTTON_TEX_MOD_OVERRIDE,
    BUTTON_TEX_MOD_SCALE_UP,
    BUTTON_TEX_MOD_SCALE_DOWN,
    BUTTON_TEX_MOD_SCALE_X,
    BUTTON_TEX_MOD_SCALE_Y,
    BUTTON_TEX_MOD_SNAP,
    BUTTON_DELETE_SECTOR,
    BUTTON_BOOKMARK,
    BUTTON_ROOM_MODE,
    BUTTON_NEW_ROOM,
    BUTTON_COUNT
};

// see editor.c
typedef struct editor editor_t;
typedef struct cursor cursor_t;

typedef enum {
    CM_DEFAULT,
    CM_SELECT,
    CM_DRAG,
    CM_MOVE_DRAG,
    CM_SELECT_AREA,
    CM_WALL,
    CM_VERTEX,
    CM_SIDE,
    CM_EZPORTAL,
    CM_FIXER,
    CM_DELETE,
    CM_FUSE,
    CM_SPLIT,
    CM_CONNECT,
    CM_DECAL,
    CM_ENTITY,
    CM_MOVE_DECAL,
    CM_MOVE_ENTITY,
    CM_TEX_MOD,
    CM_NEW_ROOM,
    CM_RESIZE_ROOM,
    CM_COUNT
} cursor_mode_e;

// cursor mode flags
enum {
    CMF_NONE            = 0,
    CMF_CLICK_CANCEL    = 1 << 0, // click elsewhere to cancel mode
    CMF_NOHOVER         = 1 << 1, // mode disables hover
    CMF_NODRAG          = 1 << 2, // mode cannot be supplanted by drag
    CMF_MAP             = 1 << 3, // mode is available in EDITOR_MODE_MAP
    CMF_CAM             = 1 << 4, // mode is available in EDITOR_MODE_CAM
    CMF_MAP_AND_CAM     = (CMF_MAP | CMF_CAM),
    CMF_EXPLICIT_CANCEL = 1 << 5, // mode MUST be canceled with ESC
};

typedef struct {
    cursor_mode_e mode;
    int flags, priority;

    bool (*trigger)(cursor_t*);
    void (*update)(cursor_t*);
    void (*render)(cursor_t*);
    const char *(*status_line)(cursor_t*);
    void (*cancel)(cursor_t*);
} cursor_mode_t;

// editor mode
typedef enum {
    EDITOR_MODE_MAP,
    EDITOR_MODE_CAM
} editor_mode_e;

typedef struct cursor {
    // current cursor position
    struct {
        v2 screen;
        v2 map;
    } pos;

    // current map mode hovered ptr LPTR_NULL if not present
    lptr_t hover;

    // current map mode hovered sector
    sector_t *sector;

    // current map mode hovered room LPTR_NULL if not present
    lptr_t hover_room;

    // current mode
    cursor_mode_e mode;

    // mode at the start of this editor frame
    cursor_mode_e start_mode;

    // state for each mode
    union {
        struct {
            // ID (igGetID*) of GUI item is selecting
            ImGuiID item;

            // selected, NULL if nothing
            lptr_t selected;

            // accepted level types
            int types;
        } mode_select_id;

        struct {
            // map of lptr_t -> v2 start pos
            map_t start_map;

            // statring cursor position
            v2 start;
        } mode_drag;

        struct {
            lptr_t room;
        } mode_resize_room;

        struct {
            // last SCREEN pos of cursor
            v2 last;

            // true if any movement has occurred
            bool moved;
        } mode_move_drag;

        struct {
            v2 start, end;
        } mode_select_area;

        struct {
            lptr_t start_ptr;
            v2 start;
            bool started;
        } mode_wall;

        struct {
            v2 start;
            bool started;
        } mode_side;

        struct {
            v2 start;
            bool started;
        } mode_ezportal;

        struct {
            lptr_t first;
        } mode_fuse;

        struct {
            side_t *first;
        } mode_connect;

        struct {
            decal_t *decal;
        } mode_move_decal;

        struct {
            entity_t *entity;
        } mode_move_entity;

        struct {
            bool drag;
            v2 drag_start;
        } mode_delete;

        struct {
            lptr_t clone;
            int bookmark;
        } mode_entity;

        struct {
            lptr_t clone;
        } mode_decal;

        struct {
            lptr_t drag_ptr;
            v2 drag_start;
            plane_type_e drag_plane;
            v2i drag_start_offsets;
            lptr_t last_ptr;
        } mode_tex_mod;
    };
} cursor_t;

// modal dialog states
// on OPEN_NEW, a new version of the modal is opened
typedef enum {
    MS_CLOSED,
    MS_OPEN_NEW,
    MS_OPEN
} modal_state_e;

typedef struct editor {
    // editor-lifetime arena
    allocator_t arena;

    level_t *level;

    // current window size
    v2i size;

    // button states for BUTTON_*
    u8 buttons[BUTTON_COUNT];

    // current editor mode, determines which view is shown
    editor_mode_e mode;

    // if true, rooms are visible/interacted with/etc.
    bool rooms;

    // current cursor state
    cursor_t cur;

    // current state of map editor
    struct {
        v2 pos;
        f32 scale;
        f32 grid_size;

        // list of things which are selected
        DYNLIST(lptr_t) selected;

        // set with scale
        f32 normal_length;
        f32 vertex_size;
        f32 side_select_dist;
        f32 wall_select_dist;
        f32 grid_point_size;
        f32 line_thickness;
    } map;

    // if true, mouse is grabbed in 3D mode (does not affect map mode where
    // mouse is never grabbed)
    bool mouse_grab;

    // current (3D) mouse cursor info
    struct {
        lptr_t ptr;         // pointer (or LPTR_NULL)
        v4 extra_id_pos;    // frag data
        sector_t *sect;     // sector associated with ptr (or NULL)
        plane_type_e plane; // plane if directly pointed
        v2 sect_pos;        // position on sector (world coords)
        side_t *side;       // side or NULL
        v2 side_pos;        // position on side
        tex_id_t texture;   // currently pointed to texture
        v4 hsva;            // currently pointed to hsva
        v3 pos_3d;          // 3D position of cursor
    } cam;

    struct {
        // oscillates between 0 and 1 for current highlight alpha
        f32 alpha;

        // visually highlighted level ptr, resets each frame
        lptr_t ptr;
    } highlight;

    // current open ptrs for editing
    DYNLIST(lptr_t) open_ptrs;

    // ptrs to open on next frame
    DYNLIST(lptr_t) to_open_ptrs;

    // things whose window should be brought to front
    DYNLIST(lptr_t) front_ptrs;

    // map of pointers which have changed (for visual indicator)
    // lptr_t -> nstime_t of change + change time
    map_t changed_ptr_to_time;

    // status_line and file_status change tracking
    struct {
        u64 change_tick;
        char last[256];
    } file_status, status_line;

    // if true, ui has mouse (don't interact with 3D world or 2D editor)
    bool ui_has_mouse;

    // if true, ui has keyboard focus (don't do any buttons)
    bool ui_has_keyboard;

    // true if ltext editor is open
    bool ltexts_open;

    // current error message
    char err_msg[1024];

    // last saved level->version
    int saved_version;

    // modals
    struct {
        bool new, open, save_as;
    } modal;

    // if "on", then level is loaded from level_path on next editor frame
    struct {
        bool on: 1;
        bool is_new: 1;
        char level_path[1024];
    } reload;

    // last bookmark which was cycled to
    int last_bookmark;

    // live bookmarks, NULL if no such entity
    entity_t *bookmarks[EDITOR_MAX_BOOKMARKS];
} editor_t;

// open an editor for a specific ptr, bringing it to front if already open
void editor_open(lptr_t ptr);

// initializes editor (one at program startup)
void editor_init();

// "do" editor frame
void editor_do_frame();

// try to save level to path, true on success
bool editor_try_save_level(const char *path, const char **errmsg);

// mark ptr as changed for visual indicator
void editor_mark_ptr_change(lptr_t ptr);
