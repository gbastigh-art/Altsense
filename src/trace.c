#include "trace.h"
#include "game.h"
#include "level/block.h"
#include "level/decal.h"
#include "level/entity.h"
#include "level/level.h"
#include "level/portal.h"
#include "level/sector.h"
#include "level/side.h"
#include "level/wall.h"

#define NEAR_WALL_THRESHOLD 0.25f   // max distance where a wall is near
#define TRACE_MAX_RETRIES 8         // maximum number of movement retries
#define TRACE_PATH_MAX_ITERS 1024

// sort on t and sort secondarily on decals, since we always want to hit decals
// before the surface they are on.
static int trace_hit_cmp(const void *a, const void *b, void *userdata) {
    trace_2d_t *trace = userdata;
    const trace_hit_t *ta = a, *tb = b;
    const f32 diff = ta->t - tb->t;

    if (fabsf(diff) < 0.000001f) {
        if (ta->type == LT_SIDE && tb->type == LT_SIDE) {
            // bias towards whichever side faces movement direction more coming
            // earlier in list
            const v2
                move_dir = v2_dir(trace->org, trace->dst),
                n_a = side_normal(ta->side.ptr),
                n_b = side_normal(tb->side.ptr);

            // b_* is from 0..1 on how much move_dir and n_* are opposites
            const f32
                b_a = max(-v2_dot(n_a, move_dir), 0.0f),
                b_b = max(-v2_dot(n_b, move_dir), 0.0f);

            return -sign(b_a - b_b);
        }

        if (ta->type == LT_DECAL && tb->type == LT_DECAL) {
            return 0;
        } else if (ta->type == LT_DECAL && tb->type != LT_DECAL) {
            // a first
            return -1;
        } else if (ta->type != LT_DECAL && tb->type == LT_DECAL) {
            // b first
            return 1;
        } else {
            // doesn't matter
        }
    }

    return sign(diff);
}

typedef struct {
    // trace data
    trace_2d_t *trace;

    // result of traverse
    trace_result_e result;

    // true if resolve wants to retry movement
    bool retry;
} trace_2d_traverse_userdata_t;

static bool trace_2d_traverse(
        level_t *level,
        block_t *block,
        v2i block_pos,
        void *userdata_) {
    trace_2d_traverse_userdata_t *userdata = userdata_;
    trace_2d_t *trace = userdata->trace;
    DYNLIST(trace_hit_t) hits = dynlist_create(trace_hit_t, &g->frame_arena);

    const v2
        delta = v2_sub(trace->dst, trace->org),
        dir = v2_normalize(delta);

    if (!(trace->types & (LTF_SIDE | LTF_WALL | LTF_DECAL))) {
        goto add_sectors;
    }

    // add walls/sides hit
    dynlist_each(block->walls, it) {
        wall_t *wall = *it.el;

        // if dot(dir, wall normal) is negative, then this is the right side
        // (wall normal side) since direction and wall normal are facing each
        // other. otherwise we're on the other side.
        side_t *side =
            wall->sides[sign(v2_dot(dir, wall->normal)) <= 0.0f ? 0 : 1];

        if (!side || !side->sector) {
            // no side to collide with
            continue;
        }

        // can we collide with this side?
        if (v2_dot(dir, side_normal(side)) > 0.0f) {
            // no collision
            continue;
        }

        if (trace->filter_fn
            && !trace->filter_fn(
                level,
                lptr_from(side),
                trace->filter_userdata)) {
            continue;
        }

        vertex_t *vs[2];
        side_get_vertices(side, vs);
        const v2 a = vs[0]->pos, b = vs[1]->pos;

        f32 t;
        v2 resolved, hit;

        if (trace->radius == 0.0f) {
            // 0 radius: intersect as point moving along line ("swept" point)
            if (!intersect_segs(
                    trace->org,
                    trace->dst,
                    a,
                    b,
                    &hit,
                    &t,
                    NULL)) {
                continue;
            }

            resolved = hit;
        } else {
            f32 t_circle, t_segment;
            if (!sweep_circle_line_segment(
                    trace->org,
                    trace->radius,
                    delta,
                    a,
                    b,
                    &t_circle,
                    &t_segment,
                    &resolved)) {
                continue;
            }

            hit = v2_lerp(a, b, t_segment);
            t = t_circle;

            if (t == 0.0f) {
                // helps un-stick sometimes
                resolved =
                    v2_add(
                        resolved,
                        v2_scale(side_normal(side), 0.001f));
            }
        }

        const f32 hit_x = v2_distance(hit, vs[0]->pos);

        if (trace->types & (LTF_SIDE | LTF_WALL)) {
            *dynlist_push(hits) = (trace_hit_t) {
                .swept_pos = resolved,
                .t = t,
                .type = LT_SIDE,
                .ptr = lptr_from(side),
                .block = block_pos,
                .side = {
                    .hit_pos = hit,
                    .u = hit_x / wall->len,
                    .x = hit_x,
                    .wall = wall,
                    .ptr = side,
                },
            };
        }

        if (trace->types & LTF_DECAL) {
            // check affected decals
            llist_each(node, &side->decals, it) {
                const box2f_t bounds = decal_bounds(level, it.el);

                if (hit_x < bounds.min.x || hit_x > bounds.max.x) {
                    continue;
                }

                if (trace->filter_fn
                    && !trace->filter_fn(
                        level,
                        lptr_from(it.el),
                        trace->filter_userdata)) {
                    continue;
                }

                *dynlist_push(hits) = (trace_hit_t) {
                    .swept_pos = resolved,
                    .type = LT_DECAL,
                    .t = t,
                    .ptr = lptr_from(it.el),
                    .block = block_pos,
                    .decal = {
                        .hit_pos = hit,
                        .ptr = it.el
                    },
                };
            }
        }
    }

add_sectors:
    if (!((trace->flags & TRACE_FLAG_INCLUDE_TRAVERSE_SECTORS)
            && (trace->types & LTF_SECTOR))) {
        goto add_entities;
    }

    // check if line crosses any sectors, add them to hit check
    dynlist_each(block->sectors, it) {
        if (trace->filter_fn
            && !trace->filter_fn(
                level,
                lptr_from(*it.el),
                trace->filter_userdata)) {
            continue;
        }

        if (!sector_intersects_line(*it.el, trace->org, trace->dst)) {
            continue;
        }

        *dynlist_push(hits) = (trace_hit_t) {
            .swept_pos = trace->org,
            .t = 1.0f,
            .type = LT_SECTOR,
            .ptr = lptr_from(*it.el),
            .block = block_pos,
            .sector = {
                .ptr = *it.el,
                .plane = PLANE_TYPE_FLOOR, // meaningless
                .traverse_only = true, // only came from traversal
            },
        };
    }

add_entities:
    if (!(trace->types & LTF_ENTITY)) {
        goto done_adding;
    }

    dynlist_each(block->entities, it) {
        entity_t *ent = *it.el;

        if (ent->destroy || ent->corpse) {
            continue;
        }

        if (trace->filter_fn
            && !trace->filter_fn(
                level,
                lptr_from(*it.el),
                trace->filter_userdata)) {
            continue;
        }

        const entity_bounds_t bounds = entity_bounds(level, ent);

        // collide with bounds origin, not ent->pos
        const v2 bounds_pos = v2_from(bounds.origin);

        f32 t = 0.0f, t_exit = -1.0f;
        v2 resolved = v2_of(0);

        // check hitbox first, if requested
        if (trace->flags & TRACE_FLAG_ENTITY_HITBOXES) {
            bool did_hitbox_hit = true;

            const f32 hitbox_radius =
                bounds.hitbox_radius > 0.0f ?
                    bounds.hitbox_radius
                    : bounds.radius;

            if (trace->radius == 0.0f) {
                f32 ts[2];
                const int n_hits =
                    intersect_circle_seg(
                        bounds_pos,
                        hitbox_radius,
                        trace->org,
                        trace->dst,
                        ts,
                        NULL);

                if (n_hits == 0) {
                    did_hitbox_hit = false;
                }

                t = ts[0];
                t_exit = ts[1];
                resolved = v2_lerp(trace->org, trace->dst, t);
            } else {
                if (!sweep_circle_circle(
                        bounds_pos,
                        hitbox_radius,
                        trace->org,
                        trace->radius,
                        delta,
                        &t,
                        &resolved)) {
                    // no hit
                    did_hitbox_hit = false;
                }
            }

            if (did_hitbox_hit) {
                // add hitbox hit
                *dynlist_push(hits) = (trace_hit_t) {
                    .swept_pos = resolved,
                    .t = t,
                    .type = LT_ENTITY,
                    .ptr = lptr_from(ent),
                    .block = block_pos,
                    .entity = {
                        .ptr = ent,
                        .t_exit = t_exit,
                        .is_hitbox = true,
                        .is_collision = false,
                    },
                };
            }
        }

        // check regular collision
        if (trace->radius == 0.0f) {
            f32 ts[2];
            const int n_hits =
                intersect_circle_seg(
                    bounds_pos,
                    bounds.radius,
                    trace->org,
                    trace->dst,
                    ts,
                    NULL);

            if (n_hits == 0) {
                continue;
            }

            t = ts[0];
            t_exit = ts[1];
            resolved = v2_lerp(trace->org, trace->dst, t);
        } else {
            if (!sweep_circle_circle(
                    bounds_pos,
                    bounds.radius,
                    trace->org,
                    trace->radius,
                    delta,
                    &t,
                    &resolved)) {
                // no hit
                continue;
            }
        }

        // add regular hit
        *dynlist_push(hits) = (trace_hit_t) {
            .swept_pos = resolved,
            .t = t,
            .type = LT_ENTITY,
            .ptr = lptr_from(ent),
            .block = block_pos,
            .entity = {
                .ptr = ent,
                .t_exit = t_exit,
                .is_hitbox = false,
                .is_collision = true,
            },
        };
    }

done_adding:
    // sort hits by t
    dynlist_sort(hits, trace_hit_cmp, trace);

    // resolve
    dynlist_each(hits, it) {
        switch (trace->resolve_fn(level, trace, it.el)) {
        case TRACE_RESOLVE_STOP:
            // we were stopped entirely, don't traverse more blocks
            userdata->result = TRACE_RESULT_STOPPED;
            return false;
        case TRACE_RESOLVE_CONTINUE:
            // keep going, nothing stopped us
            userdata->result = TRACE_RESULT_NOT_STOPPED;
            break;
        case TRACE_RESOLVE_RETRY:
            // retry entire trace, don't traverse more blocks
            userdata->result = TRACE_RESULT_STOPPED;
            userdata->retry = true;
            return false;
        }
    }

    return true;
}

