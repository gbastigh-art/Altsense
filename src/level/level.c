#include "level/level.h"
#include "level/decal.h"
#include "level/lptr.h"
#include "level/entity.h"
#include "level/portal.h"
#include "level/sector.h"
#include "level/side.h"
#include "level/vertex.h"
#include "level/wall.h"
#include "level/block.h"
#include "level/room.h"
#include "game.h"
#include "util/macros.h"
#include "util/math.h"
#include "util/bitmap.h"

char *level_types_to_str(int types, allocator_t *al) {
    strbuf_t buf = strbuf_create(&g->frame_arena);
    bool first = true;

    for (level_type_e lt = LT_FIRST; lt <= LT_LAST; lt++) {
        if (!(types & (1 << lt))) { continue; }

        strbuf_ap_fmt(
            &buf,
            "%s%s",
            first ? "" : ", ",
            level_type_to_str(lt));

        first = false;
    }

    return strbuf_dump(&buf, al);
}

bool level_can_alloc(level_t *level, genlist_t *list) {
    return genlist_size(list) != list->max_size;
}

static void level_alloc_init(
        level_t *level,
        genlist_t *list,
        genlist_handle_t handle,
        void *p) {
    // zero
    memset(p, 0, list->data.t_size);

    // assign handle into fields
    const int list_index = ARR_PTR_INDEX(level->lists, list);
    const lptr_t ptr = { .handle = handle, .type = list_index };
    level_fields_t *fields = lptr_level_fields(level, ptr);
    fields->handle = handle;
}

void *level_try_alloc(level_t *level, genlist_t *list) {
    genlist_handle_t handle;
    void *p = genlist_add_voidp(list, &handle);

    if (!p) {
        return NULL;
    }

    level_alloc_init(level, list, handle, p);
    return p;
}

void *level_try_alloc_at(level_t *level, genlist_t *list, int index) {
    genlist_handle_t handle;
    void *p = genlist_try_add_at_voidp(list, index, &handle);

    if (!p) {
        return NULL;
    }

    level_alloc_init(level, list, handle, p);
    return p;
}

void level_free(level_t *level, genlist_t *list, void *ptr) {
    genlist_remove(list, genlist_handle_of_ptr(list, ptr));
    level->version++;
}

void level_init(level_t *level, allocator_t *allocator) {
    *level = (level_t) { 0 };

    // level-lifetime heap allocator
    g->stats.level_arena = (allocator_stats_t) { 0 };
    heap_allocator_init(&level->arena, allocator, &g->stats.level_arena);

    level->name = mem_strdup(&level->arena, "");
    dynlist_init(level->ltexts, &level->arena);

    dynlist_init(level->dirty_sect_sides, &level->arena);
    dynlist_init(level->delete_ptrs, &level->arena);
    dynlist_init(level->recalc_ptrs, &level->arena);

    // version 0 indicates no version, so start with some (arbitrary) value
    level->version = 1;

    // allocate all level lists
    for (int i = 0; i < LT_COUNT; i++) {
        static struct { int size, max_count; } type_info[LT_COUNT] = {
            [LT_VERTEX] = { sizeof(vertex_t), MAX_VERTICES, },
            [LT_WALL]   = { sizeof(wall_t),   MAX_WALLS,    },
            [LT_SIDE]   = { sizeof(side_t),   MAX_SIDES,    },
            [LT_SECTOR] = { sizeof(sector_t), MAX_SECTORS,  },
            [LT_DECAL]  = { sizeof(decal_t),  MAX_DECALS,   },
            [LT_ENTITY] = { sizeof(entity_t), MAX_ENTITIES, },
            [LT_ROOM]   = { sizeof(room_t),   MAX_ROOMS,    },
        };

        genlist_t *list = &level->lists[i];
        genlist_init(
            list,
            &(genlist_desc_t) {
                .allocator = &level->arena,
                .block_size = 64,
                .t_size = type_info[i].size,
                .max_size = type_info[i].max_count,
            });
    }

    blklist_init(
        &level->subsectors,
        &(blklist_desc_t) {
            .allocator = &level->arena,
            .block_size = 256,
            .t_size = sizeof(subsector_t),
            .max_size = MAX_SUBSECTORS,
        });
}

void level_destroy(level_t *level) {
    // deallocate all used memory, implicitly frees all level structures
    heap_allocator_destroy(&level->arena);
}

void level_set_name(level_t *level, const char *name) {
    if (!strcmp(name, level->name)) {
        return;
    }

    mem_free(&level->arena, (void*) level->name);
    level->name = mem_strdup(&level->arena, name);
}

void level_frame_update(level_t *level) {
    // update sectors for dirty sides (TODO: deduplicate)
    while (dynlist_size(level->dirty_sect_sides) != 0) {
        lptr_t ptr = dynlist_pop(level->dirty_sect_sides);
        if (!lptr_is_valid(level, ptr)) { continue; }

        ASSERT_DEBUG(lptr_type(ptr) == LT_SIDE);
        side_t *s = lptr_side(level, ptr);

        ASSERT_DEBUG(s->lflags.dirty_side);
        s->lflags.dirty_side = false;

        level_update_side_sector(level, s);
    }
    dynlist_resize(level->dirty_sect_sides, 0);

    // delete accumulated delete ptrs
    dynlist_each(level->delete_ptrs, it) {
        if (lptr_is_valid(level, *it.el)) {
            ASSERT_DEBUG(lptr_level_fields(level, *it.el)->lflags.deleted);
            lptr_delete(level, *it.el);
        }
    }

    dynlist_resize(level->delete_ptrs, 0);

    // recalc accumulated recalc ptrs
    dynlist_each(level->recalc_ptrs, it) {
        level_fields_t *fields = lptr_level_fields(level, *it.el);

        if (fields && fields->lflags.recalc_enqueued) {
            lptr_recalculate(level, *it.el);
            fields->lflags.recalc_enqueued = false;
        }
    }

    dynlist_resize(level->recalc_ptrs, 0);

    // update dirty blocks
    blocks_update(level);

    // update sector matrices
    sector_matrices_update(level);

    if (g->mode != GAMEMODE_EDITOR) {
        level_each(entity_t, &level->entities, it) {
            entity_t *ent = it.el;

            if (ent->destroy) {
                // do nothing
            } else if (ent->corpse) {
                // check if we should remove corpse
                if (secs_since_tick(ent->corpse_tick) >= ENTITY_CORPSE_TIME_S) {
                    entity_destroy(level, ent);
                }
            } else if (ent->sector) {
                // normal update
                entity_frame_update(level, ent);
            }
        }
    }

    // show colliders/hitboxes for entities
    if (g->visopt & (VISOPT_COLLIDERS | VISOPT_HITBOXES)) {
        level_each(entity_t, &level->entities, it) {
            entity_t *ent = it.el;
            if (ent->itype == ENTITY_TYPE_PLAYER) { continue; }

            const entity_bounds_t bounds = entity_bounds(level, it.el);

            if (g->visopt & VISOPT_COLLIDERS) {
                rand_t rand = rand_create(ent->id);
                debug_draw_cyl(
                    &(debug_draw_cyl_t) {
                        .p = bounds.origin,
                        .r = bounds.radius,
                        .h = bounds.height,
                        .color =
                            v4_of(
                                v3_abs(rand_v3_dir(&rand)),
                                1.0f)
                    });
            }

            if (g->visopt & VISOPT_HITBOXES) {
                debug_draw_cyl(
                    &(debug_draw_cyl_t) {
                        .p = bounds.origin,
                        .r = max(bounds.hitbox_radius, bounds.radius),
                        .h = max(bounds.hitbox_height, bounds.height),
                        .color = v4_of(1.0f, 0.1f, 0.1f, 1.0f),
                    });
            }
        }
    }
}

