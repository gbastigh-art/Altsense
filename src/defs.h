#pragma once

#include "util/enum.h"
#include "util/types.h"
#include "shared_defs.h" /* IWYU pragma: export */
#include "config.h" /* IWYU pragma: export */

enum { VERSION = 1 };

typedef enum {
    GAMEMODE_EDITOR,
    GAMEMODE_GAME,
    GAMEMODE_MAIN_MENU,
} game_mode_e;

#define ENUM_LEVEL_TYPE(F, ...)        \
    F(VERTEX, _LT_VERTEX, __VA_ARGS__) \
    F(WALL,   _LT_WALL,   __VA_ARGS__) \
    F(SIDE,   _LT_SIDE,   __VA_ARGS__) \
    F(SECTOR, _LT_SECTOR, __VA_ARGS__) \
    F(DECAL,  _LT_DECAL,  __VA_ARGS__) \
    F(ENTITY, _LT_ENTITY, __VA_ARGS__) \
    F(ROOM,   _LT_ROOM,   __VA_ARGS__) \

enum { LT_COUNT = _LT_COUNT  };
enum { LT_FIRST = _LT_VERTEX };
enum { LT_LAST  = _LT_ROOM   };

ENUM_DECL_NO_SPECIAL(level_type, LT, ENUM_LEVEL_TYPE)

#define ENUM_RENDER_TYPE(F, ...) \
    F(SIDE,   0,   __VA_ARGS__)  \
    F(SECTOR, 1,   __VA_ARGS__)  \
    F(DECAL,  2,   __VA_ARGS__)  \
    F(MODEL,  3,   __VA_ARGS__)  \
    F(SPRITE, 4,   __VA_ARGS__)  \

ENUM_DECL(render_type, RENDER_TYPE, ENUM_RENDER_TYPE)

// map type to LTF_*
#define LEVEL_TYPE_TO_LTF(_T) (_Generic((_T*) (NULL),     \
        vertex_t*: LTF_VERTEX,                            \
        const vertex_t*: LTF_VERTEX,                      \
        wall_t*: LTF_WALL,                                \
        const wall_t*: LTF_WALL,                          \
        side_t*: LTF_SIDE,                                \
        const side_t*: LTF_SIDE,                          \
        sector_t*: LTF_SECTOR,                            \
        const sector_t*: LTF_SECTOR,                      \
        decal_t*: LTF_DECAL,                              \
        const decal_t*: LTF_DECAL,                        \
        entity_t*: LTF_ENTITY,                            \
        const entity_t*: LTF_ENTITY,                      \
        room_t*: LTF_ROOM,                                \
        const room_t*: LTF_ROOM                           \
    ))

// map type to LT_*
#define LEVEL_TYPE_TO_LT(_T) (_Generic((_T*) (NULL), \
        vertex_t*: LT_VERTEX,                      \
        wall_t*: LT_WALL,                          \
        side_t*: LT_SIDE,                          \
        sector_t*: LT_SECTOR,                      \
        decal_t*: LT_DECAL,                        \
        entity_t*: LT_ENTITY,                      \
        room_t*: LT_ROOM,                          \
        const vertex_t*: LT_VERTEX,                \
        const wall_t*: LT_WALL,                    \
        const side_t*: LT_SIDE,                    \
        const sector_t*: LT_SECTOR,                \
        const decal_t*: LT_DECAL,                  \
        const entity_t*: LT_ENTITY,                \
        const room_t*: LT_ROOM                     \
    ))

// type of sector plane
#define ENUM_PLANE_TYPE(_F, ...)                        \
    _F(FLOOR, _PLANE_TYPE_FLOOR, __VA_ARGS__)           \
    _F(CEIL, _PLANE_TYPE_CEIL, __VA_ARGS__)             \

ENUM_DECL(plane_type, PLANE_TYPE, ENUM_PLANE_TYPE)

// segment of side
#define ENUM_SIDE_SEGMENT(F, ...) \
    F(BOTTOM, 0, __VA_ARGS__)     \
    F(MIDDLE, 1, __VA_ARGS__)     \
    F(TOP,    2, __VA_ARGS__)     \
    F(WALL,   3, __VA_ARGS__)     \

ENUM_DECL(side_segment, SIDE_SEGMENT, ENUM_SIDE_SEGMENT, u8)