trace_result_e trace_2d(level_t *level, trace_2d_t *trace) {
    // total times movement was attempted, fails on >TRACE_MAX_RETRIES
    int n_attempts = 0;

    ASSERT(trace->resolve_fn, "trace_2d requires resolve_fn");

retry:;
    const v2 delta = v2_sub(trace->dst, trace->org);
    const f32 len = v2_norm(delta);
    const v2 dir = v2_normalize(delta);

    // check for forcing near portals: just immediately check all potential
    // nearby walls to see if we need to portal
    if (trace->flags & TRACE_FLAG_FORCE_PORTALS_NEAR_TO_CAM) {
        // use distance to center of near plane as reference
        v4 near_plane_center =
            m4_mulv(
                g->cam.inv_view_proj,
                v4_of(0.5f, 0.5f, 0.0f, 1.0f));
        near_plane_center.x /= near_plane_center.w;
        near_plane_center.y /= near_plane_center.w;
        near_plane_center.z /= near_plane_center.w;
        const v2 near_pos = v2_of(near_plane_center.x, near_plane_center.y);

        // get nearby walls
        DYNLIST(wall_t*) near_walls = dynlist_create(wall_t*, &g->frame_arena);
        level_walls_in_radius(
            level,
            trace->org,
            v2_norm(delta) + NEAR_WALL_THRESHOLD,
            &near_walls);

        // nearby/potential sides
        DYNLIST(trace_hit_t) hit_sides =
            dynlist_create(trace_hit_t, &g->frame_arena);

        dynlist_each(near_walls, it) {
            // get facing side
            side_t *s = NULL;
            for (int i = 0; i < 2; i++) {
                if (!(*it.el)->sides[i]) { continue; }
                side_t *side = (*it.el)->sides[i];

                // only disconnected portals
                if (!side->is_disconnect) { continue; }

                // must be facing each other (negative dot since this is
                // opposite side)
                if (v2_dot(dir, side_normal(side)) < 0.0f) {
                    s = side;
                    break;
                }
            }

            if (!s || !s->sector) { continue; }

            vertex_t *vs_in[2], *vs_out[2];
            side_get_vertices(s, vs_in);
            side_get_vertices(s->portal, vs_out);

            const f32 dist =
                point_to_segment(near_pos, vs_in[0]->pos, vs_in[1]->pos);

            if (dist > FORCE_PORTAL_DISTANCE) {
                continue;
            }

            v2 hit;
            if (!intersect_lines(
                    trace->org,
                    trace->dst,
                    vs_in[0]->pos,
                    vs_in[1]->pos,
                    &hit)) {
                WARN("what's going on");
                continue;
            }

            // "u" on side of hit
            const f32
                x_hit = v2_distance(hit, vs_in[0]->pos),
                u_hit = x_hit / s->wall->len;

            // insert and sort by t
            dynlist_insert_sorted(
                hit_sides,
                trace_hit_cmp,
                trace,
                &((trace_hit_t) {
                    .swept_pos = trace->org,
                    .t = v2_distance(trace->org, hit) / len,
                    .type = LT_SIDE,
                    .ptr = lptr_from(s),
                    .block = level_pos_to_block(trace->org),
                    .side = {
                        .ptr = s,
                        .hit_pos = hit,
                        .u = u_hit,
                        .x = x_hit,
                        .wall = s->wall,
                        .is_force_portal = true,
                    },
                }));
        }

        // traverse hit sides (already sorted by t)
        dynlist_each(hit_sides, it) {
            switch (trace->resolve_fn(level, trace, it.el)) {
            case TRACE_RESOLVE_STOP:
                return TRACE_RESULT_STOPPED;
            case TRACE_RESOLVE_RETRY:
                goto retry;
            case TRACE_RESOLVE_CONTINUE:
            }
        }
    }

    while (n_attempts < TRACE_MAX_RETRIES) {
        n_attempts++;

        // traverse inclusive radius so that things touched by radius but not by
        // movement vector are also processed
        const v2
            a = v2_add(trace->org, v2_scale(dir, -trace->radius)),
            b = v2_add(trace->dst, v2_scale(dir, +trace->radius));

        trace_2d_traverse_userdata_t userdata = {
            .trace = trace,
            .result = TRACE_RESULT_NOT_STOPPED,
            .retry = false,
        };

        level_traverse_blocks(
            level,
            a,
            b,
            trace_2d_traverse,
            &userdata,
            LTB_NONE);

        switch (userdata.result) {
        case TRACE_RESULT_NOT_STOPPED:
        case TRACE_RESULT_STOPPED:
            if (userdata.retry) {
                // new movement attempt
                goto retry;
            }

            // entire trace completed, this is the result
            return userdata.result;
        case TRACE_RESULT_TOO_MANY_RETRIES:
            ASSERT(false); // should be unreachable
        }
    }

    // nothing got STOPPED or NOT_STOPPED, there were too many attempts
    return TRACE_RESULT_TOO_MANY_RETRIES;
}

typedef struct {
    trace_hit_t hit;
    v2 hit_pos;
    trace_2d_seg_t *trace_seg;
} trace_2d_seg_userdata_t;