void level_fixed_update(level_t *level) {
    genlist_each(sector_t, &level->sectors, it) {
        sector_fixed_update(level, it.el);
    }

    const entity_t *fade_origin =
        level->entities_by_type[ENTITY_TYPE_FADE_ORIGIN].head;
    const f32 fog_dist =
        level->fog.dist == 0.0f ? DEFAULT_FOG_DIST : level->fog.dist;

    level_each(entity_t, &level->entities, it) {
        entity_t *ent = it.el;

        if (!ent->sector || ent->destroy || ent->corpse) { continue; }

        if (!ent->room) {
            ent->room = level_find_room(level, ent->pos);
        }

        v2 last_pos = ent->pos;

        if (entity_is_active(level, ent)) {
            entity_fixed_update(level, ent);
        }

        if (!v2_eqv_eps(ent->pos, last_pos)) {
            ent->room = level_find_room(level, ent->pos);
        }

        if (fade_origin) {
            // can only move fog_dist away from fade origin
            const v2_diff_t diff = v2_diff(fade_origin->pos, ent->pos);
            if(diff.dist > fog_dist) {
                entity_try_move(level, ent, v2_add(fade_origin->pos, v2_scale(diff.dir, fog_dist)));
            }
        }

        if (g->mode == GAMEMODE_EDITOR
            && !ent->ptype->is_attach
            && !ent->ptype->is_z_free) {
            ent->z = sector_point_zs(ent->sector, ent->pos).z0;
        }

        if (ent->update_blocks) {
            level_blocks_update_entity(level, ent);
        }
    }
}

void level_tick(level_t *level) {
    // tick sectors
    level_each(sector_t, &level->sectors, it) {
        sector_tick(level, it.el);
    }

    // tick entities
    if (g->mode != GAMEMODE_EDITOR) {
        level_each(entity_t, &level->entities, it) {
            if (!it.el->destroy
                && !it.el->corpse
                && entity_is_active(level, it.el)) {
                entity_tick(level, it.el);
            }
        }
    }
}

void level_enqueue_delete(level_t *level, lptr_t ptr) {
    level_fields_t *fields = lptr_level_fields(level, ptr);

    if (!fields || fields->lflags.deleted) {
        return;
    }

    fields->lflags.deleted = true;
    *dynlist_push(level->delete_ptrs) = ptr;
}

void level_enqueue_recalc(level_t *level, lptr_t ptr) {
    level_fields_t *fields = lptr_level_fields(level, ptr);

    if (!fields || fields->lflags.recalc_enqueued) {
        return;
    }

    fields->lflags.recalc_enqueued = true;
    *dynlist_push(level->recalc_ptrs) = ptr;
}

void level_push_dirty_sect_side(level_t *level, side_t *side) {
    if (side->lflags.dirty_side) { return; }
    side->lflags.dirty_side = true;
    *dynlist_push(level->dirty_sect_sides) = lptr_from(side);
}

static side_t *next_trace_side(side_t *side) {
    // move along wall from v0 -> v1
    vertex_t *vs[2];
    side_get_vertices(side, vs);

    // minimum found angle and corresponding side
    f32 a_min = TAU;
    side_t *next = NULL;

    // going from end of side line, try to find next side by finding wall
    // side with minimal angle
    dynlist_each(vs[1]->walls, it) {
        wall_t *vwall = *it.el;

        // ignore current wall
        if (vwall == side->wall) { continue; }

        for (int i = 0; i < 2; i++) {
            side_t *vside = vwall->sides[i];

            if (!vside) { continue; }

            // find angle between this side and next side
            vertex_t *us[2];
            side_get_vertices(vside, us);

            // side is not continuous from this side
            if (us[0] != vs[1]) { continue; }

            // get angle between sides vs[0] -> vs[1], us[0] -> us[1] where
            // vs[1] == us[0]
            const f32 a =
                angle_in_points_inner_cw(vs[0]->pos, vs[1]->pos, us[1]->pos);

            if (a < a_min) {
                a_min = a;
                next = vside;
            }
        }
    }

    return next;
}

// trace sides from a start side, picking sides which form the least angle
int level_trace_sides(
        level_t *level,
        side_t *start_side,
        DYNLIST(side_t*) *sides,
        const DYNLIST(side_t*) *fail_trace_sides) {
// #define DO_DEBUG_TRACE_SIDES

#ifdef DO_DEBUG_TRACE_SIDES
#define DEBUG_TRACE_SIDES(...) LOG(__VA_ARGS__)
#else
#define DEBUG_TRACE_SIDES(...)
#endif // ifdef DO_DEBUG_TRACE_SIDES

    ASSERT(start_side);
    ASSERT(sides);

    // number of sides written out
    int n = 0;

    // number of sides to return
    int count = 0;

    side_t *side = start_side;
    DEBUG_TRACE_SIDES("STARTING TRACE", side->index);
    while (side) {
        DEBUG_TRACE_SIDES("side: %d", side->index);

        if (fail_trace_sides) {
            dynlist_each(*fail_trace_sides, it) {
                if (*it.el == side) {
                    DEBUG_TRACE_SIDES("hit failtrace %d", side->index);

                    // if side is failtrace then this is a bad trace
                    count = 0;
                    goto done;
                }
            }
        }

        if (side->lflags.traced) {
            // if side is marked then it already exists
            // is this the start side? trace success. otherwise mark indicates
            // failure
            if (side == start_side) {
                DEBUG_TRACE_SIDES("hit start %d", side->index);
                count = n;
            } else {
                DEBUG_TRACE_SIDES("hit other %d", side->index);
                count = 0;
            }

            goto done;
        }

        // write side and mark
        *dynlist_push(*sides) = side;
        n++;
        side->lflags.traced = true;

        side_t *next = next_trace_side(side);

        if (!next) {
            DEBUG_TRACE_SIDES("no next after %d", side->index);
            // failure
            count = 0;
            goto done;
        }

        side = next;
    }

done:
    // clear all marks
    for (int i = 0; i < n; i++) {
        (*sides)[i]->lflags.traced = false;
    }


    DEBUG_TRACE_SIDES("END TRACE", side->index);
    return count;
}

side_t *level_find_near_side(
    const level_t *level,
    const vertex_t *vertex,
    const sector_t *preferred_sector) {
    side_t *res = NULL;

    dynlist_each(vertex->walls, it) {
        for (int i = 0; i < 2; i++) {
            side_t *side = (*it.el)->sides[i];

            if (side) {
                if (preferred_sector && side->sector == preferred_sector) {
                    return side;
                } else if (!res) {
                    res = side;
                }
            }
        }
    }

    return res;
}

sector_t *level_find_near_sector(
    const level_t *level,
    v2 point) {
    f32 dist = 1e30;
    sector_t *found = NULL;

    level_each(sector_t, &level->sectors, it_sect) {
        sector_t *sect = it_sect.el;

        // check if distance to any side is lesser
        llist_each(sector_sides, &sect->sides, iLTF_SIDE) {
            side_t *side = iLTF_SIDE.el;

            const f32 d =
                point_to_segment(
                    point,
                    side->wall->v0->pos,
                    side->wall->v1->pos);

            if (d < dist) {
                dist = d;
                found = sect;
                break;
            }
        }
    }

    return found;
}

// #define DO_DEBUG_UPDATE_SIDES

#ifdef DO_DEBUG_UPDATE_SIDES
#define DEBUG_UPDATE_SIDES(...) LOG(__VA_ARGS__)
#else
#define DEBUG_UPDATE_SIDES(...)
#endif // ifdef DO_DEBUG_UPDATE_SIDES