// entity types
#define ENUM_ENTITY_TYPE(_F, ...)      \
    _F(PLACEHOLDER,  0,  __VA_ARGS__)  \
    _F(BOOKMARK,     1,  __VA_ARGS__)  \
    _F(PLAYER,       2,  __VA_ARGS__)  \
    _F(CRAWLER,      3,  __VA_ARGS__)  \
    _F(EBALL,        4,  __VA_ARGS__)  \
    _F(BULLET,       5,  __VA_ARGS__)  \
    _F(TURRET,       6,  __VA_ARGS__)  \
    _F(ENEMY_BULLET, 7,  __VA_ARGS__)  \
    _F(BIGMOUTH,     8,  __VA_ARGS__)  \
    _F(MOTHER,       9,  __VA_ARGS__)  \
    _F(SAC,          10, __VA_ARGS__)  \
    _F(SCION,        11, __VA_ARGS__)  \
    _F(HEAD,         12, __VA_ARGS__)  \
    _F(HEAD_POINT,   13, __VA_ARGS__)  \
    _F(FADE_ORIGIN,  14,  __VA_ARGS__) \
    _F(FILL_LIGHT,   15,  __VA_ARGS__) \
    _F(SPAWN_POINT,  16,  __VA_ARGS__) \
    _F(HEART,        17,  __VA_ARGS__) \

ENUM_DECL(entity_type, ENTITY_TYPE, ENUM_ENTITY_TYPE)

// decal types
#define ENUM_DECAL_TYPE(_F, ...)                          \
    _F(PLACEHOLDER, _DECAL_TYPE_PLACEHOLDER, __VA_ARGS__) \
    _F(HOLE,        _DECAL_TYPE_HOLE,        __VA_ARGS__) \
    _F(GORE,        _DECAL_TYPE_GORE,        __VA_ARGS__) \
    _F(BLOOD,       _DECAL_TYPE_BLOOD,       __VA_ARGS__) \

ENUM_DECL(decal_type, DECAL_TYPE, ENUM_DECAL_TYPE, u8)

// struct sector.type
#define ENUM_SECTOR_TYPE(_F, ...)        \
    _F(DEFAULT,          0, __VA_ARGS__) \
    _F(PAIN_JUICE,       1, __VA_ARGS__) \
    _F(ENTRY_JUICE,      2, __VA_ARGS__) \
    _F(HUB_JUICE,        3, __VA_ARGS__) \
    _F(DOOR,             4, __VA_ARGS__) \
    _F(EXIT_JUICE,       5, __VA_ARGS__) \
    _F(ROOM_ENTRY_JUICE, 6, __VA_ARGS__) \

ENUM_DECL(sector_type, SECTOR_TYPE, ENUM_SECTOR_TYPE, u8)

// SCFT_DIFF plat_type
#define ENUM_PLAT_TYPE_INDEX(_F, ...)                   \
    _F(MOVE_WHEN_ON, 0, __VA_ARGS__)                    \
    _F(TOGGLE_ON_ENTER, 1, __VA_ARGS__)                 \
    _F(MOVE_WAIT_BACK, 2, __VA_ARGS__)                  \

ENUM_DECL(plat_type_index, PLAT_TYPE, ENUM_PLAT_TYPE_INDEX)

// liquid type flags
enum {
    LIQUID_TYPE_FLAG_NONE = 0 << 0,

    // if true, liquid has light (light or light_fn)
    LIQUID_TYPE_FLAG_LIGHT = 1 << 0,

    // if true, camera movement is disabled inside of liquid
    LIQUID_TYPE_FLAG_NO_MOVE = 1 << 1,
};

// liquid types
#define ENUM_LIQUID_TYPE_INDEX(_F, ...)                 \
    _F(NONE, 0, __VA_ARGS__)                            \
    _F(WATER, 1, __VA_ARGS__)                           \
    _F(PAIN_JUICE, 2, __VA_ARGS__)                      \
    _F(EXIT_JUICE, 3, __VA_ARGS__)                      \
    _F(TELEPORT_JUICE, 4, __VA_ARGS__)                  \

ENUM_DECL(liquid_type_index, LIQUID_TYPE, ENUM_LIQUID_TYPE_INDEX)

