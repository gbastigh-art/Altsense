#include "level/io.h"
#include "gfx/tex_atlas.h"
#include "level/block.h"
#include "level/decal.h"
#include "level/lptr.h"
#include "level/level_types.h"
#include "level/level.h"
#include "defs.h"
#include "level/entity.h"
#include "level/room.h"
#include "level/sector.h"
#include "level/side.h"
#include "level/vertex.h"
#include "level/wall.h"
#include "util/alloc.h"
#include "util/any.h"
#include "util/kvstore.h"
#include "util/time.h"
#include "vtext.h"

// BIG OL' OPTIMIZATION OPPORTUNITY
// most of what we save are zeros - how about instead of doing that we just omit
// fields that are trivially zero and skip saving/loading like 80% of our data?

// turn on to print extra debug info :)
// #define DO_DEBUG_IO

#ifdef DO_DEBUG_IO
    #define DEBUG_IO(...) LOG(__VA_ARGS__)
#else
    #define DEBUG_IO(...)
#endif // ifdef DO_DEBUG_IO

// recognized IO types
#define ENUM_IO_TYPE(F, ...)         \
    F(NONE,         0,  __VA_ARGS__) \
    F(VERTEX,       1,  __VA_ARGS__) \
    F(WALL,         2,  __VA_ARGS__) \
    F(SIDE,         3,  __VA_ARGS__) \
    F(SECTOR,       4,  __VA_ARGS__) \
    F(DECAL,        5,  __VA_ARGS__) \
    F(ENTITY,       6,  __VA_ARGS__) \
    F(ROOM,         7,  __VA_ARGS__) \
    F(BYTE_ARRAY,   8,  __VA_ARGS__) \
    F(F32,          9,  __VA_ARGS__) \
    F(BOOL,         10, __VA_ARGS__) \
    F(U8,           11, __VA_ARGS__) \
    F(U16,          12, __VA_ARGS__) \
    F(U32,          13, __VA_ARGS__) \
    F(U64,          14, __VA_ARGS__) \
    F(I8,           15, __VA_ARGS__) \
    F(I16,          16, __VA_ARGS__) \
    F(I32,          17, __VA_ARGS__) \
    F(I64,          18, __VA_ARGS__) \
    F(INT,          19, __VA_ARGS__) \
    F(UINT,         20, __VA_ARGS__) \
    F(V2I,          21, __VA_ARGS__) \
    F(V2,           22, __VA_ARGS__) \
    F(V3,           23, __VA_ARGS__) \
    F(V4,           24, __VA_ARGS__) \
    F(PTR_VERTEX,   25, __VA_ARGS__) \
    F(PTR_WALL,     26, __VA_ARGS__) \
    F(PTR_SIDE,     27, __VA_ARGS__) \
    F(PTR_SECTOR,   28, __VA_ARGS__) \
    F(PTR_DECAL,    29, __VA_ARGS__) \
    F(PTR_ENTITY,   30, __VA_ARGS__) \
    F(PTR_ROOM,     31, __VA_ARGS__) \
    F(SIDEMAT_DATA, 32, __VA_ARGS__) \
    F(SECTMAT_DATA, 33, __VA_ARGS__) \
    F(LIGHT_PARAMS, 34, __VA_ARGS__) \
    F(LPTR_NOGEN,   35, __VA_ARGS__) \
    F(TEX_ID,       36, __VA_ARGS__) \
    F(STR,          37, __VA_ARGS__) \

ENUM_DECL(io_type, IO_TYPE, ENUM_IO_TYPE)

ENUM_DEFINE(io_error, IO,      ENUM_IO_ERROR)
ENUM_DEFINE(io_type,  IO_TYPE, ENUM_IO_TYPE)

typedef struct {
    // io-operation-lifetime arena
    allocator_t arena;

    // level being saved/loaded
    level_t *level;

    // i32 (current index) -> i32 (save index) index map for level types
    // only used on level save
    map_t level_index_maps[LT_COUNT];

    // maps used tex id -> texture name, saved out as part of level data
    // populated on level saved, used on level load
    map_t tex_id_to_name;
} io_t;

typedef struct {
    // fx. "portal"
    const char *name;

    // fx. "side_t.portal"
    const char *qualified_name;

    uint offset;
    uint size;
    io_type_e type;
} io_field_t;

typedef struct io_type io_type_t;

typedef struct io_type {
    io_type_e type;

    io_error_e (*to_any_fn)(
        io_t*, const io_type_t*, any_t*, const range_t*);
    io_error_e (*from_any_fn)(
        io_t*, const io_type_t*, const range_t*, const any_t*);

    // only for aggregate types
    io_field_t fields[32];

    // only for level (also PTR_*) types
    level_type_e level_type;
} io_type_t;

#define IO_TYPE_OF(_T) _Generic(*((_T*)(NULL)), \
    f32: IO_TYPE_F32,                           \
    bool: IO_TYPE_BOOL,                         \
    u8: IO_TYPE_U8,                             \
    u16: IO_TYPE_U16,                           \
    u32: IO_TYPE_U32,                           \
    u64: IO_TYPE_U64,                           \
    i8: IO_TYPE_I8,                             \
    i16: IO_TYPE_I16,                           \
    i32: IO_TYPE_I32,                           \
    i64: IO_TYPE_I64,                           \
    v2i: IO_TYPE_V2I,                           \
    v2: IO_TYPE_V2,                             \
    v3: IO_TYPE_V3,                             \
    v4: IO_TYPE_V4,                             \
    vertex_t*: IO_TYPE_PTR_VERTEX,              \
    wall_t*: IO_TYPE_PTR_WALL,                  \
    side_t*: IO_TYPE_PTR_SIDE,                  \
    sector_t*: IO_TYPE_PTR_SECTOR,              \
    decal_t*: IO_TYPE_PTR_DECAL,                \
    entity_t*: IO_TYPE_PTR_ENTITY,              \
    room_t*: IO_TYPE_PTR_ROOM,                  \
    vertex_t: IO_TYPE_VERTEX,                   \
    wall_t: IO_TYPE_WALL,                       \
    side_t: IO_TYPE_SIDE,                       \
    sector_t: IO_TYPE_SECTOR,                   \
    decal_t: IO_TYPE_DECAL,                     \
    entity_t: IO_TYPE_ENTITY,                   \
    room_t: IO_TYPE_ROOM,                       \
    sidemat_data_t: IO_TYPE_SIDEMAT_DATA,       \
    sectmat_data_t: IO_TYPE_SECTMAT_DATA,       \
    light_params_t: IO_TYPE_LIGHT_PARAMS,       \
    tex_id_t:       IO_TYPE_TEX_ID,             \
    const char*:    IO_TYPE_STR,                \
    char*:          IO_TYPE_STR,                \
    lptr_nogen_t: IO_TYPE_LPTR_NOGEN)