// checks if sides form a hole in the specified sector
static bool sides_form_sector_hole(
    level_t *level,
    side_t **sides,
    int n_sides,
    sector_t *sect) {
    if (n_sides == 0) { return false; }

    DYNLIST(side_t*) sector_sides =
        dynlist_create(side_t*, &g->frame_arena, sect->n_sides);
    sector_get_sides(level, sect, &sector_sides);

    // sides which have been removed
    DYNLIST(side_t*) fail_trace_sides =
        dynlist_create(side_t*, &g->frame_arena);

    // remove all sides from sector_sides which share a wall with any sides in
    // the side list. this means all the sides in "sides" are removed as well as
    // those with which they share a wall, should they be in the sector
    //
    // mark removed sides for trace failure as well
    DYNLIST(wall_t*) side_walls =
        dynlist_create(wall_t*, &g->frame_arena, n_sides);

    for (int i = 0; i < n_sides; i++) {
        *dynlist_push(side_walls) = sides[i]->wall;
    }

    dynlist_each(sector_sides, it_s) {
        side_t *side = *it_s.el;
        bool remove = false;

        dynlist_each(side_walls, it_w) {
            wall_t *wall = *it_w.el;

            if (side->wall == wall) {
                *dynlist_push(fail_trace_sides) = side;
                remove = true;
                break;
            }
        }

        if (remove) {
            dynlist_remove_it(sector_sides, it_s);
        }
    }

    if (dynlist_size(sector_sides) == 0) {
        // no hole if sides == sector
        return false;
    }

    // check that all remaining sides still form a trace that includes all other
    // sector sides - if it doesn't, then this can't be a hole
    DYNLIST(side_t*) trace = dynlist_create(side_t*, &g->frame_arena);
    if (!level_trace_sides(level, sector_sides[0], &trace, &fail_trace_sides)) {
        DEBUG_UPDATE_SIDES("failed because sect sides don't form a trace now");
        return false;
    }

    // check that all trace sides are found in sector sides
    int n_found = 0;
    dynlist_each(trace, it_t) {
        dynlist_each(sector_sides, it_s) {
            if (*it_s.el == *it_t.el) {
                n_found++;
                break;
            }
        }
    }

    if (n_found != dynlist_size(trace)) {
        DEBUG_UPDATE_SIDES("failed because not all trace sides are sect sides");
        return false;
    }

    // lines of sector_sides
    DYNLIST(line2f_t) sect_lines =
        dynlist_create(line2f_t, &g->frame_arena, dynlist_size(sector_sides));
    sides_to_lines(sector_sides, dynlist_size(sector_sides), &sect_lines);

    // lines of *sides
    DYNLIST(line2f_t) side_lines =
        dynlist_create(line2f_t, &g->frame_arena, n_sides);
    sides_to_lines(sides, n_sides, &side_lines);

    return
        polygon_is_hole(
            sect_lines, dynlist_size(sect_lines),
            side_lines, dynlist_size(side_lines));
}

// makes sides into a sector by removing them from whatever their current sector
// is (if they have one) and putting them into a new one
static sector_t *make_sides_into_sector(
    level_t *level,
    side_t **sides,
    int n_sides,
    DYNLIST(lptr_t) *to_recalc) {
    ASSERT(n_sides != 0);

    sector_t *new_sect =
        sector_new(
            level,
            level_find_near_sector(level, wall_midpoint(sides[0]->wall)));

    new_sect->lflags.do_not_recalc = true;
    *dynlist_push(*to_recalc) = lptr_from(new_sect);

    // remove sides from sectors
    for (int i = 0; i < n_sides; i++)  {
        if (sides[i]->sector) {
            sides[i]->sector->lflags.do_not_recalc = true;
            *dynlist_push(*to_recalc) = lptr_from(sides[i]->sector);
            sector_remove_side(level, sides[i]->sector, sides[i]);
            sector_add_side(level, new_sect, sides[i]);
        }
    }

    DEBUG_UPDATE_SIDES(
        "moved %d sides to new sector %d",
        n_sides,
        lptr_to_index(level, lptr_from(new_sect)));
    return new_sect;
}

// return true if all sides have same sector (also works for NULL sector)
static bool sides_have_same_sector(
    side_t **sides,
    int n) {
    for (int i = 1; i < n; i++) {
        if (sides[i]->sector != sides[0]->sector) {
            return false;
        }
    }

    return true;
}