static trace_resolve_result_e trace_2d_seg_resolve(
        level_t *level,
        trace_2d_t *trace_2d,
        const trace_hit_t *hit) {
    trace_2d_seg_userdata_t *userdata = trace_2d->userdata;
    trace_2d_seg_t *trace_seg = userdata->trace_seg;

    if (lptr_eq(hit->ptr, trace_seg->ignore_ptr)) {
        return TRACE_RESOLVE_CONTINUE;
    }

    // check resolve function first, if present
    if (trace_seg->resolve_fn) {
        const trace_resolve_result_e res_resolve =
            trace_seg->resolve_fn(level, trace_seg, hit);

        if (res_resolve != TRACE_RESOLVE_CONTINUE) {
            userdata->hit = *hit;
            userdata->hit_pos = hit->swept_pos;
            return res_resolve;
        }
    }

    // check portal
    f32 angle;
    const trace_portal_result_e res_portal =
        trace_resolve_portal_2d(
            level,
            trace_2d,
            hit,
            &angle);

    switch (res_portal) {
    case TRACE_PORTAL_RESULT_THROUGH:
    case TRACE_PORTAL_RESULT_REJECT:
        // keep going
        return TRACE_RESOLVE_CONTINUE;
    case TRACE_PORTAL_RESULT_THROUGH_DISCONNECT:
        // moved through, need to retry movement from new location
        trace_seg->org = trace_2d->org;
        trace_seg->dst = trace_2d->dst;
        return TRACE_RESOLVE_RETRY;
    case TRACE_PORTAL_RESULT_IGNORE:
    case TRACE_PORTAL_RESULT_STOP:
        // either stopped by/around portal or hit something else
        userdata->hit = *hit;
        userdata->hit_pos = hit->swept_pos;
        return TRACE_RESOLVE_STOP;
    }

    unreachable();
}

trace_2d_seg_result_t trace_2d_seg(level_t *level, trace_2d_seg_t *trace) {
    trace_2d_seg_userdata_t userdata = {
        .trace_seg = trace,
    };

    trace_2d_t trace_2d_data = {
        .org = trace->org,
        .dst = trace->dst,
        .radius = trace->radius,
        .types = trace->types,
        .flags = trace->flags,
        .resolve_fn = trace_2d_seg_resolve,
        .filter_fn = trace->filter_fn,
        .filter_userdata = trace->filter_userdata,
        .userdata = &userdata,
    };

    const trace_result_e res = trace_2d(level, &trace_2d_data);
    trace->org = trace_2d_data.org;
    trace->dst = trace_2d_data.dst;
    return (trace_2d_seg_result_t) {
        .result = res,
        .hit = userdata.hit,
        .hit_pos = userdata.hit_pos,
    };
}

static void compute_3d_hit_normal(
        trace_3d_t *trace_3d,
        trace_hit_t *hit) {
    switch (hit->type) {
    case LT_SIDE:
        hit->normal = v3_of(side_normal(hit->side.ptr), 0.0f);
        break;
    case LT_SECTOR:
        hit->normal =
            sector_plane_normal(
                hit->sector.ptr,
                hit->sector.plane);
        break;
    case LT_DECAL:
        if (hit->decal.ptr->is_on_side) {
            hit->normal =
                v3_of(side_normal(hit->decal.ptr->side.ptr), 0.0f);
        } else {
            hit->normal =
                sector_plane_normal(
                    hit->decal.ptr->sector.ptr,
                    hit->decal.ptr->sector.plane);
        }
        break;
    case LT_ENTITY:
        hit->normal = v3_scale(v3_dir(trace_3d->org, trace_3d->dst), -1);
        break;
    default: ASSERT(false, "unexpected hit type");
    }
}

// 2D resolve function for 3D trace
static trace_resolve_result_e trace_3d_resolve(
        level_t *level,
        trace_2d_t *trace_2d,
        const trace_hit_t *hit) {
    trace_resolve_result_e res = TRACE_RESOLVE_CONTINUE;
    trace_3d_t *trace_3d = trace_2d->userdata;

    // list of hits
    DYNLIST(trace_hit_t) hits = dynlist_create(trace_hit_t, &g->frame_arena);

    sector_t *sector = NULL;

    if (hit->type == LT_ENTITY) {
        sector = hit->entity.ptr->sector;

        const entity_bounds_t bounds = entity_bounds(level, hit->entity.ptr);
        const v3 bounds_pos = bounds.origin;
        const f32 height =
            (trace_3d->flags & TRACE_FLAG_ENTITY_HITBOXES)
            && bounds.hitbox_height > 0.0f ?
                bounds.hitbox_height
                : bounds.height;

        const rangef_t ent_z_range = {
            .z0 = bounds_pos.z,
            .z1 = bounds_pos.z + height,
        };

        const v3
            swept_pos_3d_enter =
                v3_lerp(trace_3d->org, trace_3d->dst, hit->t),
            swept_pos_3d_exit =
                v3_lerp(trace_3d->org, trace_3d->dst, hit->entity.t_exit);

        if (trace_3d->height == 0.0f) {
            const rangef_t trace_z_range =
                rangef_from(swept_pos_3d_enter.z, swept_pos_3d_exit.z);

            // ray vs. cylinder
            // check entire z range, it is possible that we don't intersect
            // directly on enter but z may intersect between ray entry/exit
            if (rangef_overlap(ent_z_range, trace_z_range, NULL)) {
                *dynlist_push(hits) = *hit;
            }
        } else {
            // hit if z ranges overlap
            if (rangef_overlap(
                    ent_z_range,
                    (rangef_t) {
                        .z0 = swept_pos_3d_enter.z,
                        .z1 = swept_pos_3d_enter.z + trace_3d->height,
                    },
                    NULL)) {
                *dynlist_push(hits) = *hit;
            }
        }
    } else if (hit->type == LT_DECAL) {
        // should only be hitting side decals
        // don't worry about adding sectors, the side hit will add the sector
        // hit anyway
        ASSERT(hit->decal.ptr->is_on_side);
        *dynlist_push(hits) = *hit;
    } else if (hit->type == LT_SECTOR) {
        // should have no "real" hits
        ASSERT(hit->sector.traverse_only);
        ASSERT(trace_2d->flags & TRACE_FLAG_INCLUDE_TRAVERSE_SECTORS);
        sector = hit->sector.ptr;
    } else if (hit->type == LT_SIDE) {
        // add regular side hit to list
        *dynlist_push(hits) = *hit;
        sector = hit->side.ptr->sector;
    } else {
        // unrecognized hit type?
        ASSERT(false);
    }

    // check if our 3d line segment intersects either plane if there's a sector
    // to check
    // skip this check if TRACE_FLAG_XY_THEN_Z, in that case the Z movement is
    // checked/resolved later on
    if (sector
        && !(trace_3d->flags & TRACE_FLAG_XY_THEN_Z)
        && (!trace_3d->filter_fn
            || trace_3d->filter_fn(
                level,
                lptr_from(sector),
                trace_3d->userdata))) {
        for (plane_type_e plane = PLANE_TYPE_FIRST;
             plane <= PLANE_TYPE_LAST;
             plane++) {
            const v4 p = sector_plane_vec(sector, plane);

            int n_lines = 0;
            line3f_t lines[2];

            lines[n_lines++] = (line3f_t) {
                .a =trace_3d->org,
                .b = trace_3d->dst,
            };

            // if we have height, check the top line as well
            if (trace_3d->height != 0.0f) {
                lines[n_lines++] = (line3f_t) {
                    .a = v3_add(trace_3d->org, v3_of(0, 0, trace_3d->height)),
                    .b = v3_add(trace_3d->dst, v3_of(0, 0, trace_3d->height)),
                };
            }

            v3 plane_hit = v3_of(0);
            f32 hit_t;
            for (int i = 0; i < n_lines; i++) {
                if (!intersect_seg_plane(
                        lines[i].a,
                        lines[i].b,
                        p,
                        &plane_hit,
                        &hit_t)) {
                    // no hit
                    continue;
                }

                if (!sector_contains_point(sector, v2_from(plane_hit))) {
                    // not in sector
                    continue;
                }

                const v2i block_pos = level_pos_to_block(v2_from(plane_hit));
                if (!v2i_eqv(block_pos, hit->block)) {
                    // not in the same block
                    continue;
                }

                // sector hit
                const trace_hit_t sector_hit = {
                    .swept_pos =
                        v2_lerp(
                            v2_from(trace_3d->org),
                            v2_from(trace_3d->dst),
                            hit_t),
                    .t = hit_t,
                    .type = LT_SECTOR,
                    .block = block_pos,
                    .ptr = lptr_from(sector),
                    .sector = {
                        .ptr = sector,
                        .plane = plane,
                        .traverse_only = false,
                    },
                };

                *dynlist_push(hits) = sector_hit;

                if (!(trace_3d->types & LTF_DECAL)) {
                    continue;
                }

                // add decals
                llist_each(node, &sector->decals, it) {
                    if (it.el->sector.plane != plane) {
                        continue;
                    }

                    const box2f_t bounds = decal_bounds(level, it.el);
                    if (!box2f_contains(bounds, sector_hit.swept_pos)) {
                        continue;
                    }

                    if (trace_3d->filter_fn
                        && !trace_3d->filter_fn(
                            level,
                            lptr_from(it.el),
                            trace_3d->filter_userdata)) {
                        continue;
                    }

                    *dynlist_push(hits) = (trace_hit_t) {
                        .swept_pos = sector_hit.swept_pos,
                        .t = sector_hit.t,
                        .type = LT_DECAL,
                        .ptr = lptr_from(it.el),
                        .block = block_pos,
                        .decal = {
                            .ptr = it.el,
                            .hit_pos = sector_hit.swept_pos,
                        },
                    };
                }
            }
        }
    }

    // sort hits and resolve in order
    dynlist_sort(hits, trace_hit_cmp, &trace_3d->trace_2d);

    dynlist_each(hits, it) {
        compute_3d_hit_normal(trace_3d, it.el);
        res = trace_3d->resolve_fn(level, trace_3d, it.el);

        switch (res) {
        case TRACE_RESOLVE_CONTINUE:
            // keep going
            break;
        case TRACE_RESOLVE_STOP:
        case TRACE_RESOLVE_RETRY:
            trace_3d->last_hit = *hit;
            // stop!
            goto done;
        }
    }

done:
    // copy what may have been updated in the 3D resolve to 2D
    trace_3d->trace_2d.org = v2_from(trace_3d->org);
    trace_3d->trace_2d.dst = v2_from(trace_3d->dst);
    return res;
}

