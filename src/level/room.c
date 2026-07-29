#include "level/room.h"
#include "level/block.h"
#include "level/decal.h"
#include "level/level.h"
#include "level/level_types.h"
#include "level/entity.h"

room_t *room_new(level_t *level) {
    return level_try_alloc(level, &level->rooms);
}

void room_delete(level_t *level, room_t *room) {
    level_each(entity_t, &level->entities, it) {
        if (it.el->room == room) {
            it.el->room = NULL;
        }
    }

    level_free(level, &level->rooms, room);
}

box2f_t room_min_box(room_t *room) {
    return
        box2f_ch(
            v2_add(v2_from_i(room->bounds.min), v2_of(0.5f)),
            v2_of(0.25f));
}

box2f_t room_max_box(room_t *room) {
    return
        box2f_ch(
            v2_add(v2_from_i(room->bounds.max), v2_of(0.5f)),
            v2_of(0.25f));
}

bool room_contains_lptr(const level_t *level, const room_t *room, lptr_t ptr) {
    if (!lptr_is_valid(level, ptr)) { return false; }

    const box2f_t box = box2f_from(room->bounds);

    switch (lptr_type(ptr)) {
    case LT_VERTEX:
        return box2f_contains(box, lptr_vertex(level, ptr)->pos);
    case LT_WALL:;
        const wall_t *wall = lptr_wall(level, ptr);
        return box2f_vs_line(box, wall->v0->pos, wall->v1->pos);
    case LT_SIDE:
        return
            room_contains_lptr(
                level, room, lptr_from(lptr_side(level, ptr)->wall));
    case LT_ENTITY:;
        const entity_t *ent = lptr_entity(level, ptr);
        const entity_bounds_t bounds = entity_bounds(level, ent);
        return box2f_vs_circle(box, ent->pos, bounds.radius);
    case LT_DECAL:;
        const decal_t *decal = lptr_decal(level, ptr);
        if (decal->is_on_side){
            return
                box2f_contains(
                    box, v2_from(decal_worldpos(lptr_decal(level, ptr))));
        } else {
            return
                box2f_collides(
                    box, decal_bounds(level, lptr_decal(level, ptr)));
        }
    default: return false;
    }
}

void room_get_lptrs(
    const level_t *level, const room_t *room, int tags, DYNLIST(lptr_t) *ptrs) {
    level_ptrs_in_area(
        level, box2f_from(room->bounds), tags, ptrs, LPIA_NONE);
}

void room_get_entities(
        const level_t *level,
        const room_t *room,
        DYNLIST(entity_t*) *entities) {
    LLIST(entity_t) list;
    llist_init(&list);
    int n = 0;

    level_for_blocks_in_area(
            level,
            level_pos_to_block(v2_from_i(room->bounds.min)),
            level_pos_to_block(v2_from_i(room->bounds.max)),
            it) {
        dynlist_each(it.el->entities, it) {
            entity_t *e = *it.el;

            if (e != list.head && !e->node.next) {
                llist_prepend(node, &list, e);
                n++;
            }
        }
    }

    dynlist_reserve(*entities, n);
    while (!llist_empty(&list)) {
        *dynlist_push(*entities) = llist_pop_front(node, &list);
    }
}

room_t *level_find_room(const level_t *level, v2 pos) {
    // TODO: maybe use blocks if this begins to be too many checks
    level_each(room_t, &level->rooms, it) {
        if (box2f_contains(box2f_from(it.el->bounds), pos)) {
            return it.el;
        }
    }

    return NULL;
}

sector_t *room_find_sector_with_type(
        const level_t *level,
        const room_t *room,
        sector_type_e type) {
    level_for_blocks_in_area(
            level,
            level_pos_to_block(v2_from_i(room->bounds.min)),
            level_pos_to_block(v2_from_i(room->bounds.max)),
            it) {
        dynlist_each(it.el->sectors, it) {
            if ((*it.el)->type == type) {
                return *it.el;
            }
        }
    }
    return NULL;
}