sector_t *level_update_side_sector(
    level_t *level,
    side_t *side) {
    // result, NULL if no new sector is created
    sector_t *res = NULL;

    // trace starting at side
    DYNLIST(side_t*) trace = dynlist_create(side_t*, &g->frame_arena);
    const int n_trace = level_trace_sides(level, side, &trace, NULL);

    // normal sector for each traced side
    DYNLIST(sector_t*) normal_sects =
        dynlist_create(sector_t*, &g->frame_arena);

    // sectors to recalculate at the end of side update
    DYNLIST(lptr_t) to_recalc = dynlist_create(lptr_t, &g->frame_arena);

    // check if all sides are in the same sector
    const bool same_sect = sides_have_same_sector(trace, n_trace);

    // no trace
    if (n_trace == 0) {
        DEBUG_UPDATE_SIDES("no trace for side %d", side->id);
        if (side->sector) {
            DEBUG_UPDATE_SIDES("removing bad sector %d", side->sector->id);
            sector_delete(level, side->sector);
        }

        res = NULL;
        goto done;
    }

    // must stop here, trace contains double
    side_t *double_side = NULL;
    if ((double_side = level_sides_find_double(level, trace, n_trace))) {
        DEBUG_UPDATE_SIDES("double side is %d", double_side->id);

        // try to fix this by splitting the sides up into new sectors, if their
        // traces are different but entirely parts of the same sector
        side_t *double_other = side_other(double_side);
        ASSERT(double_other);
        ASSERT(side_other(double_other) == double_side);

        DYNLIST(side_t*)
            a_trace = dynlist_create(side_t*, &g->frame_arena),
            b_trace = dynlist_create(side_t*, &g->frame_arena);

        const int
            n_a_trace = level_trace_sides(level, double_side, &a_trace, NULL),
            n_b_trace = level_trace_sides(level, double_other, &b_trace, NULL);

        // both traces must complete in order to break sector
        if (n_a_trace == 0 || n_b_trace == 0) {
            DEBUG_UPDATE_SIDES("trace has double but one trace does not complete");
            res = NULL;
            goto done;
        }

        bool separate = true;
        dynlist_each(a_trace, it_a) {
            dynlist_each(b_trace, it_b) {
                if (*it_a.el == *it_b.el) {
                    separate = false;
                    break;
                }
            }
        }

        if (!separate) {
            DEBUG_UPDATE_SIDES("trace has double but not separate, can't fix");
            res = NULL;
            goto done;
        }

        // traces are separate! break the sector
        DEBUG_UPDATE_SIDES("traces are separate, breaking sector");

        // pick smaller group of sides to make into a new sector
        if (n_a_trace < n_b_trace) {
            res = make_sides_into_sector(level, a_trace, n_a_trace, &to_recalc);
        } else {
            res = make_sides_into_sector(level, b_trace, n_b_trace, &to_recalc);
        }
    }

    // check if sides form a hole in the sector on any of the normal points of
    // the sides
    dynlist_each(trace, it) {
        sector_t *normal_sect =
            level_find_point_sector(level, side_normal_point(*it.el), NULL);

        if (normal_sect) {
            *dynlist_push(normal_sects) = normal_sect;
            normal_sect->lflags.mark = true;
        }
    }

    sector_t *hole_sect = NULL;

    dynlist_each(normal_sects, it) {
        if (!(*it.el)->lflags.mark) {
            continue;
        }

        if (!hole_sect
            && sides_form_sector_hole(level, trace, n_trace, *it.el)) {
            hole_sect = *it.el;
        }

        (*it.el)->lflags.mark = false;
    }

    if (hole_sect) {
        DEBUG_UPDATE_SIDES(
            "sides are hole to sector %d",
            lptr_to_index(level, lptr_from(hole_sect)));

        dynlist_each(trace, it) {
            DEBUG_UPDATE_SIDES("  %d", (*it.el)->id);
        }

        // all sides are already in the hole sector, don't move them
        if (same_sect && hole_sect == side->sector) {
            DEBUG_UPDATE_SIDES("  not moving");
            res = NULL;
            goto done;
        }

        DEBUG_UPDATE_SIDES(
            "same_sect: %d, hole_sect: %d, side->sector: %d",
            same_sect,
            lptr_to_index(level, lptr_from(hole_sect)),
            lptr_to_index(level, lptr_from(side->sector)));

        // move sides to hole sector, update portals
        hole_sect->lflags.do_not_recalc = true;
        *dynlist_push(to_recalc) = lptr_from(hole_sect);

        dynlist_each(trace, it) {
            side_t *side = *it.el;

            if (side->sector == hole_sect) {
                // already in sector, no need to move
                continue;
            }

            // TODO: sector_remove_side should not be able to add sides to
            // another sector...
            while (side->sector) {
                *dynlist_push(to_recalc) = lptr_from(side->sector);
                side->sector->lflags.do_not_recalc = true;
                sector_remove_side(level, side->sector, side);
            }

            sector_add_side(level, hole_sect, side);
        }

        DEBUG_UPDATE_SIDES("  moved");
        res = NULL;
        goto done;
    }

    // if side is not in a sector, form a new sector if all other sides do not
    // have a sector. otherwise we cannot form a sector
    if (!side->sector) {
        dynlist_each(trace, it) {
            if ((*it.el)->sector) {
                DEBUG_UPDATE_SIDES("no sector AND no match");
                res = NULL;
                goto done;
            }
        }

        // form a new sector with these sides
        sector_t *new_sect =
            sector_new(
                level,
                level_find_near_sector(
                    level,
                    wall_midpoint(side->wall)));

        new_sect->lflags.do_not_recalc = true;
        *dynlist_push(to_recalc) = lptr_from(new_sect);

        dynlist_each(trace, it) {
            if ((*it.el)->sector == new_sect) {
                continue;
            }

            ASSERT(!(*it.el)->sector);
            sector_add_side(level, new_sect, *it.el);
        }

        DEBUG_UPDATE_SIDES("made new sector from empty");
        res = new_sect;
        goto done;
    }

    // if side sector has any double sides, it must be removed
    if (level_sector_find_double_side(level, side->sector)) {
        DEBUG_UPDATE_SIDES("invalid sector with double sides, but nothing can be done");
        res = NULL;
        goto done;
    }

    // if the sector is coherent, there is nothing to update
    if (level_sector_is_coherent(level, side->sector)) {
        DEBUG_UPDATE_SIDES("sector is coherent");
        res = NULL;
        goto done;
    }

    // create a sector out of these traced sides, using the selected side's
    // sector as the "dominant" sector
    if (!same_sect) {
        M_UNUSED int n_move = 0;

        // collect list of opposite sides which are portal to this sector
        dynlist_each(trace, it) {
            side_t *traceside = *it.el;
            if (traceside->sector && traceside->sector != side->sector) {
                traceside->sector->lflags.do_not_recalc = true;
                *dynlist_push(to_recalc) = lptr_from(traceside->sector);

                sector_remove_side(level, traceside->sector, traceside);
                n_move++;
            }
        }

        DEBUG_UPDATE_SIDES("moving %d sides", n_move);

        // add all other sides to side->sector
        dynlist_each(trace, it) {
            side_t *traceside = *it.el;
            if (!traceside->sector) {
                side->sector->lflags.do_not_recalc = true;
                *dynlist_push(to_recalc) = lptr_from(side->sector);

                sector_add_side(level, side->sector, traceside);
            }
        }

        // no new sector
        DEBUG_UPDATE_SIDES("no new sector but not samesect");
        res = NULL;
        goto done;
    }

    // it is ensured that all sides are in the same sector now

    // these sides form a hole but are not oriented such that they point out
    // into the rest of the sector - form a new sector with them

    // form a new sector with these sides
    res = make_sides_into_sector(level, trace, n_trace, &to_recalc);

done:;
    dynlist_each(to_recalc, it) {
        if (!lptr_is_valid(level, *it.el)) { continue; }
        level_fields_t *fields = lptr_level_fields(level, *it.el);

        if (fields->lflags.mark) { continue; }
        fields->lflags.do_not_recalc = false;

        lptr_recalculate(level, *it.el);
    }

    dynlist_each(to_recalc, it) {
        if (!lptr_is_valid(level, *it.el)) { continue; }
        lptr_level_fields(level, *it.el)->lflags.mark = false;
    }

    return res;
}

int level_intersect_walls_on_line(
    level_t *level,
    v2 p0,
    v2 p1,
    wall_t **walls,
    side_t ***sides,
    int n) {

    // normalized direction of p0 -> p1
    const v2 dir = v2_normalize(v2_sub(p1, p0));

    int i = 0;

    level_each(wall_t, &level->walls, it) {
        wall_t *w = it.el;

        if (!intersect_segs(p0, p1, w->v0->pos, w->v1->pos, NULL, NULL, NULL)) {
            continue;
        }

        if (i == n) {
            WARN("out of space in level_intersect_walls_on_line");
            return 0;
        }

        if (walls) { walls[i] = w; }

        if (sides) {
            // find "exit" side (that whose normal "dir" lies most on)
            const f32
                dotr = v2_dot(dir, w->normal),
                dotl = v2_dot(dir, v2_of(-w->normal.x, -w->normal.y));

            sides[i] = &w->sides[dotr > dotl ? 0 : 1];
        }

        i++;
    }

    return i;
}

side_t *level_sides_find_double(
    level_t *level,
    side_t **sides,
    int n) {
    for (int i = 0; i < n; i++) {
        if (!sides[i]->sector) { continue; }

        side_t *other = side_other(sides[i]);
        if (other && other->sector && other->sector == sides[i]->sector) {
            return sides[i];
        }
    }
    return NULL;
}

side_t *level_sector_find_double_side(
    level_t *level,
    sector_t *sector) {
    DYNLIST(side_t*) sides = dynlist_create(side_t*, &g->frame_arena);
    sector_get_sides(level, sector, &sides);
    return level_sides_find_double(level, sides, dynlist_size(sides));
}

bool level_sector_is_coherent(
    level_t *level,
    sector_t *sector) {
    DYNLIST(side_t*) sides = dynlist_create(side_t*, &g->frame_arena);
    const int n_sides = sector_get_sides(level, sector, &sides);

    // an "outline" is a trace of sides in the sector which do *not* form a hole
    // a coherent sector can, by definition, only have 0 outlines (all sides
    // form a single trace) or 1 outline (all holes are within this 1 outline)
    int n_outlines = 0;

    // keep track of sides which have already been traced
    DYNLIST(side_t*) traced = dynlist_create(side_t*, &g->frame_arena);

    dynlist_reserve(traced, n_sides);


    bool res = true;

    // trace each (unmarked) side
    for (int i = 0; i < n_sides; i++) {
        // check if side has been traced already
        bool found = false;
        dynlist_each(traced, it) {
            if (sides[i] == *it.el) {
                found = true;
                break;
            }
        }

        if (found) { continue; }

        // current list of traced sides
        DYNLIST(side_t*) trace = dynlist_create(side_t*, &g->frame_arena);
        const int ntrace = level_trace_sides(level, sides[i], &trace, NULL);

        // are all sides in trace also part of this sector?
        for (int j = 0; j < ntrace; j++) {
            if (trace[j]->sector != sides[i]->sector) {
                DEBUG_UPDATE_SIDES("incorrect trace");
                res = false;
                goto done;
            }

            // no need to check this side again
            *dynlist_push(traced) = trace[j];
        }

        if (ntrace != n_sides) {
            DEBUG_UPDATE_SIDES("ntrace %d, n_sides %d", ntrace, n_sides);

            // we have gotten a smaller portion of the sector - is it a hole?
            if (sides_form_sector_hole(level, trace, ntrace, sides[i]->sector)) {
                // OK
            } else {
                // this must be an outline
                n_outlines++;

                if (n_outlines >= 2) {
                    // only 0/1 outlines possible in coherent sector
                    DEBUG_UPDATE_SIDES("multiple outlines");
                    res = false;
                    goto done;
                }
            }
        }

        dynlist_destroy(trace);
    }

done:
    // if res (success), all sides ought to be in "traced"
    ASSERT(!res || dynlist_size(traced) == n_sides);
    return res;
}