trace_result_e trace_3d(level_t *level, trace_3d_t *trace) {
    int n_attempts = 0;

    while (n_attempts < TRACE_MAX_RETRIES) {
        n_attempts++;

        int flags_2d = trace->flags;

        if ((trace->types & LTF_SECTOR)
            && !(trace->flags & TRACE_FLAG_XY_THEN_Z)) {
            // if moving on all 3 axes at once, then include traverse sectors
            // for processing at the appropriate ts
            flags_2d |= TRACE_FLAG_INCLUDE_TRAVERSE_SECTORS;
        }

        trace->trace_2d = (trace_2d_t) {
            .org = v2_from(trace->org),
            .dst = v2_from(trace->dst),
            .radius = trace->radius,
            .types = trace->types,
            .flags = flags_2d,
            .resolve_fn = trace_3d_resolve,
            .filter_fn = trace->filter_fn,
            .filter_userdata = trace->filter_userdata,
            .userdata = trace,
        };

        trace_result_e res_2d;
        switch ((res_2d = trace_2d(level, &trace->trace_2d))) {
        case TRACE_RESULT_STOPPED:
        case TRACE_RESULT_TOO_MANY_RETRIES:
            return res_2d;
        case TRACE_RESULT_NOT_STOPPED:
            if (!(trace->types & LTF_SECTOR)
                || !(trace->flags & TRACE_FLAG_XY_THEN_Z)) {
                return res_2d;
            }

            // check z movement...
            break;
        }

        // only trace on z
        trace->org = v3_of(trace->dst.x, trace->dst.y, trace->org.z);
        trace->dst = v3_of(trace->dst.x, trace->dst.y, trace->dst.z);

        ASSERT(trace->flags & TRACE_FLAG_XY_THEN_Z);

        sector_t *sector =
            level_find_point_sector(level, v2_from(trace->dst), NULL);

        if (!sector) {
            //WARN(
            //    "3D destination (%" PRIv3 " -> %" PRIv3 ") out of sector",
            //    FMTv3(trace->org),
            //    FMTv3(trace->dst));
            return TRACE_RESULT_STOPPED;
        }

        const rangef_t zs = sector_point_zs(sector, v2_from(trace->dst));

        int n_hits = 0;
        trace_hit_t hits[2];

        if (trace->dst.z <= zs.z0) {
            const f32 t = lerp_solve_for_t(trace->org.z, trace->dst.z, zs.z0);
            const v2 pos = v2_lerp(v2_from(trace->org), v2_from(trace->dst), t);
            hits[n_hits++] = (trace_hit_t) {
                .swept_pos = pos,
                .t = t,
                .type = LT_SECTOR,
                .ptr = lptr_from(sector),
                .block = level_pos_to_block(pos),
                .sector = {
                    .ptr = sector,
                    .plane = PLANE_TYPE_FLOOR,
                    .traverse_only = false,
                },
                .normal = sector_plane_normal(sector, PLANE_TYPE_FLOOR),
            };
        }

        if (trace->dst.z + trace->height >= zs.z1) {
            f32 t =
                lerp_solve_for_t(
                    trace->org.z,
                    trace->dst.z,
                    zs.z1 - trace->height);
            t = satf(t);

            const v2 pos = v2_lerp(v2_from(trace->org), v2_from(trace->dst), t);
            hits[n_hits++] = (trace_hit_t) {
                .swept_pos = pos,
                .t = t,
                .type = LT_SECTOR,
                .ptr = lptr_from(sector),
                .block = level_pos_to_block(pos),
                .sector = {
                    .ptr = sector,
                    .plane = PLANE_TYPE_CEIL,
                    .traverse_only = false,
                },
                .normal = sector_plane_normal(sector, PLANE_TYPE_CEIL),
            };
        }

        if (trace->filter_fn
            && !trace->filter_fn(
                level,
                lptr_from(sector),
                trace->filter_userdata)) {
            n_hits = 0;
        }

        bool retry = false;
        for (int i = 0; i < n_hits; i++) {
            switch (trace->resolve_fn(level, trace, &hits[i])) {
            case TRACE_RESOLVE_STOP:
                trace->last_hit = hits[i];
                return TRACE_RESULT_STOPPED;
            case TRACE_RESOLVE_RETRY:
                trace->last_hit = hits[i];
                // no need to update 2D trace on retry since it is entirely
                // recomputed at function start
                retry = true;
                break;
            case TRACE_RESOLVE_CONTINUE:
                // check next hit if present
                break;
            }

            if (retry) {
                break;
            }
        }

        if (!retry) {
            // passed sector plane check
            return TRACE_RESULT_NOT_STOPPED;
        }

        // retry...
    }

    return TRACE_RESULT_TOO_MANY_RETRIES;
}

typedef struct {
    trace_hit_t hit;
    v3 hit_pos;
    trace_3d_seg_t *trace_seg;
} trace_3d_seg_userdata_t;

