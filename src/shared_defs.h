#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

// defs shared between shaders and C files

// for struct members, use STD140_* to keep alignment interop happy :)
#ifdef __STDC__
#include "util/math.h"

#define M_SHARED_STRUCT_16(NAME) \
    typedef struct NAME NAME;    \
    struct M_PACKED_ALIGNED(16) NAME

#define M_SHARED_STRUCT_8(NAME)  \
    typedef struct NAME NAME;    \
    struct M_PACKED_ALIGNED(8) NAME

#define M_SHARED_STRUCT_4(NAME)  \
    typedef struct NAME NAME;    \
    struct M_PACKED_ALIGNED(4) NAME

#define STD140_VEC2 __attribute__((aligned(8))) v2
#define STD140_VEC3 __attribute__((aligned(16))) v3
#define STD140_VEC4 __attribute__((aligned(16))) v4
#define STD140_FLOAT __attribute__((aligned(4))) f32
#define STD140_INT __attribute__((aligned(4))) int

#else

#define M_SHARED_STRUCT_16(NAME) struct NAME
#define M_SHARED_STRUCT_8(NAME)  struct NAME
#define M_SHARED_STRUCT_4(NAME)  struct NAME

#define STD140_VEC2  vec2
#define STD140_VEC3  vec3
#define STD140_VEC4  vec4
#define STD140_FLOAT float
#define STD140_INT   int

#endif // ifdef __STDC__

// indicates a texture is in the virtual layer - see atlas.c/h
#define TEX_ATLAS_LAYER_VIRTUAL 255

#define _VISOPT_GRID         (1 << 0)
#define _VISOPT_SHOW_ID      (1 << 1)
#define _VISOPT_FULLBRIGHT   (1 << 2)
#define _VISOPT_HIGHLIGHT    (1 << 3)
#define _VISOPT_HIGHLIGHT_3D (1 << 4)
#define _VISOPT_HOVERSECT    (1 << 5)
#define _VISOPT_SHOWSECT     (1 << 6)
#define _VISOPT_BLOCKS       (1 << 7)
#define _VISOPT_GEO          (1 << 8)
#define _VISOPT_SUBNEIGHBORS (1 << 9)
#define _VISOPT_PVS          (1 << 10)
#define _VISOPT_REACHABLE    (1 << 11)
#define _VISOPT_NEAR         (1 << 12)
#define _VISOPT_EVS          (1 << 13)
#define _VISOPT_COLLIDERS    (1 << 14)
#define _VISOPT_HITBOXES     (1 << 15)
#define _VISOPT_NODANCE      (1 << 16)
#define _VISOPT_VALUE        (1 << 17)
#define _VISOPT_TRACE        (1 << 18)
#define _VISOPT_SGL_WIRE     (1 << 19)
#define _VISOPT_AI_VIS       (1 << 20)
#define _VISOPT_FAST_PVS     (1 << 21)

// level element types as flat indices
#define _LT_VERTEX  0
#define _LT_WALL    1
#define _LT_SIDE    2
#define _LT_SECTOR  3
#define _LT_DECAL   4
#define _LT_ENTITY  5
#define _LT_ROOM    6
#define _LT_COUNT   7

// level element types (T_*) as bit flags
#define LTF_VERTEX    (1 << 0)
#define LTF_WALL      (1 << 1)
#define LTF_SIDE      (1 << 2)
#define LTF_SECTOR    (1 << 3)
#define LTF_DECAL     (1 << 4)
#define LTF_ENTITY    (1 << 5)
#define LTF_ROOM      (1 << 6)

// side mat info flags
#define SDMF_NONE        (0)
#define SDMF_BOT_ABS     (1 << 0)
#define SDMF_TOP_ABS     (1 << 1)
#define SDMF_EZPORT      (1 << 2)
#define SDMF_PEG         (1 << 3)
#define SDMF_MID_NO_STOP (1 << 4)
#define SDMF_MID         (1 << 5)
#define SDMF_SCROLL_H    (1 << 6)
#define SDMF_SCROLL_V    (1 << 7)
#define SDMF_SKY         (1 << 8)
#define SDMF_EZX         (1 << 9)
#define SDMF_OV_SCROLL_H (1 << 10)
#define SDMF_OV_SCROLL_V (1 << 11)
#define SDMF_OV_SCRY     (1 << 12)
#define SDMF_TRUE_COLOR  (1 << 13)

// sector mat info flags
#define SCMF_NONE        (0)
#define SCMF_SKY         (1 << 0)
#define SCMF_SCROLL_H    (1 << 1)
#define SCMF_SCROLL_V    (1 << 2)
#define SCMF_OV_SCROLL_H (1 << 3)
#define SCMF_OV_SCROLL_V (1 << 4)
#define SCMF_SCROLL_INV  (1 << 5)

#define _PLANE_TYPE_FLOOR 0
#define _PLANE_TYPE_CEIL  1