side_t *level_nearest_side(
    const level_t *level,
    v2 point) {
    // TODO: more effecient search, use blocks?
    const side_t *side = NULL;
    f32 dist = 1e20f;
    level_each(side_t, &level->sides, it) {
        const v2 p =
            point_project_segment(
                point,
                it.el->wall->v0->pos,
                it.el->wall->v1->pos);

        const f32 d = v2_norm(v2_sub(point, p));
        const int sgn =
            (int) sign(v2_dot(side_normal(it.el), v2_sub(point, p)));

        // take nearer side *or* side on same wall but which actually points to
        // point
        if (d < dist || (side && side == side_other(it.el) && sgn >= 0)) {
            dist = d;
            side = it.el;
        }
    }

    return (side_t*) side;
}

vertex_t *level_nearest_vertex(
    const level_t *level,
    v2 point,
    f32 *dist) {
    vertex_t *v = NULL;
    f32 d = 1e10;

    // TODO: search via block
    level_each(vertex_t, &level->vertices, it) {
        const f32 d_it = v2_norm2(v2_sub(point, it.el->pos));
        if (v == NULL || d_it < d) {
            v = it.el;
            d = d_it;
        }
    }

    if (dist) { *dist = d; }
    return v;
}

sector_t *level_find_point_sector(
        const level_t *level,
        v2 point,
        const sector_t *sector) {
    if (sector && sector_contains_point(sector, point)) {
        return (sector_t*) sector;
    }

    if (!level->blocks.arr) {
        // OK that this is costly, only used during loading
        level_each(sector_t, &level->sectors, it) {
            if (sector_contains_point(it.el, point)) {
                return it.el;
            }
        }
    } else {
        // block data is OK, use it
        const block_t *block =
            level_get_block(level, level_pos_to_block(point));

        if (!block) {
            return NULL;
        }

        dynlist_each(block->sectors, it) {
            if (sector_contains_point(*it.el, point)) {
                return *it.el;
            }
        }
    }

    return NULL;
}

subsector_t *level_find_point_subsector(
        const level_t *level,
        v2 point,
        const subsector_t *sub) {
    if (sub) {
        // check same subsector
        if (subsector_contains_point(sub, point)) {
            return (subsector_t*) sub;
        }

        // TODO: consider this - but sectors are usually bigger/will contain
        // more subs than just checking blocks?
    }

    if (!level->blocks.arr) {
        // OK that this is costly, only used during loading
        blklist_each(subsector_t, &level->subsectors, it) {
            if (subsector_contains_point(it.el, point)) {
                return it.el;
            }
        }
    } else {
        // block data is OK, use it
        const v2i block_pos = level_pos_to_block(point);
        const block_t *block = level_get_block(level, block_pos);

        if (!block) {
            return NULL;
        }

        dynlist_each(block->subsectors, it) {
            const subsector_t *sub =
                blklist_ptr_unsafe(subsector_t, &level->subsectors, *it.el);
            ASSERT_DEBUG(sub);

            if (subsector_contains_point(sub, point)) {
                return (subsector_t*) sub;
            }
        }
    }

    return NULL;
}

v2 level_clamp_point(const level_t *l, v2 point) {
    subsector_t *sub = level_find_point_subsector(l, point, NULL);

    if (sub) {
        // already in subsector
        return point;
    }

    // clamp point to block space
    point =
        v2_clampv(
            point,
            v2_scale(v2_from_i(l->blocks.offset), BLOCK_SIZE),
            v2_scale(
                v2_from_i(
                    v2i_add(l->blocks.offset, l->blocks.size)),
                BLOCK_SIZE));

    const v2i bpos = level_pos_to_block_clamped(l, point);

    // find nearby blocks in expanding radius
    int r = 1;
    while (r < max(l->blocks.size.x, l->blocks.size.y)) {
        int n_blocks = 0;
        block_t *blocks[max(4 * (1 + (2 * r)), 1)];

        for (int y = -r; y <= +r; y++) {
            block_t *b;
            if ((b = level_get_block(l, v2i_of(bpos.x - r, bpos.y + y)))) {
                blocks[n_blocks++] = b;
            }

            if ((b = level_get_block(l, v2i_of(bpos.x + r, bpos.y + y)))) {
                blocks[n_blocks++] = b;
            }
        }

        // use (r - 1) since -r and +r were already covered above
        for (int x = -(r - 1); x <= +(r - 1); x++) {
            block_t *b;
            if ((b = level_get_block(l, v2i_of(bpos.x + x, bpos.y - r)))) {
                blocks[n_blocks++] = b;
            }

            if ((b = level_get_block(l, v2i_of(bpos.x + x, bpos.y + r)))) {
                blocks[n_blocks++] = b;
            }
        }

        bool ok = false;
        v2 p_best = point;
        f32 d2_best = 1e10f;

        // check each subsector
        for (int i = 0; i < n_blocks; i++) {
            dynlist_each(blocks[i]->subsectors, it) {
                // got at least one subsector to clamp to
                ok = true;

                subsector_t *sub =
                    blklist_ptr_unsafe(subsector_t, &l->subsectors, *it.el);

                const v2 p = subsector_clamp_point(sub, point);
                const f32 d2 = v2_norm2(v2_sub(p, point));
                if (d2 < d2_best) {
                    p_best = p;
                    d2_best = d2;
                }
            }
        }

        if (ok) {
            point = p_best;
            break;
        }

        r++;
    }

    return point;
}

v3 level_clamp_point_3d(const level_t *level, v3 point, f32 height) {
    const v2 xy = level_clamp_point(level, v2_from(point));
    const subsector_t *sub = level_find_point_subsector(level, xy, NULL);
    if (!sub) { return point; }

    const rangef_t zs = sector_point_zs(sub->parent, xy);
    return
        v3_of(
            xy,
            clamp(
                point.z,
                zs.z0,
                max(zs.z1 - height, zs.z0)));
}

void level_sides_in_radius(
        level_t *level,
        const level_sides_in_radius_params_t *params) {
    const f32 r2 = params->r * params->r;

    const v2i
        bmin =
            level_pos_to_block_clamped(
                level, v2_add(params->pos, v2_of(-params->r))),
        bmax =
            level_pos_to_block_clamped(
                level, v2_add(params->pos, v2_of(+params->r)));

    level_for_blocks_in_area(level, bmin, bmax, it) {
        dynlist_each(it.el->walls, it) {
            const wall_t *w = *it.el;
            const v2 proj =
                point_project_segment(params->pos, w->v0->pos, w->v1->pos);

            if (v2_distance2(params->pos, proj) > r2) {
                // out of radius
                continue;
            }

            for (int i = 0; i < 2; i++) {
                if (!w->sides[i]) {
                    // no side
                    continue;
                }

                if (params->filter_fn
                    && !params->filter_fn(w->sides[i], params)) {
                    // filtered
                    continue;
                }

                *dynlist_push(*params->out) = w->sides[i];
            }
        }
    }
}

typedef struct {
    bool is_radius;
    union {
        struct {
            v2 pos;
            f32 r2;
        };

        box2f_t box;
    };

    DYNLIST(wall_t*) *out;
} walls_in_data_t;

static bool traverse_walls_in(
    level_t *level,
    block_t *block,
    v2i,
    walls_in_data_t *data) {
    dynlist_each(block->walls, it) {
        if (!(*it.el)->lflags.mark
            && ((data->is_radius
                && point_to_segment2(
                    data->pos,
                    (*it.el)->v0->pos,
                    (*it.el)->v1->pos) <= data->r2)
                || (!data->is_radius
                    && box2f_vs_line(
                            data->box,
                            (*it.el)->v0->pos,
                            (*it.el)->v1->pos)))) {
            (*it.el)->lflags.mark = true;
            *dynlist_push(*data->out) = *it.el;
        }
    }
    return true;
}