static trace_resolve_result_e trace_3d_seg_resolve(
    level_t *level,
    trace_3d_t *trace_3d,
    const trace_hit_t *hit) {
    trace_3d_seg_userdata_t *userdata = trace_3d->userdata;
    trace_3d_seg_t *trace_seg = userdata->trace_seg;

    if (lptr_eq(hit->ptr, trace_seg->ignore_ptr)) {
        return TRACE_RESOLVE_CONTINUE;
    }

    // check resolve function first, if present
    if (trace_seg->resolve_fn) {
        const trace_resolve_result_e res_resolve =
            trace_seg->resolve_fn(level, trace_seg, hit);

        if (res_resolve != TRACE_RESOLVE_CONTINUE) {
            userdata->hit = *hit;
            userdata->hit_pos =
                v3_of(
                    hit->swept_pos,
                    lerp(trace_3d->org.z, trace_3d->dst.z, hit->t));
            return res_resolve;
        }
    }

    // check portal
    const trace_portal_result_e res_portal =
        trace_resolve_portal_3d(
            level,
            trace_3d,
            hit,
            NULL);

    switch (res_portal) {
    case TRACE_PORTAL_RESULT_THROUGH:
    case TRACE_PORTAL_RESULT_REJECT:
        // keep going
        return TRACE_RESOLVE_CONTINUE;
    case TRACE_PORTAL_RESULT_THROUGH_DISCONNECT:
        // moved through, need to retry movement from new location
        trace_seg->org = trace_3d->org;
        trace_seg->dst = trace_3d->dst;
        return TRACE_RESOLVE_RETRY;
    case TRACE_PORTAL_RESULT_IGNORE:
    case TRACE_PORTAL_RESULT_STOP:
        // either stopped by/around portal or hit something else
        userdata->hit = *hit;
        userdata->hit_pos =
            v3_of(
                hit->swept_pos,
                lerp(trace_seg->org.z, trace_seg->dst.z, hit->t));
        return TRACE_RESOLVE_STOP;
    }

    unreachable();
}

trace_3d_seg_result_t trace_3d_seg(level_t *level, trace_3d_seg_t *trace) {
    trace_3d_seg_userdata_t userdata = {
        .trace_seg = trace,
    };

    trace_3d_t trace_3d_data = {
        .org = trace->org,
        .dst = trace->dst,
        .radius = 0.0f,
        .height = 0.0f,
        .step_height = 0.0f,
        .types = trace->types,
        .flags = trace->flags,
        .resolve_fn = trace_3d_seg_resolve,
        .filter_fn = trace->filter_fn,
        .filter_userdata = trace->filter_userdata,
        .userdata = &userdata,
    };

    const trace_result_e trace_result = trace_3d(level, &trace_3d_data);
    trace->org = trace_3d_data.org;
    trace->dst = trace_3d_data.dst;

    return (trace_3d_seg_result_t) {
        .result = trace_result,
        .hit = userdata.hit,
        .hit_pos = userdata.hit_pos,
    };
}

// move a line segment + angle through a 2D portal without modifying the
// original trace
static trace_portal_result_e trace_move_portal_2d(
        level_t *level,
        const trace_2d_t *trace,
        const trace_hit_t *hit,
        v2 *org,
        v2 *dst,
        f32 *angle) {
    if (hit->type != LT_SIDE || !hit->side.ptr->portal) {
        // not a portal
        return TRACE_PORTAL_RESULT_IGNORE;
    } 

    // check that the ray actually passes the portal and matches the direction
    // of the side
    if (!hit->side.is_force_portal) {
        if (!intersect_segs(
                *org,
                *dst,
                hit->side.ptr->wall->v0->pos,
                hit->side.ptr->wall->v1->pos,
                NULL, NULL, NULL)) {
            return TRACE_PORTAL_RESULT_REJECT;
        }

        const v2 dir = v2_dir(*org, *dst);
        if (v2_dot(dir, side_normal(hit->side.ptr)) > 0.0f) {
            // cannot hit each other, normals point same direction
            return TRACE_PORTAL_RESULT_REJECT;
        }
    }

    if (!hit->side.ptr->is_disconnect) {
        // resolve as normal portal
        *angle = 0.0f;
        return TRACE_PORTAL_RESULT_THROUGH;
    }

    // moving through disconnected portal

    // check if explicitly asked to stop
    if (trace->flags & TRACE_FLAG_DISCONNECTED_PORTALS_STOP) {
        return TRACE_PORTAL_RESULT_STOP;
    }

    side_t
        *entry = hit->side.ptr,
        *exit = hit->side.ptr->portal;

    vertex_t *vs_entry[2], *vs_exit[2];
    side_get_vertices(entry, vs_entry);
    side_get_vertices(exit, vs_exit);

    const f32
        u_entry = hit->side.u,
        u_exit = 1.0f - u_entry;

    if (angle) {
        *angle = portal_angle(level, entry, exit);
    }

    v2
        nomal_exit = side_normal(exit),
        new_org = v2_lerp(vs_exit[0]->pos, vs_exit[1]->pos, u_exit),
        new_dst = portal_transform(level, entry, exit, trace->dst);

    // move to through portal
    if ((trace->flags & TRACE_FLAG_FORCE_PORTAL_AWAY)) {
        new_org = v2_add(new_org, v2_scale(nomal_exit, FORCE_PORTAL_DISTANCE));
        new_dst = v2_add(new_dst, v2_scale(nomal_exit, FORCE_PORTAL_DISTANCE));
    }

    *org = new_org;
    *dst = new_dst;
    return TRACE_PORTAL_RESULT_THROUGH_DISCONNECT;
}

trace_portal_result_e trace_resolve_portal_2d(
        level_t *level,
        trace_2d_t *trace,
        const trace_hit_t *hit,
        f32 *angle_out) {
    v2 org = trace->org, dst = trace->dst;
    const trace_portal_result_e res =
        trace_move_portal_2d(level, trace, hit, &org, &dst, angle_out);

    trace->org = org;
    trace->dst = dst;
    return res;
}