#define IO_TYPE_OF_VALUE(v) (IO_TYPE_OF(typeof((v))))

static const io_type_t IO_TYPES[IO_TYPE_COUNT];

#define MK_BASIC_TO_FROM_FUNCS(T, A)                                            \
    static io_error_e T##_to_any(                                               \
        io_t*, const io_type_t*, any_t *dst, const range_t *src) {              \
        ASSERT(src->size == sizeof(T));                                         \
        any_set_##T(dst, *(T*) (src->ptr));                                     \
        return IO_OK;                                                           \
    }                                                                           \
    static io_error_e T##_from_any(                                             \
        io_t*, const io_type_t*, const range_t *dst, const any_t *src) {        \
        ASSERT(dst->size == sizeof(T));                                         \
        T val;                                                                  \
        any_t copy = any_create(NULL);                                          \
        if (!any_try_massage_type(&copy, src, A)) {                             \
            ERROR("could massage %s from %s", #T, any_to_str(src, tlscratch()));\
            return IO_BAD_VALUE;                                                \
        }                                                                       \
        if (!any_get_##T(&copy, &val)) {                                        \
            ERROR("could not get %s from %s", #T, any_to_str(src, tlscratch()));\
            return IO_BAD_VALUE;                                                \
        }                                                                       \
        memcpy(dst->ptr, &val, sizeof(T));                                      \
        return IO_OK;                                                           \
    }

MK_BASIC_TO_FROM_FUNCS(f32,  ANY_F32)
MK_BASIC_TO_FROM_FUNCS(bool, ANY_BOOL)
MK_BASIC_TO_FROM_FUNCS(u8,   ANY_U8)
MK_BASIC_TO_FROM_FUNCS(u16,  ANY_U16)
MK_BASIC_TO_FROM_FUNCS(u32,  ANY_U32)
MK_BASIC_TO_FROM_FUNCS(u64,  ANY_U64)
MK_BASIC_TO_FROM_FUNCS(i8,   ANY_I8)
MK_BASIC_TO_FROM_FUNCS(i16,  ANY_I16)
MK_BASIC_TO_FROM_FUNCS(i32,  ANY_I32)
MK_BASIC_TO_FROM_FUNCS(i64,  ANY_I64)
MK_BASIC_TO_FROM_FUNCS(uint, ANY_UINT)
MK_BASIC_TO_FROM_FUNCS(int,  ANY_INT)
MK_BASIC_TO_FROM_FUNCS(v2,   ANY_V2)
MK_BASIC_TO_FROM_FUNCS(v3,   ANY_V3)
MK_BASIC_TO_FROM_FUNCS(v4,   ANY_V4)
MK_BASIC_TO_FROM_FUNCS(v2i,  ANY_V2I)

static io_error_e str_to_any(
        io_t *io,
        const io_type_t *type,
        any_t *dst,
        const range_t *src) {
    const char *value = *(char**) src->ptr;
    if (value && *value) {
        any_set_str(dst, value);
    } else {
        any_set_str(dst, "");
    }

    return IO_OK;
}

static io_error_e str_from_any(
        io_t *io,
        const io_type_t *type,
        const range_t *dst,
        const any_t *src) {
    if (!any_is(src, ANY_STR)){
        WARN("%s is not str", any_to_str(src, tlscratch()));
        return IO_BAD_VALUE;
    }

    const char *str = any_get_str(src);

    char **pvalue = (char**) dst->ptr;
    if (str && *str) {
        *pvalue = mem_strdup(&io->level->arena, str);
    } else {
        *pvalue = NULL;
    }

    return IO_OK;
}

// LT_* -> IO_TYPE_*
static io_type_e level_type_to_io_type(level_type_e lt) {
    switch (lt) {
    case LT_VERTEX: return IO_TYPE_VERTEX;
    case LT_WALL:   return IO_TYPE_WALL;
    case LT_SIDE:   return IO_TYPE_SIDE;
    case LT_SECTOR: return IO_TYPE_SECTOR;
    case LT_DECAL:  return IO_TYPE_DECAL;
    case LT_ENTITY: return IO_TYPE_ENTITY;
    case LT_ROOM:   return IO_TYPE_ROOM;
    default: ASSERT(false, "%d", lt);
    }
}

// IO_TYPE_PTR_* -> any_t
static io_error_e ptr_to_any(
        io_t *io,
        const io_type_t *type,
        any_t *dst,
        const range_t *src) {
    const void *value = *(void**) src->ptr;
    if (value) {
        lptr_t lptr;
        ASSERT(lptr_from_raw(io->level, type->level_type, value, &lptr));

        const i32 cur_index = lptr_level_fields(io->level, lptr)->id;

        // convert to iter index, must be present in map
        const i32 *iter_index =
            map_get(int, &io->level_index_maps[type->level_type], cur_index);

        ASSERT(iter_index);

        any_set_i32(dst, *iter_index);
    } else {
        any_set_i32(dst, -1);
    }

    return IO_OK;
}

// any_t -> IO_TYPE_PTR_*
static io_error_e ptr_from_any(
        io_t *io,
        const io_type_t *type,
        const range_t *dst,
        const any_t *src) {
    // doesn't need allocator, should be convertible to i32
    any_t copy = any_create(NULL);

    if (!any_try_massage_type(&copy, src, ANY_I32)) {
        WARN("could not massage %s to i32?", any_to_str(src, tlscratch()));
        return IO_BAD_VALUE;
    }

    const i32 value = any_get_i32_or_default(&copy, -1);

    void **pvalue = (void**) dst->ptr;

    if (value == -1) {
        *pvalue = NULL;
        return IO_OK;
    }

    const genlist_t *list = &io->level->lists[type->level_type];
    const genlist_handle_t handle = genlist_handle_of_index(list, value);

    if (genlist_handle_is_null(handle)) {
        ERROR(
            "bad index %d for type %s (setting ptr to NULL)",
            value,
            level_type_to_str(type->level_type));
        return IO_BAD_INDEX;
    }

    *pvalue = genlist_try_ptr_voidp(list, handle);

    // intenral error, must be present if handle is not NULL
    ASSERT(*pvalue);

    return IO_OK;
}

static io_error_e generic_to_any(
        io_t *io,
        const io_type_t *type,
        any_t *dst,
        const range_t *src) {
    DEBUG_IO("generic_to_any %s", io_type_to_str(type->type));

    kvstore_t *kvs = any_set_kvstore(dst, NULL);

    const io_field_t *field = &type->fields[0];
    while (field->type != IO_TYPE_NONE) {
        DEBUG_IO("  field %s to any", io_type_to_str(field->type));
        any_t value = any_create(&io->arena);
        const io_error_e err =
            IO_TYPES[field->type].to_any_fn(
                io,
                &IO_TYPES[field->type],
                &value,
                &(range_t) { ((u8*) src->ptr) + field->offset, field->size });

        if (err != IO_OK) {
            WARN("error to_any'ing field %s", field->qualified_name);
            return err;
        }

        kvstore_move_value(kvs, field->name, &value);
        field++;
    }

    return IO_OK;
}

static io_error_e generic_from_any(
        io_t *io,
        const io_type_t *type,
        const range_t *dst,
        const any_t *src) {
    DEBUG_IO("generic_from_any %s", io_type_to_str(type->type));
    ASSERT(any_is(src, ANY_KVSTORE));
    const kvstore_t *kvs = any_get_kvstore(src);

    const io_field_t *field = &type->fields[0];
    while (field->type != IO_TYPE_NONE) {
        DEBUG_IO("  field %s from any", io_type_to_str(field->type));
        const any_t *value = kvstore_get(kvs, field->name);

        if (!value) {
            WARN(
                "missing field %s on type %s",
                field->qualified_name,
                io_type_to_str(type->type));
            // TODO: not actually fatal?
            // return IO_MISSING_FIELD;

            // zero init and continue
            memset(((u8*) dst->ptr) + field->offset, 0, field->size);
            goto next_field;
        }

        const io_error_e err =
            IO_TYPES[field->type].from_any_fn(
                io,
                &IO_TYPES[field->type],
                &(range_t) { ((u8*) dst->ptr) + field->offset, field->size },
                value);

        if (err != IO_OK) {
            WARN("error from_any'ing field %s", field->qualified_name);
            return err;
        }

next_field:
        field++;
    }

    return IO_OK;
}

static io_error_e lptr_nogen_to_any(
        io_t *io,
        const io_type_t *type,
        any_t *dst,
        const range_t *src) {
    ASSERT(src->size == sizeof(lptr_nogen_t));
    lptr_nogen_t ptr = *(lptr_nogen_t*) src->ptr;
    any_set_i32(dst, ((i32) (ptr.type << 16)) | ptr.index);
    return IO_OK;
}

static io_error_e lptr_nogen_from_any(
        io_t *io,
        const io_type_t *type,
        const range_t *dst,
        const any_t *src) {
    ASSERT(dst->size == sizeof(lptr_nogen_t));
    lptr_nogen_t *pptr = (lptr_nogen_t*) dst->ptr;

    if (src->type == ANY_KVSTORE) {
        *pptr = (lptr_nogen_t) { 0 };
        return IO_OK;
    }

    // doesn't need allocator, should be convertible to i32
    any_t copy = any_create(NULL);

    if (!any_try_massage_type(&copy, src, ANY_I32)) {
        WARN("could not massage %s to i32?", any_to_str(src, tlscratch()));
        return IO_BAD_VALUE;
    }

    i32 value;
    if (!any_get_i32(&copy, &value)) {
        WARN("could not get %s as i32?", any_to_str(src, tlscratch()));
        return IO_BAD_VALUE;
    } else if (!level_type_is_valid(value >> 16)) {
        WARN(
            "bad level type for %s (0x%08x)",
            any_to_str(src, tlscratch()),
            value);
        return IO_BAD_VALUE;
    }

    *pptr = (lptr_nogen_t) {
        .type = (level_type_e) (value >> 16),
        .index = value & 0xFFFF,
    };

    return IO_OK;
}

static io_error_e tex_id_to_any(
        io_t *io,
        const io_type_t *type,
        any_t *dst,
        const range_t *src) {
    ASSERT(src->size == sizeof(tex_id_t));
    const tex_id_t id = *(tex_id_t*) src->ptr;

    // register with tex id -> name map
    if (!map_contains(&io->tex_id_to_name, id.index)) {
        // optimization opportunity: unnecessary (but defensive) strdup
        map_insert(
            &io->tex_id_to_name,
            id.index,
            mem_strdup(&io->arena, tex_atlas_entry_by_id(id)->name));
    }

    any_set_i32(dst, (i32) id.index);
    return IO_OK;
}

static io_error_e tex_id_from_any(
        io_t *io,
        const io_type_t *type,
        const range_t *dst,
        const any_t *src) {
    ASSERT(dst->size == sizeof(tex_id_t));

    i32 index;
    const io_error_e err =
        i32_from_any(io, &IO_TYPES[IO_TYPE_I32], &RANGE(index), src);

    if (err != IO_OK) {
        return err;
    }

    // read index, go thorugh id -> name map
    tex_id_t id = { index };

    if (!map_contains(&io->tex_id_to_name, id)) {
        WARN("tex_id %d found not specified in mapping", id.index);
        // id.index = 0;
    } else {
        char **pname = map_get(char*, &io->tex_id_to_name, id);
        ASSERT(pname);
        id = tex_atlas_lookup(*pname);
    }

    *((tex_id_t*) dst->ptr) = id;
    return IO_OK;
}

#define MKBASIC(INDEX, T)            \
    ((io_type_t) {                   \
        .type = INDEX,               \
        .to_any_fn = T##_to_any,     \
        .from_any_fn = T##_from_any, \
    })

#define MKFIELD(T, f)                           \
    ((io_field_t) {                             \
        .offset = offsetof(T, f),               \
        .size = sizeof(typeof_field(T, f)),     \
        .type = IO_TYPE_OF(typeof_field(T, f)), \
        .name = #f,                             \
        .qualified_name = #T "." #f,            \
    })

#define MKGENERIC_NOPTR(T, LT, ...)      \
    [IO_TYPE_##T] = {                    \
        .type = IO_TYPE_##T,             \
        .to_any_fn = generic_to_any,     \
        .from_any_fn = generic_from_any, \
        .level_type = LT,                \
        .fields =                        \
            __VA_ARGS__                  \
    }

#define MKGENERIC(T, LT, ...)            \
    [IO_TYPE_PTR_##T] = {                \
        .type = IO_TYPE_PTR_##T,         \
        .to_any_fn = ptr_to_any,         \
        .from_any_fn = ptr_from_any,     \
        .level_type = LT,                \
    },                                   \
    MKGENERIC_NOPTR(T, LT, __VA_ARGS__)

static const io_type_t IO_TYPES[IO_TYPE_COUNT] = {
    [IO_TYPE_F32]  = MKBASIC(IO_TYPE_F32,  f32),
    [IO_TYPE_BOOL] = MKBASIC(IO_TYPE_BOOL, bool),
    [IO_TYPE_U8]   = MKBASIC(IO_TYPE_U8,   u8),
    [IO_TYPE_U16]  = MKBASIC(IO_TYPE_U16,  u16),
    [IO_TYPE_U32]  = MKBASIC(IO_TYPE_U32,  u32),
    [IO_TYPE_U64]  = MKBASIC(IO_TYPE_U64,  u64),
    [IO_TYPE_I8]   = MKBASIC(IO_TYPE_I8,   i8),
    [IO_TYPE_I16]  = MKBASIC(IO_TYPE_I16,  i16),
    [IO_TYPE_I32]  = MKBASIC(IO_TYPE_I32,  i32),
    [IO_TYPE_I64]  = MKBASIC(IO_TYPE_I64,  i64),
    [IO_TYPE_INT]  = MKBASIC(IO_TYPE_INT,  int),
    [IO_TYPE_UINT] = MKBASIC(IO_TYPE_UINT, uint),
    [IO_TYPE_V2I]  = MKBASIC(IO_TYPE_V2I,  v2i),
    [IO_TYPE_V2]   = MKBASIC(IO_TYPE_V2,   v2),
    [IO_TYPE_V3]   = MKBASIC(IO_TYPE_V3,   v3),
    [IO_TYPE_V4]   = MKBASIC(IO_TYPE_V4,   v4),
    MKGENERIC_NOPTR(SIDEMAT_DATA, -1, {
        MKFIELD(sidemat_data_t, tex_low),
        MKFIELD(sidemat_data_t, tex_mid),
        MKFIELD(sidemat_data_t, tex_high),
        MKFIELD(sidemat_data_t, tex_overlay),
        MKFIELD(sidemat_data_t, overlay_alpha),
        MKFIELD(sidemat_data_t, split_bottom),
        MKFIELD(sidemat_data_t, split_top),
        MKFIELD(sidemat_data_t, offsets),
        MKFIELD(sidemat_data_t, hsva),
        MKFIELD(sidemat_data_t, flags),
    }),
    MKGENERIC_NOPTR(SECTMAT_DATA, -1, {
        MKFIELD(sectmat_data_t, texs[0]),
        MKFIELD(sectmat_data_t, texs[1]),
        MKFIELD(sectmat_data_t, overlays[0]),
        MKFIELD(sectmat_data_t, overlays[1]),
        MKFIELD(sectmat_data_t, overlay_alphas[0]),
        MKFIELD(sectmat_data_t, overlay_alphas[1]),
        MKFIELD(sectmat_data_t, offsets[0]),
        MKFIELD(sectmat_data_t, offsets[1]),
        MKFIELD(sectmat_data_t, hsva[0]),
        MKFIELD(sectmat_data_t, hsva[1]),
        MKFIELD(sectmat_data_t, flags),
    }),
    MKGENERIC_NOPTR(LIGHT_PARAMS, -1, {
        MKFIELD(light_params_t, color),
        MKFIELD(light_params_t, power),
        MKFIELD(light_params_t, attenuation),
        MKFIELD(light_params_t, z_attenuation),
        MKFIELD(light_params_t, c1),
        MKFIELD(light_params_t, c2),
        MKFIELD(light_params_t, ambient),
        MKFIELD(light_params_t, flags),
    }),
    MKGENERIC(VERTEX, LT_VERTEX, {
        MKFIELD(vertex_t, pos)
    }),
    MKGENERIC(WALL, LT_WALL, {
        MKFIELD(wall_t, vertices[0]),
        MKFIELD(wall_t, vertices[1]),
        MKFIELD(wall_t, sides[0]),
        MKFIELD(wall_t, sides[1]),
    }),
    MKGENERIC(SIDE, LT_SIDE, {
        MKFIELD(side_t, sector),
        MKFIELD(side_t, portal),
        MKFIELD(side_t, mat),
        MKFIELD(side_t, like),
        MKFIELD(side_t, flags),
        MKFIELD(side_t, light),
    }),
    MKGENERIC(SECTOR, LT_SECTOR, {
        MKFIELD(sector_t, floor.z),
        MKFIELD(sector_t, floor.slope),
        MKFIELD(sector_t, ceil.z),
        MKFIELD(sector_t, ceil.slope),
        MKFIELD(sector_t, floor.slope_side),
        MKFIELD(sector_t, ceil.slope_side),
        MKFIELD(sector_t, flags),
        MKFIELD(sector_t, mat),
        MKFIELD(sector_t, like),
        MKFIELD(sector_t, floor.light),
        MKFIELD(sector_t, ceil.light),
        MKFIELD(sector_t, type),
        MKFIELD(sector_t, liquid_offset),
        MKFIELD(sector_t, liquid_hsv),
        MKFIELD(sector_t, teleport_exit_level),
        MKFIELD(sector_t, approach_name),
    }),
    MKGENERIC(DECAL, LT_DECAL, {
        MKFIELD(decal_t, type),
        MKFIELD(decal_t, is_on_side),
        MKFIELD(decal_t, side.ptr),
        MKFIELD(decal_t, side.offsets),
        MKFIELD(decal_t, sector.ptr),
        MKFIELD(decal_t, sector.pos),
        MKFIELD(decal_t, sector.plane),
        MKFIELD(decal_t, tex),
        MKFIELD(decal_t, tex_offsets),
        MKFIELD(decal_t, ticks),
    }),
    MKGENERIC(ENTITY, LT_ENTITY, {
        MKFIELD(entity_t, itype),
        MKFIELD(entity_t, pos),
        MKFIELD(entity_t, z),
        MKFIELD(entity_t, vel_xyz),
        MKFIELD(entity_t, bookmark_index),
        MKFIELD(entity_t, attach.side),
        MKFIELD(entity_t, attach.sector),
        MKFIELD(entity_t, attach.plane),
        MKFIELD(entity_t, attach.offset),
        MKFIELD(entity_t, light),
        MKFIELD(entity_t, spawn_point_angle),
    }),
    MKGENERIC(ROOM, LT_ROOM, {
        MKFIELD(room_t, bounds.min),
        MKFIELD(room_t, bounds.max),
        MKFIELD(room_t, is_entry),
    }),
    [IO_TYPE_LPTR_NOGEN] = {
        .type = IO_TYPE_LPTR_NOGEN,
        .to_any_fn = lptr_nogen_to_any,
        .from_any_fn = lptr_nogen_from_any,
    },
    [IO_TYPE_TEX_ID] = {
        .type = IO_TYPE_TEX_ID,
        .to_any_fn = tex_id_to_any,
        .from_any_fn = tex_id_from_any,
    },
    [IO_TYPE_STR]  = {
        .type = IO_TYPE_STR,
        .to_any_fn = str_to_any,
        .from_any_fn = str_from_any,
    },
};

// write "src" with specified name "name" and type "type" to "dst"
static io_error_e try_save_simple(
        io_t *io,
        kvstore_t *dst,
        io_type_e type,
        const char *name,
        const range_t *src) {
    any_t a = any_create(&io->arena);
    const io_error_e err =
        IO_TYPES[type].to_any_fn(
            io,
            &IO_TYPES[type],
            &a,
            src);

    if (err != IO_OK) {
        ERROR("error saving %s: %s", name, io_error_to_str(err));
    } else {
        kvstore_move_value(dst, name, &a);
    }

    return err;
}

// read from "src" with name "name" into "dst" with type "type"
static io_error_e try_load_simple(
        io_t *io,
        const kvstore_t *src,
        io_type_e type,
        const char *name,
        range_t *dst) {
    const any_t *a;
    if ((a = kvstore_get(src, name))) {
        const io_error_e err =
            IO_TYPES[type].from_any_fn(io, &IO_TYPES[type], dst, a);
        if (err != IO_OK) {
            ERROR("error reading %s: %s", name, io_error_to_str(err));
            return err;
        }
    } else {
        WARN("level has no %s", name);
        return IO_MISSING_FIELD;
    }

    return IO_OK;
}

io_error_e io_load_level(level_t *level, const range_t *data) {
    const u64 start = time_ns();

    io_error_e res = IO_OK;

    io_t io = { .level = level };
    bump_allocator_init(&io.arena, g_mallocator, 2 * 1024 * 1024, NULL);

    map_init(
        &io.tex_id_to_name,
        &io.arena,
        sizeof(tex_id_t),
        sizeof(char*),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    any_t any_base = any_create(&io.arena);
    if (!any_from_json(
            &any_base,
            NULL,
            &(str_view_t) {
                .start = data->ptr,
                .end = ((const char*) data->ptr) + data->size,
            })) {
        res = IO_BAD_PARSE;
        goto done;
    }

    if (!any_is(&any_base, ANY_KVSTORE)) {
        res = IO_NOT_DICT;
        goto done;
    }

    kvstore_t *kvs_base = any_get_kvstore(&any_base);
    ASSERT(kvs_base);

    // object -> entity migration
    if (kvstore_has(kvs_base, "OBJECT")) {
        kvstore_rename(kvs_base, "OBJECT", "ENTITY");
    }

    int version = -1;
    if (!kvstore_get_as_int(kvs_base, "version", &version)
        || version != IO_VERSION) {
        ERROR("bad version %d", version);
        res = IO_BAD_VERSION;
        goto done;
    }

    // load ltexts
    const any_t *ltexts;
    if ((ltexts = kvstore_get(kvs_base, "ltexts"))) {
        const DYNLIST(any_t) *list = any_get_list(ltexts);
        if (list) {
            dynlist_each(*list, it) {
                const kvstore_t *entry_kvs = any_get_kvstore(it.el);
                if (!entry_kvs) {
                    ERROR(
                        "ltexts entry %s, is not kvs",
                        any_to_str(it.el, tlscratch()));
                    continue;
                }

                char *name, *text;

                if (!kvstore_get_str(entry_kvs, "name", &io.arena, &name)) {
                    ERROR(
                        "ltexts entry %s: bad name",
                        any_to_str(it.el, tlscratch()));
                    continue;
                }

                if (!kvstore_get_str(entry_kvs, "text", &io.arena, &text)) {
                    ERROR(
                        "ltexts entry %s: bad text",
                        any_to_str(it.el, tlscratch()));
                    continue;
                }

                ltext_t *lt;
                *(lt = dynlist_push(level->ltexts)) =
                    (ltext_t) {
                        .name = mem_strdup(&level->arena, name),
                        .text = mem_strdup(&level->arena, text),
                    };

                // TODO: would love to get rid of this, but unless it's here
                // then things don't get mapped properly when textures are
                // loaded because the vtext names don't have assign tex_ids
                vtext_ensure_ltext(lt);
            }
        } else {
            ERROR(
                "ltexts is not list: %s",
                any_to_str(ltexts, tlscratch()));
        }
    } else {
        ERROR("no ltexts");
    }

    // load palette
    const any_t *palette;
    if ((palette = kvstore_get(kvs_base, "palette"))) {
        const DYNLIST(any_t) *list = any_get_list(palette);
        if (list) {
            int i;
            for (i = 0;
                 i < ARRLEN(level->palette)
                 && i < dynlist_size(*list);
                 i++) {
                any_t entry = any_create(&io.arena);

                if (!any_try_massage_type(
                        &entry,
                        &(*list)[i],
                        ANY_V3)) {
                    WARN(
                        "palette entry #%d ('%s') is not v3",
                        any_to_str(&(*list)[i], tlscratch()));
                    break;
                }

                ASSERT(any_get_v3(&entry, &level->palette[i]));
            }

            if (i != dynlist_size(*list)) {
                WARN(
                    "level palette size mismatch, expected %d but got %d",
                    LEVEL_PALETTE_SIZE,
                    dynlist_size(*list));
            }
        } else {
            WARN(
                "level palette is not list, %s",
                any_to_str(palette, tlscratch()));
        }
    } else {
        WARN("level has no palette");
    }

    // load matrices
    const any_t *matrices;
    if ((matrices = kvstore_get(kvs_base, "matrices"))) {
        const kvstore_t *matrices_kvs = any_get_kvstore(matrices);

        if (matrices_kvs) {
            const char *names[] = { "pvs", "evs", "reachable", "near" };
            STATIC_ASSERT(ARRLEN(names) == ARRLEN(level->matrices.arr));

            for (int i = 0; i < ARRLEN(names); i++) {
                const any_t *a = kvstore_get(matrices_kvs, names[i]);

                if (!a) {
                    WARN("sector matrix %s not present", names[i]);
                    continue;
                }

                const char *errmsg;
                if (!sector_matrix_from_any(
                        level, &level->matrices.arr[i], a, &errmsg)) {
                    WARN(
                        "error loading matrix %s from %s: %s",
                        names[i],
                        any_to_str(a, tlscratch()),
                        errmsg);
                }
            }
        } else {
            WARN(
                "matrices: %s is not kvs",
                any_to_str(matrices, tlscratch()));
        }
    } else {
        WARN("level has no sector matrices");
    }

    // load tex id -> name map
    const any_t *tex_id_to_name;
    if ((tex_id_to_name = kvstore_get(kvs_base, "tex_id_to_name"))) {
        const kvstore_t *tex_id_to_name_kvs = any_get_kvstore(tex_id_to_name);

        if (tex_id_to_name_kvs) {
            kvstore_each(tex_id_to_name_kvs, it) {
                tex_id_t id;
                if (sscanf(*it.key, "%" SCNu16, &id.index) != 1) {
                    ERROR("could not scan %s as u16", *it.key);
                    res = IO_BAD_TEX_ID_MAP;
                    goto done;
                }

                const char *name = any_get_str(it.value);
                if (!name) {
                    ERROR(
                        "%s is not string",
                        any_to_str(it.value, &io.arena));
                    res = IO_BAD_TEX_ID_MAP;
                    goto done;
                }

                map_insert(
                    &io.tex_id_to_name,
                    id,
                    mem_strdup(&io.arena, name));
            }
        } else {
            ERROR(
                "bad tex_id_to_name type %s",
                any_type_to_str(tex_id_to_name->type));
            res = IO_BAD_TEX_ID_MAP;
            goto done;
        }
    } else {
        WARN("no tex_id_to_name map");
    }

    // load name
    if (kvstore_has(kvs_base, "name")) {
        char *str;
        ASSERT(kvstore_get_str(kvs_base, "name", &io.arena, &str));
        level->name = mem_strdup(&level->arena, str);
    } else {
        ERROR("level has no name");
        level->name = mem_strdup(&level->arena, "");
    } 

    // flags
    try_load_simple(
        &io,
        kvs_base,
        IO_TYPE_OF_VALUE(level->flags),
        "flags",
        RANGE_REF(level->flags));

    // fog dist
    try_load_simple(
        &io,
        kvs_base,
        IO_TYPE_OF_VALUE(level->fog.dist),
        "fog_dist",
        RANGE_REF(level->fog.dist));

    // first pass: verify JSON types, create all level types, disable recalc on
    // all
    for (level_type_e lt = LT_VERTEX; lt <= LT_ROOM; lt++) {
        const any_t *any_list = kvstore_get(kvs_base, level_type_to_str(lt));
        if (!any_list) {
            WARN("level has no %s", level_type_to_str(lt));
            continue;
        }

        if (!any_is(any_list, ANY_LIST)) {
            ERROR("no list for %s", level_type_to_str(lt));
            res = IO_MALFORMATTED;
            goto done;
        }

        const DYNLIST(any_t) *list = any_get_list(any_list);

        dynlist_each(*list, it) {
            const any_t *any = it.el;

            if (!any_is(any, ANY_KVSTORE)) {
                ERROR(
                    "list %s contains non-kvstore: %s",
                    level_type_to_str(lt),
                    any_to_json(any, tlscratch()));
                res = IO_MALFORMATTED;
                goto done;
            }

            void *value;

            switch (lt) {
            case LT_VERTEX:
                value = vertex_new(level, v2_of(0));
                break;
            case LT_WALL:
                value = wall_new(level, NULL, NULL);
                break;
            case LT_SIDE:
                value = side_new(level, NULL);
                break;
            case LT_SECTOR:
                value = sector_new(level, NULL);
                break;
            case LT_DECAL:
                value = decal_new(level, NULL);
                break;
            case LT_ENTITY:
                value = entity_new(level, NULL);
                break;
            case LT_ROOM:
                value = room_new(level);
                break;
            }

            lptr_t lptr;
            ASSERT(lptr_from_raw(level, lt, value, &lptr));
            ASSERT(lptr.id == it.i);

            // disable recalc, enabled later
            lptr_level_fields(level, lptr)->lflags.do_not_recalc = true;
        }
    }

    // second pass: load from JSON
    for (level_type_e lt = LT_VERTEX; lt <= LT_ROOM; lt++) {
        const io_type_t *type = &IO_TYPES[level_type_to_io_type(lt)];

        const any_t *any_list = kvstore_get(kvs_base, level_type_to_str(lt));
        if (!any_list) {
            // already warned
            continue;
        }

        const DYNLIST(any_t) *list = any_get_list(any_list);

        dynlist_each(*list, it) {
            const any_t *any = it.el;

            if (!any_is(any, ANY_KVSTORE)) {
                ERROR("list %s contains non-kvstore", level_type_to_str(lt));
                res = IO_MALFORMATTED;
                goto done;
            }

            // iteration indices must match
            void *ptr =
                lptr_raw_ptr(
                    level,
                    lptr_from_nogen(
                        level,
                        (lptr_nogen_t) { .type = lt, .index = it.i }));
            ASSERT(ptr);

            res =
                type->from_any_fn(
                    &io,
                    type,
                    &(range_t) { ptr, level->lists[lt].data.t_size, },
                     it.el);

            if (res != IO_OK) {
                WARN("failed from_any %s/%d", level_type_to_str(lt), it.i);
                goto done;
            }
        }
    }

    // allow vertices, walls to recalc
    level_each(vertex_t, &level->vertices, it) {
        it.el->lflags.do_not_recalc = false;
    }

    level_each(wall_t, &level->walls, it) {
        it.el->lflags.do_not_recalc = false;
    }

    // load wall vertices, sides
    level_each(wall_t, &level->walls, it) {
        wall_t *wall = it.el;

        for (uint i = 0; i < 2; i++) {
            vertex_t *v = wall->vertices[i];
            wall->vertices[i] = NULL;
            wall_set_vertex(level, wall, i, v);
        }

        for (uint i = 0; i < 2; i++) {
            side_t *s = wall->sides[i];
            wall->sides[i] = NULL;
            wall_set_side(level, wall, i, s);
        }
    }

    // load sector sides
    level_each(side_t, &level->sides, it) {
        if (!it.el->sector) {
            WARN("loading with sect-less side %d", it.i);
            continue;
        }

        sector_t *s = it.el->sector;
        it.el->sector = NULL;
        sector_add_side(level, s, it.el);
    }

    // allow sides to be updated
    level_each(side_t, &level->sides, it) {
        it.el->lflags.do_not_recalc = false;
        side_recalculate(level, it.el);
    }

    // recalculate sectors
    level_each(sector_t, &level->sectors, it) {
        it.el->lflags.do_not_recalc = false;
        sector_recalculate(level, it.el);
    }

    // load decal types / sides and sectors
    level_each(decal_t, &level->decals, it) {
        decal_t *decal = it.el;

        // set type
        const decal_type_e type = decal->type;
        decal->type = DECAL_TYPE_PLACEHOLDER;
        decal_set_type(level, decal, type);

        // attach to side/sector
        decal->lflags.do_not_recalc = false;

        if (decal->is_on_side) {
            side_t *side = decal->side.ptr;
            const v2 offsets = decal->side.offsets;

            decal->side.ptr = NULL;
            decal_set_side(level, decal, side);
            decal->side.offsets = offsets;
        } else {
            sector_t *sector = decal->sector.ptr;
            const v2 pos = decal->sector.pos;

            decal->sector.ptr = NULL;
            decal_set_sector(level, decal, sector, decal->sector.plane);
            decal->sector.pos = pos;
        }

        decal_recalculate(level, decal);
    }

    blocks_reset(level);
    sector_matrices_recompute(level);

    // load entity types, sectors
    level_each(entity_t, &level->entities, it) {
        entity_t *ent = it.el;
        const entity_type_e type = ent->itype;
        ent->itype = ENTITY_TYPE_PLACEHOLDER;
        entity_set_type(level, ent, type);

        ASSERT(!ent->sector);
        const v2 pos = ent->pos;
        ent->pos = v2_of(0);
        entity_try_move(level, ent, pos);
    }

done:
    bump_allocator_destroy(&io.arena);
    LOG("loaded level in %.3f ms", (time_ns() - start) / 1000000.0);
    return res;
}

io_error_e io_save_level(const level_t *level, DYNLIST(u8) *dst) {
    const nstime_t start_ns = time_ns();

    io_error_e res = IO_OK;

    io_t io = { .level = (level_t*) level };
    bump_allocator_init(&io.arena, g_mallocator, 2 * 1024 * 1024, NULL);

    map_init(
        &io.tex_id_to_name,
        &io.arena,
        sizeof(tex_id_t),
        sizeof(char*),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    any_t any = any_create(&io.arena);
    kvstore_t *kvs = any_set_kvstore(&any, NULL);

    kvstore_set_int(kvs, "version", IO_VERSION);
    kvstore_set_u64(kvs, "timestamp", time_epoch_ns()); 

    // save ltexts
    {
        any_t ltexts_any = any_create(&io.arena);
        DYNLIST(any_t) *ltexts_list = any_set_list(&ltexts_any, NULL);

        dynlist_each(level->ltexts, it) {
            any_t entry_any = any_create(&io.arena);
            kvstore_t *entry_kvs = any_set_kvstore(&entry_any, NULL);

            kvstore_set_str(entry_kvs, "name", it.el->name);
            kvstore_set_str(entry_kvs, "text", it.el->text);

            *dynlist_push(*ltexts_list) = entry_any;
        }

        kvstore_move_value(kvs, "ltexts", &ltexts_any);
    }

    // save palette
    {
        any_t pal_any = any_create(&io.arena);
        DYNLIST(any_t) *pal_list = any_set_list(&pal_any, NULL);

        for (int i = 0; i < ARRLEN(level->palette); i++) {
            *dynlist_push(*pal_list) =
                any_wrap_v3(&io.arena, level->palette[i]);
        }

        kvstore_move_value(kvs, "palette", &pal_any);
    }

    // save matrices
    {
        any_t matrices_any = any_create(&io.arena);
        kvstore_t *matrices_kvs = any_set_kvstore(&matrices_any, NULL);

        const char *names[] = { "pvs", "evs", "reachable", "near" };
        STATIC_ASSERT(ARRLEN(names) == ARRLEN(level->matrices.arr));

        for (int i = 0; i < ARRLEN(names); i++) {
            any_t matrix_any = any_create(&io.arena);
            sector_matrix_to_any(
                level,
                &matrix_any,
                &level->matrices.arr[i]);
            kvstore_move_value(matrices_kvs, names[i], &matrix_any);
        }

        kvstore_move_value(kvs, "matrices", &matrices_any);
        ASSERT(kvstore_has(kvs, "matrices"));
        ASSERT(any_is(kvstore_get(kvs, "matrices"), ANY_KVSTORE));
    }

    // level name
    kvstore_set_str(kvs, "name", level->name);

    // flags
    try_save_simple(
        &io,
        kvs,
        IO_TYPE_OF_VALUE(level->flags),
        "flags",
        RANGE_REF(level->flags));

    // fog dist
    try_save_simple(
        &io,
        kvs,
        IO_TYPE_OF_VALUE(level->fog.dist),
        "fog_dist",
        RANGE_REF(level->fog.dist));

    // first pass: create index maps to map current list index (which might have
    // holes!) to a contiguous iteration index. this means that everything is
    // contiguous on level load :)
    for (uint i = LT_VERTEX; i <= LT_ROOM; i++) {
        map_init(
            &io.level_index_maps[i],
            &io.arena,
            sizeof(i32),
            sizeof(i32),
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            NULL,
            NULL);

        uint iter_index = 0;
        genlist_each_voidp(&level->lists[i], it) {
            lptr_t lptr;
            ASSERT(lptr_from_raw(level, i, it.el, &lptr));

            const u32 list_index = lptr_level_fields(level, lptr)->id;

            // map list index -> iter index
            map_insert(&io.level_index_maps[i], list_index, iter_index);
            iter_index++;
        }
    }

    for (uint i = LT_VERTEX; i <= LT_ROOM; i++) {
        const io_type_t *type = &IO_TYPES[level_type_to_io_type(i)];

        any_t any = any_create(&io.arena);
        DYNLIST(any_t) *list = any_set_list(&any, NULL);

        genlist_each_voidp(&level->lists[i], it) {
            any_t *el = dynlist_push(*list);
            any_init(el, &io.arena);

            res =
                type->to_any_fn(
                    &io,
                    type,
                    el,
                    &(range_t) { it.el, level->lists[i].data.t_size });

            if (res != IO_OK) {
                WARN("failed to_any %s/%d", level_type_to_str(i), it.i);
                goto done;
            }
        }

        kvstore_move_value(kvs, level_type_to_str(i), &any);
    }

    // save tex id -> name map
    {
        any_t tex_id_to_name = any_create(&io.arena);
        kvstore_t *tex_id_to_name_kvs = any_set_kvstore(&tex_id_to_name, NULL);

        map_each(tex_id_t, char*, &io.tex_id_to_name, it) {
            kvstore_set_str(
                tex_id_to_name_kvs,
                mem_strfmt(&io.arena, "%d", (int) it.key->index),
                *it.value);
        }

        kvstore_set(kvs, "tex_id_to_name", &tex_id_to_name);
    }

    // copy out - DO NOT copy null terminator!
    const nstime_t json_start_ns = time_ns();
    const char *json = any_to_json(&any, &io.arena);
    const usize len = strlen(json);
    dynlist_resize(*dst, len);
    memcpy(&(*dst)[0], json, len);
    const nstime_t json_time_ns = time_ns() - json_start_ns;

    LOG(
        "saved %.3f KiB JSON (%.3f KiB in-memory / %.3f KiB allocated)",
        dynlist_size(*dst) / 1024.0f,
        any_footprint(&any) / 1024.0f,
        io.arena.bump.allocated  / 1024.0f);

    LOG("  took %.3f ms (of which %.3f ms was JSON)",
        ns_to_ms(time_ns() - start_ns),
        ns_to_ms(json_time_ns));
done:
    bump_allocator_destroy(&io.arena);
    return res;
}