int level_walls_in_radius(
        level_t *level,
        v2 pos,
        f32 r,
        DYNLIST(wall_t*) *out) {
    walls_in_data_t data = {
        .is_radius = true,
        .pos = pos,
        .r2 = r * r,
        .out = out
    };

    const int n_start = dynlist_size(*out);
    level_traverse_block_area(
        level,
        v2_sub(pos, v2_of(r)),
        v2_add(pos, v2_of(r)),
        (traverse_blocks_f) traverse_walls_in,
        &data,
        LTB_NONE);

    for (int i = n_start; i < dynlist_size(*out); i++) {
        ASSERT_DEBUG((*out)[i]->lflags.mark);

        // TODO: use llist? or set
        (*out)[i]->lflags.mark = false;
    }

    return dynlist_size(*out) - n_start;
}

int level_walls_in_area(
        level_t *level,
        box2f_t area,
        DYNLIST(wall_t*) *out) {
    walls_in_data_t data = {
        .is_radius = false,
        .box = area,
        .out = out
    };

    const int n_start = dynlist_size(*out);
    level_traverse_block_area(
        level,
        area.min,
        area.max,
        (traverse_blocks_f) traverse_walls_in,
        &data,
        LTB_NONE);

    for (int i = n_start; i < dynlist_size(*out); i++) {
        ASSERT_DEBUG((*out)[i]->lflags.mark);
        (*out)[i]->lflags.mark = false;
    }

    return dynlist_size(*out) - n_start;
}

typedef struct {
    v2 pos;
    f32 r;
    DYNLIST(sector_t*) *out;

    // set of u16 index
    map_t visited;
} sectors_in_radius_data_t;

static bool traverse_sectors_in_radius(
        level_t *level,
        block_t *block,
        v2i,
        sectors_in_radius_data_t *data) {
    typedef struct {
        sector_t *ptr;
        v2 pos;
    } queue_entry_t;

    DYNLIST(queue_entry_t) queue =
        dynlist_create(queue_entry_t, &g->frame_arena);

    dynlist_each(block->sectors, it) {
        if (!map_getp(&data->visited, &(*it.el)->handle)) {
            *dynlist_push(queue) = (queue_entry_t) {
                .ptr = *it.el,
                .pos =  data->pos
            };

            *dynlist_push(*data->out) = *it.el;
        }
    }

    while (dynlist_size(queue) != 0) {
        queue_entry_t entry = dynlist_pop(queue);
        sector_t *s = entry.ptr;

        if (map_getp(&data->visited, &s->handle)) {
            continue;
        }

        const v2 pos = entry.pos;
        map_insertpk(&data->visited, &s->handle);

        // TODO: could sector_clamp_point, but is that necessary for the perf
        // penalty?
        const v2 c = v2_lerp(entry.ptr->min, entry.ptr->max, 0.5f);
        if (v2_distance2(pos, c) < data->r * data->r) {
            *dynlist_push(*data->out) = s;

            // enqueue non-visited immediate neighbors
            llist_each(sector_sides, &s->sides, it) {
                if (it.el->portal
                    && it.el->portal->sector
                    && !map_containsp(
                        &data->visited,
                        &it.el->portal->sector->handle)) {
                    const v2 entry_pos =
                        it.el->is_disconnect ?
                            portal_transform(
                                    level,
                                    it.el,
                                    it.el->portal,
                                    data->pos)
                            : data->pos;

                    *dynlist_push(queue) =
                        (queue_entry_t) {
                            .ptr = it.el->portal->sector,
                            .pos = entry_pos
                        };
                }
            }
        }
    }

    dynlist_destroy(queue);
    return true;
}

int level_sectors_in_radius(
    level_t *level, v2 pos, f32 r, DYNLIST(sector_t*) *out) {
    sectors_in_radius_data_t data = {
        .pos = pos,
        .r = r,
        .out = out
    };

    map_init(
        &data.visited,
        &g->frame_arena,
        sizeof_field(sector_t, handle),
        0,
        map_hash_bytes, map_cmp_bytes,
        NULL, NULL, NULL);

    const int n_start = dynlist_size(*out);
    level_traverse_block_area(
        level,
        v2_sub(pos, v2_of(r)),
        v2_add(pos, v2_of(r)),
        (traverse_blocks_f) traverse_sectors_in_radius,
        &data,
        LTB_NONE);

    map_destroy(&data.visited);
    return dynlist_size(*out) - n_start;
}

int level_contiguous_sectors_in_radius(
        level_t *level,
        sector_t *start,
        v2 pos,
        f32 r,
        DYNLIST(sector_t*) *out) {
    int i = 0;

    BITMAP_DECL_STATIC(visited, MAX_SECTORS);
    bitmap_fill_n(&visited, 0, level->sectors.data.capacity);

    DYNLIST(sector_t*) queue = dynlist_create(sector_t*, &g->frame_arena);
    *dynlist_push(queue) = start;

    *dynlist_push(*out) = start;

    while(dynlist_size(queue) != 0) {
        sector_t *s = dynlist_pop(queue);

        dynlist_each(s->neighbors, it) {
            sector_t *n = *it.el;

            if (!bitmap_get(&visited, n->id)) {
                bitmap_set(&visited, n->id);

                const v2 p = sector_clamp_point(n, pos);
                const float dist = v2_norm(v2_sub(p, pos));

                if (dist <= r) {
                    *dynlist_push(*out) = n;
                    *dynlist_push(queue) = n;
                    i++;
                }
            }
        }
    }

    return i;
}

static void accumulate_subs_in_area(
        level_t *level,
        box2f_t box,
        LLIST(subsector_t) *list) {
    LLIST(subsector_t) subs = { NULL };

    level_for_blocks_in_area(
        level,
        level_pos_to_block_clamped(level, box.min),
        level_pos_to_block_clamped(level, box.max),
        it) {
        dynlist_each(it.el->subsectors, it) {
            subsector_t *sub =
                blklist_ptr_unsafe(
                    subsector_t,
                    &level->subsectors,
                    *it.el);

            if (!sub->node.next && subs.head != sub) {
                llist_prepend(node, &subs, sub);
            }
        }
    }

    list->head = subs.head;
}

void level_entities_in_radius(
        level_t *level,
        v2 pos,
        f32 r,
        DYNLIST(entity_t*) *out) {
    LLIST(subsector_t) subs;
    accumulate_subs_in_area(level, box2f_ch(pos, v2_of(r)), (void*) &subs);

    const f32 r2 = r * r;
    while (!llist_empty(&subs)) {
        subsector_t *sub = llist_pop_front(node, &subs);

        dlist_each(subsector_node, &sub->entities, it) {
            if (v2_distance2(pos, it.el->pos) < r2) {
                *dynlist_push(*out) = it.el;
            }
        }
    }
}

void level_entities_in_radius_3d(
        level_t *level,
        v3 pos,
        f32 r,
        DYNLIST(entity_t*) *out) {
    LLIST(subsector_t) subs;
    accumulate_subs_in_area(
        level,
        box2f_ch(v2_from(pos), v2_of(r)),
        (void*) &subs);

    const f32 r2 = r * r;
    while (!llist_empty(&subs)) {
        subsector_t *sub = llist_pop_front(node, &subs);

        dlist_each(subsector_node, &sub->entities, it) {
            if (v3_distance2(pos, it.el->pos_xyz) < r2) {
                *dynlist_push(*out) = it.el;
            }
        }
    }
}