// gfx flags
#define GFX_NO_FLAGS       (0 << 0)
#define GFX_FLIP_X         (1 << 0)
#define GFX_FLIP_Y         (1 << 1)
#define GFX_ROTATE_CW      (1 << 2)
#define GFX_ROTATE_CCW     (1 << 3)
#define GFX_DITHER         (1 << 4)
#define GFX_DITHER_INTENSE (1 << 5)

#define SPRITE_FLAG_NONE        (0 << 0)
#define SPRITE_FLAG_FACE_CAMERA (1 << 0)

// model render flags
#define MRF_NONE                (0 << 0)
#define MRF_OVERLAY_SCROLL_H    (1 << 0)
#define MRF_OVERLAY_SCROLL_V    (1 << 1)
#define MRF_OVERLAY_SCROLL_INV  (1 << 2)
#define MRF_FIRST_PERSON        (1 << 3)
#define MRF_FINGER              (1 << 4)
#define MRF_LEFT_HAND           (1 << 5)
#define MRF_RIGHT_HAND          (1 << 6)
#define MRF_ANY_HAND            (MRF_LEFT_HAND | MRF_RIGHT_HAND)
#define MRF_BULLET              (1 << 7)
#define MRF_ENEMY               (1 << 8)
#define MRF_OVERLAY_POST        (1 << 9)

// decal types
#define _DECAL_TYPE_PLACEHOLDER 0
#define _DECAL_TYPE_HOLE 1
#define _DECAL_TYPE_GORE 2
#define _DECAL_TYPE_BLOOD 3

// particle types
#define PARTICLE_TYPE_PLACEHOLDER     0
#define PARTICLE_TYPE_SMOKE           1
#define PARTICLE_TYPE_BLOOD           2
#define PARTICLE_TYPE_SPAWN           3
#define PARTICLE_TYPE_SPARK           4
#define PARTICLE_TYPE_FLOATER         5
#define PARTICLE_TYPE_AMBIENT_FLOATER 6
#define PARTICLE_TYPE_PORTAL          7
#define PARTICLE_TYPE_ENEMY_BULLET    8
#define PARTICLE_TYPE_SAC_GAS         9
#define PARTICLE_TYPE_RICOCHET        10
#define PARTICLE_TYPE_COUNT           (PARTICLE_TYPE_RICOCHET + 1)

#define LIGHT_TYPE_NONE 0
#define LIGHT_TYPE_FLOOR 1
#define LIGHT_TYPE_CEIL 2
#define LIGHT_TYPE_LIQUID 3
#define LIGHT_TYPE_ENV 4
#define LIGHT_TYPE_PARTICLE 5
#define LIGHT_TYPE_SIDE 6
#define LIGHT_TYPE_ENTITY 7
#define LIGHT_TYPE_DECAL 8

// type_ is LIGHT_TYPE_* or 0
// index_ is a unique index for the tag_ and type_
// stored in 32-bit int as (type << 16) | index
#define LIGHT_ID_FROM(type_, index_) (((type_) << 16) | ((index_) & 0xFFFF))

// get LIGHT_TYPE_* from light id
#define LIGHT_ID_TYPE(id_) (((id_) >> 16) & 0xFFFF)

// get index from light id
#define LIGHT_ID_INDEX(id_) ((id_) & 0xFFFF)

#ifdef __STDC__
typedef i32 light_id_t;
#endif // ifdef __STDC__

// MUST MATCH WITH defs.h
#ifndef __STDC__
#define RENDER_TYPE_SIDE     0
#define RENDER_TYPE_SECTOR   1
#define RENDER_TYPE_DECAL    2
#define RENDER_TYPE_MODEL    3
#define RENDER_TYPE_SPRITE   4
#define RENDER_TYPE_MASK     0x7 // bottom 3 bits
#endif // ifndef __STDC__

// NOTE: render flags can only go up to (1 << 15) since they're stored alongside
// the buffer index!
#define RENDER_FLAG_NONE           (0)
#define RENDER_FLAG_IS_CEIL        (1 << 3)
#define RENDER_FLAG_SEG_BOTTOM     (1 << 4)
#define RENDER_FLAG_SEG_MIDDLE     (1 << 5)
#define RENDER_FLAG_SEG_TOP        (1 << 6)
#define RENDER_FLAG_PORTAL         (1 << 7)
#define RENDER_FLAG_SGL            (1 << 8)
#define RENDER_FLAG_LIQUID         (1 << 9)
#define RENDER_FLAG_PARTICLE       (1 << 10)
#define RENDER_FLAG_SPECIAL_ID     (1 << 11)
#define RENDER_FLAG_FANCY_PARTICLE (1 << 12)
#define RENDER_FLAG_SKY            (1 << 13)
#define RENDER_FLAG_TRUE_COLOR     (1 << 14)

// for RENDER_FLAG_SPECIAL_ID
#define RENDER_SPECIAL_ID_TAG    (1 << 15)

