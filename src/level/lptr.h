#pragma once

#include "defs.h"
#include "util/genlist.h"

// "tagged pointer" (really a typed index) for anything in level arrays
// only interact by from LPTR_* functions
typedef union lptr {
    struct {
        // handle of level object
        union {
            genlist_handle_t handle;

            // MUST MATCH genlist_handle_t
            struct {
                int id: GENLIST_HANDLE_BITS;
                genlist_gen_t gen: GENLIST_GEN_BITS;
            };
        };

        // type
        level_type_e type;
    };

    u64 raw;
} lptr_t;

// lptr without generation
typedef union lptr_nogen {
    struct {
        i32 index: 16;
        level_type_e type: 16;
    };

    u32 raw;
} lptr_nogen_t;

#define LPTR_NULL ((lptr_t) { 0 })

// true if lptr is null
M_INLINE bool lptr_is_null(lptr_t lp) {
    return lp.raw == 0;
}

// true if lptr is any of LTF_* (in set of level type flags)
M_INLINE bool lptr_matches(lptr_t lp, u32 flags) {
    return !!((1 << lp.type) & flags);
}

// true if lptr is LT_*
M_INLINE bool lptr_is(lptr_t lp, level_type_e type) {
    return lp.type == type;
}

// get underlying pointer, retuns NULL is lptr is invalid/doesn't match current
// generation
#define lptr_get(_T, _lt, _l, _lp) ({        \
        const lptr_t __lp = (_lp);           \
        lptr_is(__lp, (_lt)) ?               \
            ((_T*) lptr_raw_ptr((_l), __lp)) \
            : ((_T*) (NULL));                \
    })

#define lptr_vertex(_l, _lp) lptr_get(vertex_t, LT_VERTEX, _l, _lp)
#define lptr_wall(_l,   _lp) lptr_get(wall_t,   LT_WALL,   _l, _lp)
#define lptr_side(_l,   _lp) lptr_get(side_t,   LT_SIDE,   _l, _lp)
#define lptr_sector(_l, _lp) lptr_get(sector_t, LT_SECTOR, _l, _lp)
#define lptr_decal(_l,  _lp) lptr_get(decal_t,  LT_DECAL,  _l, _lp)
#define lptr_entity(_l, _lp) lptr_get(entity_t, LT_ENTITY, _l, _lp)
#define lptr_room(_l,   _lp) lptr_get(room_t,   LT_ROOM,   _l, _lp)

// impl for lptr_from, internal use only
lptr_t _lptr_from_impl(const void *ptr, level_type_e type);

// convert pointer to level type p_ to lptr
#define lptr_from(p_) (_lptr_from_impl((p_), LEVEL_TYPE_TO_LT(typeof(*(p_)))))

// true if lptrs are equal
M_INLINE bool lptr_eq(lptr_t a, lptr_t b) {
    return a.type == b.type && genlist_handle_eq(a.handle, b.handle);
}

// lptr type -> flag
M_INLINE u32 lptr_type_flag(lptr_t lp) {
    return 1U << lp.type;
}

// get lptr type
M_INLINE level_type_e lptr_type(lptr_t lp) {
    return lp.type;
}

// convert lptr -> "TYPE (INDEX)" string
char *lptr_to_str(const level_t *level, lptr_t ptr, allocator_t *al);

// convert lptr -> "TYPE (INDEX) (extra info...)" string
char *lptr_to_fancy_str(const level_t *level, lptr_t ptr, allocator_t *al);

// convert lptr to its respective level list index
i32 lptr_to_index(const level_t *level, lptr_t ptr);

// returns true if lptr is to a valid index
bool lptr_is_valid(const level_t *level, lptr_t ptr);

// generate a random (but ID-consistent) abgr color based on an lptr
u32 lptr_rand_abgr(lptr_t ptr);

// delete underlying object for lptr
void lptr_delete(level_t *level, lptr_t ptr);

// run *_recalculate function for underlying ptr
void lptr_recalculate(level_t *level, lptr_t ptr);

// get raw underlying pointer from lptr
void *lptr_raw_ptr(const level_t *level, lptr_t ptr);

// convert typed raw ptr -> lptr
// returns true on success
bool lptr_from_raw(
    const level_t *level,
    level_type_e type,
    const void *ptr,
    lptr_t *out);

// convert no-gen lptr -> lptr
lptr_t lptr_from_nogen(const level_t *level, lptr_nogen_t ptr);

// convert lptr -> lptr without generation
lptr_nogen_t lptr_to_nogen(lptr_t ptr);

// gets level_fields on pointer, NULL if pointer is invalid
level_fields_t *lptr_level_fields(const level_t *level, lptr_t ptr);
