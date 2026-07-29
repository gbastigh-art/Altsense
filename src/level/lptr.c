#include "level/lptr.h"
#include "level/decal.h"
#include "level/level_types.h"
#include "level/entity.h"
#include "level/room.h"
#include "level/sector.h"
#include "level/side.h"
#include "level/vertex.h"
#include "level/wall.h"
#include "util/str.h"

static level_fields_t *get_fields_ptr(level_type_e type, void *p) {
    if (!p) { return NULL; }

    static const int OFFSETS[LT_COUNT] = {
        [LT_VERTEX]  = offsetof(vertex_t,  level_fields),
        [LT_WALL]    = offsetof(wall_t,    level_fields),
        [LT_SIDE]    = offsetof(side_t,    level_fields),
        [LT_SECTOR]  = offsetof(sector_t,  level_fields),
        [LT_DECAL]   = offsetof(decal_t,   level_fields),
        [LT_ENTITY]  = offsetof(entity_t,  level_fields),
        [LT_ROOM]    = offsetof(room_t,    level_fields),
    };

    return p + OFFSETS[type];
}

lptr_t _lptr_from_impl(const void *ptr, level_type_e type) {
    if (M_UNLIKELY(!ptr)) { return LPTR_NULL; }

    const level_fields_t *fields = get_fields_ptr(type, (void*) ptr);
    return (lptr_t) {
        .handle = fields->handle,
        .type = type,
    };
}

char *lptr_to_str(const level_t *level, lptr_t ptr, allocator_t *al) {
    if (lptr_is_null(ptr)) {
        return mem_strdup(al, "(NULL)");
    } else if (!lptr_is_valid(level, ptr)) {
        return mem_strfmt(al, "(INVALID/%s)", level_type_to_str(ptr.type));
    } else {
        return
            mem_strfmt(
                al,
                "%s (%d)",
                level_type_to_str(lptr_type(ptr)),
                lptr_is_valid(level, ptr) ?
                    (int) lptr_to_index(level, ptr)
                    : -1);
    }
}

char *lptr_to_fancy_str(const level_t *level, lptr_t ptr, allocator_t *al) {
    strbuf_t buf = strbuf_create(tlscratch());
    strbuf_ap_fmt(&buf, "%s", lptr_to_str(level, ptr, tlscratch()));

    if (lptr_is_valid(level, ptr)) {
        switch (lptr_type(ptr)) {
        case LT_SECTOR: {
            sector_t *s = lptr_sector(level, ptr);
            const v2 span = v2_sub(s->max, s->min);
            strbuf_ap_fmt(&buf, " (%.2f x %.2f)", span.x, span.y);
        } break;
        case LT_ENTITY: {
            entity_t *ent = lptr_entity(level, ptr);
            strbuf_ap_fmt(&buf, " (%s)", entity_type_to_str(ent->itype));

            if (ent->itype == ENTITY_TYPE_BOOKMARK) {
                strbuf_ap_fmt(&buf, " #%d", ent->bookmark_index);
            }
        } break;
        case LT_WALL: {
            wall_t *w = lptr_wall(level, ptr);
            strbuf_ap_fmt(&buf, " (%.2f)", w->len);
        } break;
        default:
        }
    }

    return strbuf_dump(&buf, al);
}

i32 lptr_to_index(const level_t *level, lptr_t ptr) {
    return ptr.id;
}

bool lptr_is_valid(const level_t *level, lptr_t ptr) {
    return
        level_type_is_valid(ptr.type)
        && genlist_present(&level->lists[ptr.type], ptr.handle);
}

u32 lptr_rand_abgr(lptr_t ptr) {
    // NOTE: must match with algorithm in level.glsl
    const u32 seed = ptr.id;
    v3 v = v3_of((seed * 37) & 0xFF, (seed * 17) & 0xFF, (seed * 67) & 0xFF);
    v = v3_divs(v, 255.0);
    v = v3_normalize(v);
    return 0xFF000000
        | (((u32) (v.b * 255)) << 16)
        | (((u32) (v.g * 255)) <<  8)
        | (((u32) (v.r * 255)) <<  0);
}

void lptr_delete(level_t *level, lptr_t ptr) {
    if (!lptr_is_valid(level, ptr)) {
        WARN(
            "attempt to delete invalid lptr %s",
            lptr_to_str(level, ptr, tlscratch()));
        return;
    }

    switch (lptr_type(ptr)) {
    case LT_VERTEX: vertex_delete(level,            lptr_vertex(level, ptr)); break;
    case LT_WALL:   wall_delete(level,              lptr_wall(level,   ptr)); break;
    case LT_SIDE:   side_delete(level,              lptr_side(level,   ptr)); break;
    case LT_SECTOR: sector_delete_with_sides(level, lptr_sector(level, ptr)); break;
    case LT_DECAL:  decal_delete(level,             lptr_decal(level,  ptr)); break;
    case LT_ENTITY: entity_delete(level,            lptr_entity(level, ptr)); break;
    case LT_ROOM:   room_delete(level,              lptr_room(level,   ptr)); break;
    default: ASSERT(false);
    }
}

void lptr_recalculate(level_t *level, lptr_t ptr) {
    if (!lptr_is_valid(level, ptr)) {
        WARN(
            "attempt to recalc invalid lptr %s",
            lptr_to_str(level, ptr, tlscratch()));
        return;
    }

    switch (lptr_type(ptr)) {
    case LT_VERTEX: vertex_recalculate(level, lptr_vertex(level, ptr)); break;
    case LT_WALL:   wall_recalculate(level,   lptr_wall(level,   ptr)); break;
    case LT_SIDE:   side_recalculate(level,   lptr_side(level,   ptr)); break;
    case LT_SECTOR: sector_recalculate(level, lptr_sector(level, ptr)); break;
    case LT_DECAL:  decal_recalculate(level,  lptr_decal(level,  ptr)); break;
    case LT_ENTITY:
    case LT_ROOM:
        level->version++;
        lptr_level_fields(level, ptr)->version++;
        break;
    default: ASSERT(false);
    }
}

void *lptr_raw_ptr(const level_t *level, lptr_t ptr) {
    return genlist_try_ptr_voidp(&level->lists[ptr.type], ptr.handle);
}

bool lptr_from_raw(
    const level_t *level,
    level_type_e type,
    const void *ptr,
    lptr_t *out) {
    const genlist_handle_t handle =
        genlist_handle_of_ptr(&level->lists[type], ptr);

    if (genlist_handle_is_null(handle)) {
        ERROR("bad ptr/type %p/%d", ptr, type);
        return false;
    }

    *out = (lptr_t) { .type = type, .handle = handle };
    return true;
}

lptr_t lptr_from_nogen(const level_t *level, lptr_nogen_t ptr) {
    if (!level_type_is_valid(ptr.type)) {
        // WARN("pointer %d/%d with invalid type", ptr.type, ptr.index);
        return LPTR_NULL;
    }

    const genlist_t *list = &level->lists[ptr.type];
    const genlist_handle_t idx = genlist_handle_of_index(list, ptr.index);

    return
        genlist_handle_is_null(idx) ?
            LPTR_NULL
            : (lptr_t) { .handle = idx, .type = ptr.type };
}

lptr_nogen_t lptr_to_nogen(lptr_t ptr) {
    ASSERT(ptr.id < 16777216); // 2^24
    return (lptr_nogen_t) { .type = ptr.type, .index = ptr.id };
}

level_fields_t *lptr_level_fields(const level_t *level, lptr_t ptr) {
    return get_fields_ptr(ptr.type, lptr_raw_ptr(level, ptr));
}