entity_t *level_nearest_entity_with_type(
        level_t *level,
        v3 pos,
        f32 r,
        entity_type_e type) {
    if (r == 0.0f) {
        entity_t *nearest = NULL;
        f32 d2_nearest = 1e10f;
        dlist_each(level_by_type_node, &level->entities_by_type[type], it) {
            const f32 d2 = v3_distance2(pos, it.el->pos_xyz);
            if (d2 < d2_nearest) {
                nearest = it.el;
                d2_nearest = d2;
            }
        }

        return nearest;
    } else {
        LLIST(subsector_t) subs;
        accumulate_subs_in_area(
            level,
            box2f_ch(v2_from(pos), v2_of(r)),
            (void*) &subs);

        entity_t *nearest = NULL;
        f32 d2_nearest = 1e10f;
        const f32 r2 = r * r;

        while (!llist_empty(&subs)) {
            subsector_t *sub = llist_pop_front(node, &subs);

            dlist_each(subsector_node, &sub->entities, it) {
                if (it.el->itype != type) {
                    continue;
                }

                const f32 d2 = v2_distance2(v2_from(pos), it.el->pos);
                if (d2 < r2 && d2 < d2_nearest) {
                    d2_nearest = d2;
                    nearest = it.el;
                }
            }
        }

        return nearest;
    }
}

typedef struct {
    box2f_t area;
    DYNLIST(subsector_t*) *out;
    v2 pos;
    f32 radius;
} traverse_subsectors_data_t;

static bool traverse_subsectors(
        level_t *level,
        block_t *block,
        v2i,
        void *userdata) {
    traverse_subsectors_data_t *data = userdata;

    // find subsectors which overlap
    dynlist_each(block->subsectors, it) {
        subsector_t *sub = blklist_ptr(subsector_t, &level->subsectors, *it.el);
        if ((data->radius == 0.0f && box2f_collides(data->area, sub->bounds))
            || (box2f_distance_to_point(sub->bounds, data->pos)
                    <= data->radius)) {
            *dynlist_push(*data->out) = sub;
        }
    }

    return true;
}

void level_subsectors_in_area(
        level_t *level,
        box2f_t area,
        DYNLIST(subsector_t*) *out) {
    traverse_subsectors_data_t data = {
        .area = area,
        .out = out,
        .radius = 0.0f,
    };

    level_traverse_block_area(
        level, area.min, area.max, traverse_subsectors, &data, LTB_NONE);
}

void level_subsectors_in_radius(
        level_t *level,
        v2 pos,
        f32 r,
        DYNLIST(subsector_t*) *out) {
    const box2f_t area = box2f_ch(pos, v2_of(r));

    traverse_subsectors_data_t data = {
        .area = area,
        .out = out,
        .pos = pos,
        .radius = r,
    };

    level_traverse_block_area(
        level, area.min, area.max, traverse_subsectors, &data, LTB_NONE);
}

typedef struct traverse_tris_data {
    bool use_radius;
    box2f_t area;
    v2 pos;
    f32 r;
    DYNLIST(sect_tri_t*) *out;

    // list of visited subsectors
    DYNLIST(subsector_t*) visited;
} traverse_tris_data_t;

static bool traverse_tris_in_area(
    level_t *level,
    block_t *block,
    v2i,
    traverse_tris_data_t *data) {

    dynlist_each(block->subsectors, it) {
        subsector_t *sub = blklist_ptr(subsector_t, &level->subsectors, *it.el);

        ASSERT(genlist_present(&level->sectors, sub->parent->handle));

        // already visited
        if (sub->mark) { continue; }
        *dynlist_push(data->visited) = sub;

        // check if subsector overlaps at all
        if (!box2f_collides(data->area, box2f_mm(sub->min, sub->max))) {
            continue;
        }

        dynlist_each(sub->tris, it) {
            if (data->use_radius) {
                if (intersect_circle_triangle(
                        data->pos,
                        data->r,
                        (*it.el)->a->pos,
                        (*it.el)->b->pos,
                        (*it.el)->c->pos,
                        NULL)) {
                    *dynlist_push(*data->out) = *it.el;
                }
            } else if (
                box2f_vs_triangle(
                    data->area,
                    (*it.el)->a->pos,
                    (*it.el)->b->pos,
                    (*it.el)->c->pos)) {
                *dynlist_push(*data->out) = *it.el;
            }
        }
    }

    return true;
}

int level_tris_in_area(
    level_t *level, box2f_t area, DYNLIST(sect_tri_t*) *out) {
    traverse_tris_data_t data = {
        .use_radius = false,
        .area = area,
        .out = out,
        .visited = dynlist_create(subsector_t*, &g->frame_arena),
    };

    const int n_start = dynlist_size(*out);
    level_traverse_block_area(
        level,
        area.min,
        area.max,
        (traverse_blocks_f) traverse_tris_in_area,
        &data,
        LTB_NONE);

    dynlist_each(data.visited, it) { (*it.el)->mark = false; }

    return dynlist_size(*out) - n_start;
}

int level_tris_in_radius(
    level_t *level, v2 pos, f32 r, DYNLIST(sect_tri_t*) *out) {
    traverse_tris_data_t data = {
        .use_radius = true,
        .area = box2f_ch(pos, v2_of(r)),
        .pos = pos,
        .r = r,
        .out = out,
        .visited = dynlist_create(subsector_t*, &g->frame_arena),
    };

    const int n_start = dynlist_size(*out);
    level_traverse_block_area(
        level,
        data.area.min,
        data.area.max,
        (traverse_blocks_f) traverse_tris_in_area,
        &data,
        LTB_NONE);

    dynlist_each(data.visited, it) { (*it.el)->mark = false; }

    return dynlist_size(*out) - n_start;
}

v2 level_random_point_in_radius(
    level_t *level, rand_t *rand, v2 pos, f32 r) {
    DYNLIST(sect_tri_t*) tris = dynlist_create(sect_tri_t*, &g->frame_arena);
    level_tris_in_radius(level, pos, r, &tris);

    f32 total = 0.0f;
    dynlist_each(tris, it) {
        total += (*it.el)->area;
    }

    const f32 cutoff = rand_f32(&g->rand, 0.0f, total);
    f32 acc = 0.0f;

    sect_tri_t *tri = tris[0];
    for (int i = 0, n = dynlist_size(tris); i < n; i++) {
        acc += tris[i]->area;

        if (cutoff <= acc) {
            tri = tris[i];
            break;
        }
    }

    return rand_v2_triangle(rand, tri->a->pos, tri->b->pos, tri->c->pos);
}

v2 level_random_point_in_radius_by_volume(
    level_t *level, rand_t *rand, v2 pos, f32 r) {
    DYNLIST(sect_tri_t*) tris = dynlist_create(sect_tri_t*, &g->frame_arena);
    level_tris_in_radius(level, pos, r, &tris);

    f32 total = 0.0f;
    dynlist_each(tris, it) {
        total += sect_tri_volume(*it.el, 0.0f);
    }

    const f32 cutoff = rand_f32(&g->rand, 0.0f, total);
    f32 acc = 0.0f;

    sect_tri_t *tri = tris[0];
    for (int i = 0, n = dynlist_size(tris); i < n; i++) {
        acc += sect_tri_volume(tris[i], 0.0f);

        if (cutoff <= acc) {
            tri = tris[i];
            break;
        }
    }

    return rand_v2_triangle(rand, tri->a->pos, tri->b->pos, tri->c->pos);
}