trace_portal_result_e trace_resolve_portal_3d(
        level_t *level,
        trace_3d_t *trace,
        const trace_hit_t *hit,
        f32 *angle_out) {
    if (hit->type != LT_SIDE || !hit->side.ptr->portal) {
        // not a portal
        return TRACE_PORTAL_RESULT_IGNORE;
    }

    const side_segments_t segs = side_get_segments(level, hit->side.ptr);

    if (!segs.middle.present) {
        // no middle segment to move through
        return TRACE_PORTAL_RESULT_STOP;
    }

    // check zs at hit point
    const rangef_t middle_zs = side_segment_zs_at(&segs.middle, hit->side.u);

    // check if height can fit through zs
    if (trace->height > (middle_zs.z1 - middle_zs.z0)) {
        // can't fit
        return TRACE_PORTAL_RESULT_STOP;
    }

    // z at time of hit t
    const f32 z_t = lerp(trace->org.z, trace->dst.z, hit->t);

    if (trace->step_height == 0.0f || z_t >= middle_zs.z0) {
        // no step height OR we've already cleared the step
        // check if clean move is ok
        if (trace->height == 0.0f) {
            // check if single point goes through
            if (!rangef_contains(middle_zs, z_t)) {
                return TRACE_PORTAL_RESULT_STOP;
            }
        } else {
            // check if both points go through
            if (!rangef_contains(middle_zs, z_t + 0.0f)
                || !rangef_contains(middle_zs, z_t + trace->height)) {
                return TRACE_PORTAL_RESULT_STOP;
            }
        }
    } else {
        // need to step
        ASSERT(trace->step_height != 0.0f);
        ASSERT(z_t < middle_zs.z0);

        if (middle_zs.z0 - z_t > trace->step_height) {
            // can't step, difference too large
            return TRACE_PORTAL_RESULT_STOP;
        }

        // check if top can go through after step
        const f32 z_t_top_step = middle_zs.z0 + trace->height;
        if (!rangef_contains(middle_zs, z_t_top_step)) {
            return TRACE_PORTAL_RESULT_STOP;
        }
    }

    // test move to the desintation...
    v2 org = trace->trace_2d.org, dst = trace->trace_2d.dst;

    f32 portal_angle;
    const trace_portal_result_e res =
        trace_move_portal_2d(
            level,
            &trace->trace_2d,
            hit,
            &org,
            &dst,
            &portal_angle);

    if (res == TRACE_PORTAL_RESULT_REJECT) {
        // allow movement but don't portal
        return TRACE_PORTAL_RESULT_REJECT;
    }

    v3 org_3d = v3_of(org, trace->org.z), dst_3d = v3_of(dst, trace->dst.z);

    if (res == TRACE_PORTAL_RESULT_THROUGH_DISCONNECT) {
        const f32
            z_floor = hit->side.ptr->sector->floor.z,
            nz_floor = hit->side.ptr->portal->sector->floor.z,
            z_diff = nz_floor - z_floor;

        // update zs according to portal difference
        org_3d.z += z_diff;
        dst_3d.z += z_diff;
    }

    // TODO: might bug if sector is too small on the other end - maybe consider
    // using level_point_zs?
    const rangef_t
        zs_org = sector_point_zs(hit->side.ptr->portal->sector, org),
        zs_dst = sector_point_zs(hit->side.ptr->portal->sector, dst);

    // can we fit in the moved origin?
    if (trace->height != 0.0f) {
        if (trace->height > zs_org.z1 - zs_org.z0) {
            return TRACE_PORTAL_RESULT_STOP;
        }

        // ok, can we fit into the new destination?
        if (trace->height > zs_dst.z1 - zs_dst.z0) {
            // move destination such that we fit: find t on org.z..dst.z where
            // where lerp(org.z, dst.z, t) + trace->height == zs_dst.z1
            if (fabsf(org_3d.z - dst_3d.z) < 0.000001f) {
                // not large enough to try
                return TRACE_PORTAL_RESULT_STOP;
            }

            // adjust
            f32 t =
                (zs_dst.z1 - trace->height - dst_3d.z) / (org_3d.z - dst_3d.z);
            // TODO
            // ASSERT(t >= 0.0f && t <= 1.0f);
            t = satf(t);
            dst_3d = v3_lerp(org_3d, dst_3d, t);
        }
    }

    // ok trace is good, move
    if (angle_out) {
        *angle_out = portal_angle;
    }

    trace->org = org_3d;
    trace->dst = dst_3d;
    trace->trace_2d.org = v2_from(trace->org);
    trace->trace_2d.dst = v2_from(trace->dst);
    return res;
}

move_project_result_t move_project_hit_velocity(
        const level_t *level,
        const trace_hit_t *hit,
        line2f_t movement,
        v2 velocity,
        v2 restitution,
        f32 dt) {
    v2 normal, tangent;

    if (hit->type == LT_SIDE) {
        vertex_t *vs[2];
        side_get_vertices(hit->side.ptr, vs);
        const line2f_t line = { .a = vs[0]->pos, .b = vs[1]->pos };
        normal = line_right_normal(line.a, line.b);
        if (v2_eqv_eps(normal, v2_of(0))) {
            normal = rand_v2_dir(&g->rand);
        }
        tangent = v2_dir(line.a, line.b);
    } else if (hit->type == LT_ENTITY) {
        const v3 other_center = entity_center(level, hit->entity.ptr);
        normal = v2_dir(v2_from(other_center), hit->swept_pos);
        if (v2_eqv_eps(normal, v2_of(0))) {
            normal = rand_v2_dir(&g->rand);
        }
        tangent = v2_rotate(normal, PI_2);
    } else {
        // no projection
        return (move_project_result_t) {
            .changed = false,
            .movement = movement,
            .velocity = velocity,
        };
    }

    // velocity gets + -velocity projected onto -normal * restitution
    const v2
        v_towards_wall = v2_proj(v2_normalize(velocity), normal),
        rest =
            v2_scale(
                v2_mul(
                    v_towards_wall,
                    restitution),
                -v2_norm(velocity));

    // project velocity onto tangent
    v2 projected =
        v2_add(
            v2_proj(velocity, tangent),
            v2_add(
                rest,
                v2_scale(normal, 0.005f)));

    move_project_result_t result = {
        .changed = true,
        .movement = movement,
        .velocity = projected,
    };

    if (hit->t <= 0.001f) {
        // go for immediate exit
        result.movement.a =
            v2_add(
                hit->swept_pos,
                v2_scale(normal, 0.0001f));
    }

    result.movement.b =
        v2_add(
            hit->swept_pos,
            v2_scale(projected, dt));

    ASSERT_DEBUG(v2_isvalid(result.velocity));

    return result;
}