// debug visual options
#define ENUM_VISOPT(_F, ...)                            \
    _F(GRID,         _VISOPT_GRID,         __VA_ARGS__) \
    _F(SHOW_ID,      _VISOPT_SHOW_ID,      __VA_ARGS__) \
    _F(FULLBRIGHT,   _VISOPT_FULLBRIGHT,   __VA_ARGS__) \
    _F(HIGHLIGHT,    _VISOPT_HIGHLIGHT,    __VA_ARGS__) \
    _F(HIGHLIGHT_3D, _VISOPT_HIGHLIGHT_3D, __VA_ARGS__) \
    _F(HOVERSECT,    _VISOPT_HOVERSECT,    __VA_ARGS__) \
    _F(SHOWSECT,     _VISOPT_SHOWSECT,     __VA_ARGS__) \
    _F(BLOCKS,       _VISOPT_BLOCKS,       __VA_ARGS__) \
    _F(GEO,          _VISOPT_GEO,          __VA_ARGS__) \
    _F(SUBNEIGHBORS, _VISOPT_SUBNEIGHBORS, __VA_ARGS__) \
    _F(PVS,          _VISOPT_PVS,          __VA_ARGS__) \
    _F(REACHABLE,    _VISOPT_REACHABLE,    __VA_ARGS__) \
    _F(NEAR,         _VISOPT_NEAR,         __VA_ARGS__) \
    _F(EVS,          _VISOPT_EVS,          __VA_ARGS__) \
    _F(COLLIDERS,    _VISOPT_COLLIDERS,    __VA_ARGS__) \
    _F(HITBOXES,     _VISOPT_HITBOXES,     __VA_ARGS__) \
    _F(NODANCE,      _VISOPT_NODANCE,      __VA_ARGS__) \
    _F(VALUE,        _VISOPT_VALUE,        __VA_ARGS__) \
    _F(TRACE,        _VISOPT_TRACE,        __VA_ARGS__) \
    _F(SGL_WIRE,     _VISOPT_SGL_WIRE,     __VA_ARGS__) \
    _F(AI_VIS,       _VISOPT_AI_VIS,       __VA_ARGS__) \
    _F(FAST_PVS,     _VISOPT_FAST_PVS,     __VA_ARGS__) \

ENUM_DECL(visopt, VISOPT, ENUM_VISOPT)

typedef union ivec2s v2i;
typedef union ivec3s v3i;
typedef union vec2s v2;
typedef union vec3s v3;
typedef union vec4s v4;
typedef union mat2s m2;
typedef union mat3s m3;
typedef union mat4s m4;
typedef struct box2i box2i_t;
typedef struct box2f box2f_t;

// sokol
typedef struct sg_buffer      sg_buffer;
typedef struct sg_image       sg_image;
typedef struct sg_sampler     sg_sampler;
typedef struct sg_shader      sg_shader;
typedef struct sg_pipeline    sg_pipeline;
typedef struct sg_attachments sg_attachments;

// game
typedef struct level level_t;
typedef struct editor editor_t;
typedef struct renderer renderer_t;
typedef struct level_vertex level_vertex_t;
typedef struct model_vertex model_vertex_t;
typedef struct palette palette_t;
typedef struct tex_atlas tex_atlas_t;
typedef struct tex_atlas_entry tex_atlas_entry_t;
typedef struct input input_t;
typedef union lptr lptr_t;
typedef struct level_fields level_fields_t;
typedef struct vertex vertex_t;
typedef struct wall wall_t;
typedef struct side side_t;
typedef struct side_segment side_segment_t;
typedef union side_segments side_segments_t;
typedef struct sector sector_t;
typedef struct sect_line sect_line_t;
typedef struct subsector subsector_t;
typedef struct entity entity_t;
typedef struct decal decal_t;
typedef struct particle particle_t;
typedef struct block block_t;
typedef struct entity_type entity_type_t;
typedef struct sidemat_data sidemat_data_t;
typedef struct sectmat_data sectmat_data_t;
typedef struct sprite_inst_desc sprite_inst_desc_t;
typedef struct sound_state sound_state_t;
typedef struct sprite sprite_t;
typedef struct light_t light_t;
typedef struct light_desc light_desc_t;
typedef struct { u16 index; } model_id_t;
typedef struct model_atlas model_atlas_t;
typedef struct model_data model_data_t;
typedef struct model model_t;
typedef struct rand rand_t;
typedef struct entity_damage_desc entity_damage_desc_t;
typedef struct reload_state reload_state_t;
typedef struct reload_curve reload_curve_t;
typedef struct room room_t;
typedef struct { u16 index; } tex_id_t;
typedef struct trace_hit trace_hit_t;
typedef struct particle_sim particle_sim_t;
typedef struct particle_inst_desc particle_inst_desc_t;
typedef struct ltext ltext_t;
typedef struct mtextgen mtextgen_t;