static void level_transform_ptrs(
        level_t *level,
        DYNLIST(lptr_t) *ptrs,
        void (*transform_fn)(level_t*, lptr_t, void*),
        void *userdata) {
    DYNLIST(lptr_t) all_ptrs = dynlist_create(lptr_t, &g->frame_arena);

    if (ptrs) {
        dynlist_push_all(all_ptrs, *ptrs);
    } else {

#define ACCUMULATE_PTRS(type, list)                 \
    level_each(type, &level->list, it) {            \
        *dynlist_push(all_ptrs) = lptr_from(it.el); \
    }

        ACCUMULATE_PTRS(vertex_t, vertices)
        ACCUMULATE_PTRS(wall_t,   walls)
        ACCUMULATE_PTRS(side_t,   sides)
        ACCUMULATE_PTRS(sector_t, sectors)
        ACCUMULATE_PTRS(decal_t,  decals)
        ACCUMULATE_PTRS(entity_t, entities)
        ACCUMULATE_PTRS(room_t,   rooms)

#undef ACCUMULATE_PTRS
    }

    // disallow recalc on all ptrs, transform them
    dynlist_each(all_ptrs, it) {
        lptr_level_fields(level, *it.el)->lflags.do_not_recalc = true;
        transform_fn(level, *it.el, userdata);
    }

    // allow recalc
    dynlist_each(all_ptrs, it) {
        lptr_level_fields(level, *it.el)->lflags.do_not_recalc = false;
    }

    // recalculate vertices (implicitly does walls and sectors)
    dynlist_each(all_ptrs, it) {
        if (lptr_type_flag(*it.el) == LTF_VERTEX) {
            vertex_recalculate(level, lptr_vertex(level, *it.el));
        }
    }
}

static void level_shift__transform(level_t *level, lptr_t ptr, void *userdata) {
    const v3 shift = *(v3*) userdata;

    switch (lptr_type_flag(ptr)) {
    case LTF_VERTEX: {
        vertex_t *v = lptr_vertex(level, ptr);
        v->pos = v2_add(v->pos, v2_from(shift));
    } break;
    case LTF_ENTITY: {
        entity_t *o = lptr_entity(level, ptr);
        o->pos_xyz = v3_add(o->pos_xyz, shift);
    } break;
    case LTF_DECAL: {
        decal_t *d = lptr_decal(level, ptr);
        if (!d->is_on_side) {
            d->sector.pos = v2_add(d->sector.pos, v2_from(shift));
        }
    } break;
    case LTF_SECTOR: {
        sector_t *s = lptr_sector(level, ptr);
        for (int i = 0; i < 2; i++) {
            s->planes[i].z += shift.z;
        }
    } break;
    case LTF_ROOM: {
        room_t *r = lptr_room(level, ptr);
        r->bounds = box2i_translate(r->bounds, v2i_from_v(v2_from(shift)));
    } break;
    }
}

void level_shift(level_t *level, v3 shift, DYNLIST(lptr_t) *ptrs) {
    level_transform_ptrs(
        level,
        ptrs,
        level_shift__transform,
        &shift);
}

static void level_rotate__transform(level_t *level, lptr_t ptr, void *userdata) {
    const v3 ref_and_angle = *(v3*) userdata;
    const v2 pos = v2_from(ref_and_angle);
    const f32 angle = ref_and_angle.z;

    switch (lptr_type_flag(ptr)) {
    case LTF_VERTEX: {
        vertex_t *v = lptr_vertex(level, ptr);
        v->pos = v2_add(v2_rotate(v2_sub(v->pos, pos), angle), pos);
    } break;
    case LTF_ENTITY: {
        entity_t *o = lptr_entity(level, ptr);
        o->pos = v2_add(v2_rotate(v2_sub(o->pos, pos), angle), pos);
    } break;
    case LTF_DECAL: {
        decal_t *d = lptr_decal(level, ptr);
        if (!d->is_on_side) {
            d->sector.pos =
                v2_add(
                    v2_rotate(v2_sub(d->sector.pos, pos), angle), pos);
        }
    } break;
    }
}

void level_rotate(level_t *level, v2 pos, f32 angle, DYNLIST(lptr_t) *ptrs) {
    v3 ref_and_angle = v3_of(pos, angle);
    level_transform_ptrs(
        level,
        ptrs,
        level_rotate__transform,
        &ref_and_angle);
}

rangef_t level_point_zs(const level_t *level, v2 point) {
    const sector_t *sector = level_find_point_sector(level, point, NULL);
    if (!sector) {
        return (rangef_t) { 0 };
    }

    return sector_point_zs(sector, point);
}

typedef struct {
    // entire area
    box2f_t area;

    // set of lptrs
    map_t ptrs;

    // LPIA_*
    int flags;

    // T_*
    int tags;
} level_ptrs_in_area__data_t;

static bool level_ptrs_in_area__traverse(
        level_t *level,
        block_t *block,
        v2i,
        level_ptrs_in_area__data_t *data) {

    if (data->tags & (LTF_WALL | LTF_SIDE | LTF_DECAL | LTF_VERTEX)) {
        dynlist_each(block->walls, it) {
            if (data->tags & LTF_VERTEX) {
                for (int i = 0; i < 2; i++) {
                    if (box2f_contains(
                            data->area,
                            (*it.el)->vertices[i]->pos)) {
                        map_insertk(
                            &data->ptrs,
                            lptr_from((*it.el)->vertices[i]));
                    }
                }
            }

            const bool
                whole =
                    box2f_contains(data->area, (*it.el)->v0->pos)
                    && box2f_contains(data->area, (*it.el)->v1->pos),
                partial =
                    box2f_vs_line(
                        data->area, (*it.el)->v0->pos, (*it.el)->v1->pos);

            if ((data->tags & LTF_WALL)
                && (((data->flags & LPIA_WHOLE_WALL) && whole)
                    || (!(data->flags & LPIA_WHOLE_WALL) && partial))) {
                map_insertk(&data->ptrs, lptr_from(*it.el));
            }

            if (partial && (data->tags & (LTF_SIDE | LTF_DECAL))) {
                for (int i = 0; i < 2; i++) {
                    if (!(*it.el)->sides[i]) { continue; }

                    if (data->tags & LTF_SIDE) {
                        map_insertk(
                            &data->ptrs,
                            lptr_from((*it.el)->sides[i]));
                    }

                    if (data->tags & LTF_DECAL) {
                        llist_each(node, &(*it.el)->sides[i]->decals, it) {
                            ASSERT(it.el->is_on_side);
                            if (box2f_contains(
                                    data->area,
                                    v2_from(
                                        decal_worldpos(it.el)))) {
                                map_insertk(
                                    &data->ptrs,
                                    lptr_from(it.el));
                            }
                        }
                    }
                }
            }
        }
    }

    if (data->tags & LTF_ENTITY) {
        dynlist_each(block->entities, it) {
            if (box2f_vs_circle(
                    data->area,
                    (*it.el)->pos,
                    entity_bounds(level, *it.el).radius)) {
                map_insertk(&data->ptrs, lptr_from(*it.el));
            }
        }
    }

    if (data->tags & LTF_SECTOR) {
        dynlist_each(block->sectors, it) {
            if (((data->flags & LPIA_WHOLE_SECT)
                    && sector_contained_by_box2f(*it.el, data->area))
                || (!(data->flags & LPIA_WHOLE_SECT)
                    && sector_intersects_box2f(*it.el, data->area))) {
                map_insertk(&data->ptrs, lptr_from(*it.el));
            }
        }
    }

    return true;
}

void level_ptrs_in_area(
    const level_t *level,
    box2f_t area,
    int tags,
    DYNLIST(lptr_t) *ptrs,
    int flags) {
    area = box2f_sort(area);

    level_ptrs_in_area__data_t data = { .area = area, .tags = tags, .flags = flags };
    map_init(
        &data.ptrs,
        &g->frame_arena,
        sizeof(lptr_t),
        0,
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    level_traverse_block_area(
        (level_t*) level, area.min, area.max,
        (traverse_blocks_f) level_ptrs_in_area__traverse,
        &data,
        0);

    map_each(lptr_t, void, &data.ptrs, it) {
        *dynlist_push(*ptrs) = *it.key;
    }

    // rooms are not contained by blocks, must scan all
    if (tags & LTF_ROOM) {
        level_each(room_t, &level->rooms, it) {
            if (box2f_collides(room_min_box(it.el), area)
                || box2f_collides(room_max_box(it.el), area)) {
                *dynlist_push(*ptrs) = lptr_from(it.el);
            }
        }
    }
}