bool path_subsectors_to_goal(
        level_t *level,
        v2 start,
        v2 goal,
        f32 (*cost_fn)(
            level_t*,
            const subsector_t*,
            const subsector_t*,
            const sect_line_t*,
            void*),
        DYNLIST(subsector_path_point_t) *out,
        void *userdata) {
    // set of subsector_t*
    map_t open_set;
    map_init(
        &open_set,
        &g->frame_arena,
        sizeof(subsector_t*),
        0,
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    // subsector_t* -> f32
    map_t g_score;
    map_init(
        &g_score,
        &g->frame_arena,
        sizeof(subsector_t*),
        sizeof(f32),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    // subsector_t* -> f32
    map_t f_score;
    map_init(
        &f_score,
        &g->frame_arena,
        sizeof(subsector_t*),
        sizeof(f32),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    subsector_t
        *start_sub = level_find_point_subsector(level, start, NULL),
        *goal_sub = level_find_point_subsector(level, goal, NULL);

    if (!start_sub) {
        WARN("could not find start_sub %" PRIv2, FMTv2(start));
        return false;
    }

    if (!goal_sub) {
        WARN("could not find goal_sub %" PRIv2, FMTv2(goal));
        return false;
    }

    start_sub->from = NULL;
    start_sub->via = NULL;
    goal_sub->from = NULL;
    goal_sub->via = NULL;

    map_insertk(&open_set, start_sub);
    map_insert(&g_score, start_sub, 0.0f);
    map_insert(&f_score, start_sub, 0.0f);

    bool success = false;

    int n = 0;
    while (!map_empty(&open_set)) {
        if (n >= TRACE_PATH_MAX_ITERS) {
            success = false;
            break;
        }

        n++;

        bool first = true;
        f32 current_fscore = 1e10f;
        subsector_t *current = NULL;

        map_each(subsector_t*, void, &open_set, it) {
            if (first) {
                current = *it.key;
                first = false;
            }

            f32 *slot = map_get(f32, &f_score, *it.key);
            const f32 node_fscore = slot ? *slot : 1e10f;

            if (node_fscore < current_fscore) {
                current = *it.key;
                current_fscore = node_fscore;
            }
        }

        if (current == goal_sub) {
            success = true;
            break;
        }

        map_try_remove(&open_set, current);

        dynlist_each(current->neighbors, it) {
            subsector_t *neighbor = it.el->sub;

            const f32 dist =
                subsector_distance_via(current, neighbor, it.el->line);

            f32 c;
            if (neighbor == goal_sub) {
                // zero cost to move to goal
                c = 0.0f;
            } else if (cost_fn) {
                c = cost_fn(level, current, neighbor, it.el->line, userdata);
            } else {
                // in lieu of a real function, use the distance
                c = dist;
            }

            if (c < 0.0f) {
                continue;
            }

            const f32 h = dist;

            f32 *slot_gscore_neighbor = map_get(f32, &g_score, neighbor);
            const f32
                gscore_tentative = *map_get(f32, &g_score, current) + c,
                gscore_neighbor =
                    slot_gscore_neighbor ? *slot_gscore_neighbor: 1e10f;

            if (gscore_tentative < gscore_neighbor) {
                neighbor->from = current;
                neighbor->via = it.el->line;
                map_insert(&g_score, neighbor, gscore_tentative);
                map_insert(&f_score, neighbor, gscore_tentative + h);

                if (!map_contains(&open_set, neighbor)) {
                    map_insertk(&open_set, neighbor);
                }
            }
        }
    }

    if (success) {
        dynlist_resize(*out, 0);
        *dynlist_insert(*out, 0) =
            (subsector_path_point_t) {
                .sub = goal_sub,
                .via = goal_sub->via,
            };

        // reconstruct path to goal until we hit start with ->from = NULL
        subsector_t *sub = goal_sub;
        while (sub && sub->via) {
            *dynlist_insert(*out, 0) =
                (subsector_path_point_t) {
                    .sub = sub,
                    .via = sub->via,
                };
            sub = sub->from;
        }

        *dynlist_insert(*out, 0) =
            (subsector_path_point_t) {
                .sub = start_sub,
                .via = NULL,
            };
    }

    return success;
}

static bool path_to_goal__default_trivial(
        level_t *level,
        const path_point_t *a,
        const path_point_t *b,
        const path_point_t *c,
        void *userdata) {
    // check if the points are directly visible in a 2D trace, only traverse
    // sides
    trace_2d_seg_t trace = {
        .org = a->point,
        .dst = c->point,
        .types = LTF_SIDE,
        .flags = TRACE_FLAG_DISCONNECTED_PORTALS_STOP,
    };

    const trace_2d_seg_result_t result = trace_2d_seg(level, &trace);
    return result.result == TRACE_RESULT_NOT_STOPPED;
}

bool path_to_goal(
        level_t *level,
        v2 start,
        v2 goal,
        f32 (*cost_fn)(
            level_t*,
            const subsector_t*,
            const subsector_t*,
            const sect_line_t*,
            void*),
        bool (*trivial_fn)(
            level_t*,
            const path_point_t*,
            const path_point_t*,
            const path_point_t*,
            void*),
        DYNLIST(path_point_t) *out,
        void *userdata,
        int flags) {

    if (!trivial_fn) {
        trivial_fn = path_to_goal__default_trivial;
    }

    DYNLIST(subsector_path_point_t) path_subs =
        dynlist_create(subsector_path_point_t, &g->frame_arena);

    if (!path_subsectors_to_goal(
            level,
            start,
            goal,
            cost_fn,
            &path_subs,
            userdata)) {
        // no path
        return false;
    }

    DYNLIST(path_point_t) path_points =
        dynlist_create(path_point_t, &g->frame_arena, dynlist_size(path_subs));

    // add start point
    *dynlist_push(path_points) =
        (path_point_t) {
            .point = start,
        };

    dynlist_each(path_subs, it) {
        const bool disconnect =
            it.el->via
            && it.el->via->side
            && it.el->via->side->is_disconnect;

        if (disconnect) {
            v2
                p_a = wall_midpoint(it.el->via->side->wall),
                p_b = wall_midpoint(it.el->via->side->portal->wall);

            if (flags & PATH_TO_GOAL_DISCONNECT_PROTRUDE) {
                p_a =
                    v2_add(
                        p_a,
                        v2_scale(
                            side_normal(it.el->via->side),
                            -1.0f));

                p_b =
                    v2_add(
                        p_b,
                        v2_scale(
                            side_normal(it.el->via->side->portal),
                            1.0f));
            }

            // add three points: this side, the other side, the other sub
            // center
            *dynlist_push(path_points) =
                (path_point_t) {
                    .point = p_a,
                    .is_on_disconnect = true,
                };

            *dynlist_push(path_points) =
                (path_point_t) {
                    .point = p_b,
                    .is_on_disconnect = false,
                };

            *dynlist_push(path_points) =
                (path_point_t) {
                    .point = it.el->via->side->portal->subsector->center,
                    .is_on_disconnect = false,
                };
        } else if (it.el->via) {
            // not disconnected but still on a line -> add a single point
            *dynlist_push(path_points) =
                (path_point_t) {
                    .point =
                        v2_scale(
                            v2_add(
                                it.el->via->a->pos,
                                it.el->via->b->pos),
                            0.5f),
                    .is_on_disconnect = false,
                };
        }

        if (!disconnect) {
            *dynlist_push(path_points) =
                (path_point_t) {
                    .point = it.el->sub->center,
                };
        }
    }

    // add goal point
    *dynlist_push(path_points) =
        (path_point_t) {
            .point = goal,
        };

    // working backwards from point (i = n - 3), check for each i if i to i + 2
    // is a "trivial" path (trivial_fn is true) so that i + 1 can be removed
    int i = dynlist_size(path_points) - 3;

    while (i >= 0 && dynlist_size(path_points) > 2) {
        if (i + 2 < dynlist_size(path_points)
            && trivial_fn(
                level,
                &path_points[i],
                &path_points[i + 1],
                &path_points[i + 2],
                userdata)) {
            // remove i + 1, i -> i + 2 is trivial
            dynlist_remove(path_points, i + 1);
        }

        i--;
    }

    ASSERT(dynlist_size(path_points) >= 2);

    dynlist_push_all(*out, path_points);
    return true;
}

typedef struct {
    // current origin after portal transformations
    v2 origin;

    // current length of all lines
    f32 len;

    // current lines in sightline
    DYNLIST(line2f_t) lines;

    // all portals this sightline has been through
    // NOTE: if sightline goes from side A -> B (via side A), then "portals" has
    // side B *NOT* side A
    DYNLIST(const side_t*) portals;

    // all sides this sightline has been through, in order
    DYNLIST(const side_t*) sides;

    // *total* length when each side was hit
    DYNLIST(f32) side_lens;
} sightline_2d_t;

static bool trace_sightline_2d__filter_near_portals(
        const side_t *side,
        const level_sides_in_radius_params_t *params) {
    const sightline_2d_t *sightline = params->userdata;

    if (!side->portal || !side->is_disconnect) {
        // reject if not disconnected
        return false;
    }

    vertex_t *vs[2];
    side_get_vertices(side, vs);
    if (point_side(sightline->origin, vs[0]->pos, vs[1]->pos) >= 0.0f) {
        // reject if origin is not visible from side
        return false;
    }

    const f32 dist2 =
        point_to_segment2(sightline->origin, vs[0]->pos, vs[1]->pos);

    if (dist2 > params->r * params->r) {
        // reject if too far away
        return false;
    }

    // check if we've previously been through this portal
    dynlist_each(sightline->portals, it) {
        if (side == *it.el) {
            return false;
        }
    }

    // ok, portal is near
    return true;
}

static trace_resolve_result_e try_get_sightline_2d__resolve(
        level_t *level,
        trace_2d_seg_t *trace,
        const trace_hit_t *hit) {
    if (hit->type == LT_SIDE) {
        sightline_2d_t *sightline = trace->userdata;

        if (hit->side.ptr->portal) {
            *dynlist_push(sightline->sides) = hit->side.ptr;
            *dynlist_push(sightline->side_lens) =
                sightline->len + (v2_distance(trace->org, trace->dst) * hit->t);
        }
    }

    return TRACE_RESOLVE_CONTINUE;
}

static bool try_get_sightline_2d(
        level_t *level,
        v2 start,
        v2 goal,
        f32 max_dist,
        sightline_2d_t *out) {
    sector_t *start_sector, *end_sector;
    if (!(start_sector = level_find_point_sector(level, start, NULL))
        || !(end_sector = level_find_point_sector(level, goal, NULL))) {
        // no sightlines between out of bounds points
        return false;
    }

    // quick fail if end sector is not in EVS for start sector
    if (!sector_matrix_get_for_sector(
            level,
            &level->matrices.evs,
            start_sector,
            end_sector)) {
        return false;
    }

    DYNLIST(sightline_2d_t) queue =
        dynlist_create(sightline_2d_t, &g->frame_arena);

    // start from origin
    *dynlist_push(queue) = (sightline_2d_t) {
        .origin = start,
        .len = 0.0f,
        .lines = dynlist_create(line2f_t, &g->frame_arena),
        .portals = dynlist_create(const side_t*, &g->frame_arena),
        .sides = dynlist_create(const side_t*, &g->frame_arena),
        .side_lens = dynlist_create(f32, &g->frame_arena),
    };

    while (dynlist_size(queue) != 0) {
        // pop front to get sightline with shortest length
        sightline_2d_t sightline = dynlist_remove(queue, 0);

        // check if goal is directly visible from origin
        {
            trace_2d_seg_t trace_2d_seg_data = {
                .org = sightline.origin,
                .dst = goal,
                .types = LTF_SIDE,
                .flags = TRACE_FLAG_DISCONNECTED_PORTALS_STOP,
                .resolve_fn = try_get_sightline_2d__resolve,
                .userdata = &sightline,
            };

            const trace_2d_seg_result_t result =
                trace_2d_seg(level, &trace_2d_seg_data);

            if (result.result == TRACE_RESULT_NOT_STOPPED) {
                // if not stopped, we found a sightline - all done
                // add final line
                *dynlist_push(sightline.lines) = (line2f_t) {
                    .a = sightline.origin,
                    .b = goal,
                };

                *out = sightline;
                return true;
            }
        }

        // goal is not visible - check if there are any near portals visible
        // within length
        DYNLIST(side_t*) near_portals =
            dynlist_create(side_t*, &g->frame_arena);

        level_sides_in_radius(
            level,
            &(level_sides_in_radius_params_t) {
                .pos = sightline.origin,
                .r = max_dist - sightline.len,
                .filter_fn = trace_sightline_2d__filter_near_portals,
                .userdata = &sightline,
                .out = &near_portals,
            });

        dynlist_each(near_portals, it) {
            // improvement opportunity: don't just check midpoint
            // is midpoint visible?
            const side_t *side = *it.el;
            const v2 midpoint = wall_midpoint(side->wall);
            trace_2d_seg_t trace_2d_seg_data = {
                .org = sightline.origin,
                .dst = midpoint,
                .types = LTF_SIDE,
                .flags = TRACE_FLAG_DISCONNECTED_PORTALS_STOP,
                .ignore_ptr = lptr_from(*it.el),
            };
            const trace_2d_seg_result_t result =
                trace_2d_seg(level, &trace_2d_seg_data);

            if (result.result != TRACE_RESULT_NOT_STOPPED) {
                continue;
            }

            // ok, portal is visible, move sightline there and keep checking
            sightline_2d_t new_sightline = {
                .origin =
                    v2_add(
                        portal_transform(level, side, side->portal, midpoint),
                        v2_scale(side_normal(side->portal), 0.01f)),
                .len = sightline.len,
                .lines = dynlist_copy(sightline.lines),
                .portals = dynlist_copy(sightline.portals),
                .sides = dynlist_copy(sightline.sides),
                .side_lens = dynlist_copy(sightline.side_lens),
            };

            // add new line
            const line2f_t new_line = { .a = sightline.origin, .b = midpoint };
            *dynlist_push(new_sightline.lines) = new_line;
            new_sightline.len += v2_distance(new_line.a, new_line.b);

            // add new portal
            *dynlist_push(new_sightline.portals) = side->portal;

            // add new side
            *dynlist_push(new_sightline.sides) = side;

            // record length when hit
            *dynlist_push(new_sightline.side_lens) = new_sightline.len;

            *dynlist_push(queue) = new_sightline;
        }
    }

    // found nothing
    return false;
}

bool trace_sightline_2d(
        level_t *level,
        v2 start,
        v2 goal,
        f32 max_dist,
        DYNLIST(line2f_t) *out) {
    sightline_2d_t sightline;
    if (!try_get_sightline_2d(level, start, goal, max_dist, &sightline)) {
        return false;
    }

    dynlist_push_all(*out, sightline.lines);
    return true;
}

bool trace_sightline_3d(
        level_t *level,
        v3 start,
        v3 goal,
        f32 max_dist,
        DYNLIST(line3f_t) *out) {
    sightline_2d_t sightline;
    if (!try_get_sightline_2d(
            level,
            v2_from(start),
            v2_from(goal),
            max_dist,
            &sightline)) {
        return false;
    }

    DYNLIST(line3f_t) lines_3d = dynlist_create(line3f_t, &g->frame_arena);

    // make list of 3D lines
    dynlist_each(sightline.lines, it) {
        *dynlist_push(lines_3d) = (line3f_t) {
            .a = v3_of(it.el->a, 0.0f),
            .b = v3_of(it.el->b, 0.0f),
        };
    }

    // add start/end zs
    lines_3d[0].a.z = start.z;
    lines_3d[dynlist_size(lines_3d) - 1].b.z = goal.z;

    v3 last_origin = start;
    f32 last_origin_len = 0.0f;

    // check that we can pass through all sides
    int j = 0;
    for (int i = 0; i < dynlist_size(sightline.sides); i++) {
        const side_t *side = sightline.sides[i];
        if (!side->portal) {
            WARN("sightline has non-portal side %d", side->id);
            continue;
        }

        const f32 l = sightline.side_lens[i];

        // improvement opportunity: can be more precise
        // get generous zs
        const side_segments_t segs = side_get_segments(level, side);

        if (!segs.middle.present) {
            // TODO??
            continue;
        }

        const rangef_t side_zs = side_segment_zs_at(&segs.middle, 0.5f);

        // improvement opportunity: check a range
        // get d_z: from last_origin to midpoint of next portal
        // (or goal if there are no more portals)
        f32 d_z;

        if (j == dynlist_size(sightline.portals)) {
            // no more portals, go to goal
            d_z = v3_dir(last_origin, goal).z;
        } else {
            // go to midpoint of portal (on OUR side)
            if (!sightline.portals[j]->portal) {
                // TODO: breaks on one sided portals!
                return false;
            }

            const side_segments_t portal_segs =
                side_get_segments(level, sightline.portals[j]->portal);

            if (!portal_segs.middle.present) {
                // WARN("failed no middle in portal??");
                return false;
            }

            const rangef_t portal_zs =
                side_segment_zs_at(&portal_segs.middle, 0.5f);
            const v3 portal_goal =
                v3_of(
                    sightline.lines[j].b,
                    rangef_lerp(portal_zs, 0.5f));

            d_z = v3_dir(last_origin, portal_goal).z;
        }

        // move z from last origin -> current according to d_z
        const f32 z_l = last_origin.z + ((l - last_origin_len) * d_z);
        if (z_l < side_zs.z0 || z_l > side_zs.z1) {
            return false;
        }

        // move origin if this is a portal
        if (j < dynlist_size(sightline.portals)
            && side->portal == sightline.portals[j]) {
            const side_t *portal = sightline.portals[j];

            // must always have at least one more line than total portals
            if ((j + 1) >= dynlist_size(sightline.lines)) {
                WARN(
                    "not enough lines for portals? %d/%d",
                    dynlist_size(sightline.lines),
                    dynlist_size(sightline.portals));
                return false;
            }

            const side_segments_t portal_segs =
                side_get_segments(level, portal);
            const rangef_t portal_zs =
                side_segment_zs_at(&portal_segs.middle, 0.5f);

            const f32 new_origin_len = sightline.side_lens[i];
            const v3 new_origin =
                v3_of(
                    sightline.lines[j + 1].a,
                    rangef_lerp(portal_zs, 0.5f));

            // update zs...
            lines_3d[j + 0].b.z = z_l;          // portal entry z
            lines_3d[j + 1].a.z = new_origin.z; // portal exit z

            // update origin
            last_origin_len = new_origin_len;
            last_origin = new_origin;

            // advance to next portal (if there are more)
            j++;
        }
    }

    dynlist_push_all(*out, lines_3d);
    return true;
}