#define LIGHT_FLAG_NONE         (0)
#define LIGHT_FLAG_NO_SHADOWS   (1 << 0)
#define LIGHT_FLAG_SECTOR       (1 << 1)
#define LIGHT_FLAG_CEIL         (1 << 2)
#define LIGHT_FLAG_SIDE         (1 << 3)
#define LIGHT_FLAG_ENTITY       (1 << 4)
#define LIGHT_FLAG_IGNORE_NEAR  (1 << 5)
#define LIGHT_FLAG_DISABLE      (1 << 6)
#define LIGHT_FLAG_PARTICLE     (1 << 7)

M_SHARED_STRUCT_16(light_t) {
    STD140_VEC3  pos;
    STD140_FLOAT attenuation;
    STD140_VEC3  color;
    STD140_INT   flags;
    STD140_INT   shadow_index;
    STD140_FLOAT span;
    STD140_FLOAT c1;
    STD140_FLOAT c2;
    STD140_FLOAT ambient;
    STD140_FLOAT power;
    STD140_FLOAT z_attenuation;
    STD140_INT   id;
    STD140_VEC4  plane;
};

M_SHARED_STRUCT_16(block_wall_data_t) {
    STD140_VEC2 a;
    STD140_VEC2 b;
    STD140_INT sects[2];
    STD140_INT sides[2];
    STD140_FLOAT zbls[2];
    STD140_FLOAT zbrs[2];
    STD140_FLOAT ztls[2];
    STD140_FLOAT ztrs[2];
    STD140_VEC4 floors[2];
    STD140_VEC4 ceils[2];
};

// NOTE: 4-byte packed, no vec3/4 members
M_SHARED_STRUCT_4(block_walls_indices_t) {
    STD140_INT offset, count;
};

M_SHARED_STRUCT_16(side_render_data_t) {
    STD140_FLOAT z_floor;
    STD140_FLOAT z_ceil;
    STD140_FLOAT nz_floor;
    STD140_FLOAT nz_ceil;
    STD140_VEC2  offsets;
    STD140_FLOAT split_bottom;
    STD140_FLOAT split_top;
    STD140_INT tex_low;
    STD140_INT tex_mid;
    STD140_INT tex_high;
    STD140_INT sidemat_flags;
    STD140_INT flags;
    STD140_INT sector_index;
    STD140_INT tex_overlay;
    STD140_FLOAT overlay_alpha;
    STD140_VEC4  hsva;
    STD140_FLOAT mzbl;
    STD140_FLOAT mzbr;
    STD140_FLOAT mztl;
    STD140_FLOAT mztr;
    STD140_VEC2  a;
    STD140_VEC2  b;
};

M_SHARED_STRUCT_16(sector_render_data_t) {
    STD140_INT tex_floor;
    STD140_INT tex_ceil;
    STD140_INT flags;
    STD140_INT sectmat_flags;
    STD140_VEC2 floor_offsets;
    STD140_VEC2 ceil_offsets;
    STD140_VEC4 floor_hsva;
    STD140_VEC4 ceil_hsva;
    STD140_VEC3 liquid_hsv;
    STD140_VEC4 liquid_extra_bloom;
    STD140_INT tex_liquid;
    STD140_INT tex_overlay_floor;
    STD140_INT tex_overlay_ceil;
    STD140_FLOAT overlay_alpha_floor;
    STD140_FLOAT overlay_alpha_ceil;
};

M_SHARED_STRUCT_16(sprite_render_data_t) {
    STD140_INT id;
    STD140_INT tex;
    STD140_VEC4 extra_bloom;
    STD140_VEC3 extra_light;
};

M_SHARED_STRUCT_16(model_render_data_t) {
    STD140_INT   id;
    STD140_INT   tex;
    STD140_INT   tex_overlay;
    STD140_FLOAT overlay_alpha;
    STD140_VEC3  hsv;
    STD140_INT   flags; // MRF_*
    STD140_VEC4  tint;
    STD140_VEC4  extra_bloom;
    STD140_VEC3  extra_light;
    STD140_FLOAT spawn_time;  // based on total_scaled_ns
    STD140_INT   corpse_tick; // tick when model became corpse
    STD140_VEC3  m_centroid;  // model centroid
};

M_SHARED_STRUCT_16(decal_render_data_t) {
    STD140_INT   tex;
    STD140_INT   type;
    STD140_INT   sector_index;
    STD140_INT   side_index;
    STD140_INT   plane_type;
    STD140_FLOAT rotation;
    STD140_VEC4  tint;
    STD140_VEC2  pos;
    STD140_VEC2  size; // world units
};

// NOTE: 8-byte backed for vec2 member
M_SHARED_STRUCT_8(tex_atlas_entry_info_t) {
    STD140_VEC2 uv_min;
    STD140_VEC2 uv_max;
    STD140_INT  layer;
    STD140_INT  anim_ticks_per_frame;
    STD140_INT  anim_width;
};

M_SHARED_STRUCT_8(gpu_psim_particle_t) {
    STD140_VEC2  pos;
    STD140_FLOAT density;
    STD140_INT   is_right;
};

M_SHARED_STRUCT_4(gpu_psim_cell_t) {
    int start, count;
};

#endif // ifndef SHARED_DEFS_H
