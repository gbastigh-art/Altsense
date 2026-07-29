#include "entity/enemy.h"
#include "game.h"
#include "level/entity.h"
#include "level/level.h"
#include "level/level_types.h"
#include "level/particle.h"
#include "level/portal.h"
#include "level/sector.h"
#include "level/side.h"
#include "level/wall.h"
#include "util/fixlist.h"

#define MAX_SIGHTLINE 20.0f

void enemy_update_sightline(
        level_t *level,
        entity_t *ent,
        v3 eye_offset,
        v3 target_offset) {
    const entity_t *target = g->player;

    ent->target_last_update_tick = g->tick;

    const v3 eye_pos = v3_add(entity_center(level, ent), target_offset);
    const v3 target_pos = v3_add(entity_center(level, target), target_offset);

    // check if target is visible
    DYNLIST(line3f_t) sightline = dynlist_create(line3f_t, &g->frame_arena);
    if (trace_sightline_3d(
            level,
            eye_pos,
            target_pos,
            MAX_SIGHTLINE,
            &sightline)) {
        ASSERT(dynlist_size(sightline) >= 1);
        ent->target_last_seen_pos = target_pos;
        ent->target_last_seen_dir = v3_safedir(sightline[0].a, sightline[0].b),
        ent->target_last_seen_tick = g->tick;
        ent->target_through_portal = dynlist_size(sightline) > 1;

        ent->target_last_seen_dist = 0.0f;
        dynlist_each(sightline, it) {
            ent->target_last_seen_dist += v3_distance(it.el->a, it.el->b);
        }
    }
}

static f32 enemy_path_cost(
        level_t *l,
        const subsector_t *a,
        const subsector_t *b,
        const sect_line_t *via,
        void *userdata) {
    const entity_t *e = userdata;

    const entity_bounds_t bounds = entity_bounds(l, e);

    if (sector_type(b->parent)->is_liquid) {
        // no liquids
        return -1.0f;
    }

    {
        // check if we can fit in "b"
        const v2 midpoint = v2_divs(v2_add(via->b->pos, via->b->pos), 2.0f);
        const rangef_t zs_b = sector_point_zs(b->parent, midpoint);

        if (zs_b.z1 - zs_b.z0 < bounds.height) {
            // cannot move into subsector b
            return -1.0f;
        }
    }

    if (via->side
        && via->side->portal
        && via->side->portal->portal) {
        if (!side_get_segments(l, via->side).middle.present
            || !side_get_segments(l, via->side->portal).middle.present) {
            return -1.0f;
        }

        // check step height if not flyer
        if (!e->ptype->is_flyer) {
            const side_t *entry =
                via->side->subsector == a ? via->side : via->side->portal;

            const f32 step_height = entity_step_height(l, e);
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
    }

    const f32 big_diameter = 2.1f * bounds.radius;

    // check if we can pass through the line
    if (v2_distance(via->a->pos, via->b->pos) < big_diameter) {
        return -1.0f;
    }

    // add slight randomness to path, differs per-entity per-tick
    rand_t rand = rand_create(e->id + a->id + b->id + (g->tick % 30));
    return subsector_distance_via(a, b, via) + rand_f32(&rand, 0.0f, 0.5f);
}

static trace_resolve_result_e enemy_path_trivial__resolve(
        level_t *l,
        trace_2d_seg_t *trace,
        const trace_hit_t *hit) {
    // non-portals are immediate stop
    if (!hit->side.ptr->portal) {
        return TRACE_RESOLVE_STOP;
    }

    const f32 cost =
        enemy_path_cost(
            l,
            hit->side.ptr->subsector,
            hit->side.ptr->portal->subsector,
            hit->side.ptr->sect_line,
            trace->userdata);

    return cost < 0.0f ? TRACE_RESOLVE_STOP : TRACE_RESOLVE_CONTINUE;
}

static bool enemy_path_trivial(
        level_t *l,
        const path_point_t *a,
        const path_point_t *b,
        const path_point_t *c,
        void *userdata) {
    const entity_t *e = userdata;
    trace_2d_seg_t trace = {
        .org = a->point,
        .dst = c->point,
        .radius = 1.25f * entity_radius(l, e),
        .types = LTF_SIDE,
        .flags = TRACE_FLAG_DISCONNECTED_PORTALS_STOP,
        .resolve_fn = enemy_path_trivial__resolve,
        .userdata = userdata,
    };

    const trace_2d_seg_result_t result = trace_2d_seg(l, &trace);
    return result.result == TRACE_RESULT_NOT_STOPPED;
}

bool enemy_try_find_path(level_t *level, entity_t *ent, v3 goal) {
    ent->last_path_tick = g->tick;

    // try to path and check that we can store it...
    DYNLIST(path_point_t) path = dynlist_create(path_point_t, &g->frame_arena);

    if (path_to_goal(
            level,
            ent->pos,
            v2_from(goal),
            enemy_path_cost,
            enemy_path_trivial,
            &path,
            ent,
            PATH_TO_GOAL_DISCONNECT_PROTRUDE)
            && dynlist_size(path) <= fixlist_capacity(ent->path)) {

        ent->path.n = 0;
        dynlist_each(path, it) {
            *fixlist_push(ent->path) = *it.el;
        }

        return true;
    }

    return false;
}

bool enemy_trivial_to_point(level_t *level, entity_t *ent, v2 dst) {
    trace_2d_seg_t trace = {
        .org = ent->pos,
        .dst = dst,
        .radius = 1.25f * entity_radius(level, ent),
        .types = LTF_SIDE,
        .flags = TRACE_FLAG_DISCONNECTED_PORTALS_STOP,
        .resolve_fn = enemy_path_trivial__resolve,
        .userdata = ent,
    };

    const trace_2d_seg_result_t res = trace_2d_seg(level, &trace);
    switch (res.result) {
    case TRACE_RESULT_NOT_STOPPED:
        return true;
    default:
        return false;
    }
}

bool enemy_should_move_back(const level_t *level, const entity_t *ent) {
    if (ticks_since_tick(ent->last_portal_tick) <= 3) { return false; }

    // are we going to fall off of a cliff in the next few ticks?
    // assume we repeat the last move for the next 10 ticks
    const v2 diff_xy = v2_sub(ent->pos, v2_from(ent->last_tick_pos));
    const v2 pos_xy_pred =
        level_clamp_point(level, v2_add(ent->pos, v2_scale(diff_xy, 10)));

    const sector_t *sect_xy_pred =
        level_find_point_sector(level, pos_xy_pred, ent->sector);

    if (sect_xy_pred && sector_type(sect_xy_pred)->is_liquid) {
        // avoid liquids
        return true;
    }

    // floor z at predicted position
    const f32 z_floor_pred = level_point_zs(level, pos_xy_pred).z0;

    // current z floor
    const f32 z_floor = level_point_zs(level, ent->pos).z0;

    if (z_floor_pred < z_floor
        && (z_floor - z_floor_pred) > entity_step_height(level, ent)) {
        // TODO: allow if our target is down there
        // cliff
        return true;
    }

    return false;
}

void enemy_pre_tick(level_t *level, entity_t *ent) {
    ent->pain_ticks = max(ent->pain_ticks - 1, 0);
    ent->move_ticks = max(ent->move_ticks - 1, 0);
    ent->path_ticks = max(ent->path_ticks - 1, 0);
    ent->move_frustration = clamp(ent->move_frustration - 1, 0, 200);

    // consume path points that have already been reached if path is present
    const int ticks_since_portal = ticks_since_tick(ent->last_portal_tick);
    while(ent->path.n > 0) {
        const path_point_t pt = ent->path.arr[0];

        if (pt.is_on_disconnect && ticks_since_portal <= 1) {
            break;
        }

        if(v2_distance(ent->pos, pt.point) < 1.0f
            || ent->subsector
                == level_find_point_subsector(level, pt.point, NULL)) {
            fixlist_remove(ent->path, 0);

            // get a new move direction
            ent->move_ticks = 0;
        } else {
            break;
        }
    }
}

void enemy_post_tick(level_t *level, entity_t *ent) {
    ent->last_tick_pos = ent->pos_xyz;
}

bool enemy_check_death_and_explode(level_t *level, entity_t *ent) {
    // check if dead
    if (ent->health > 0.0f) { return false; }

    // if already corpse, nothing to do
    if (ent->corpse) { return true; }

    // become a corpse
    ent->corpse = true;
    ent->corpse_tick = g->tick;

    v3 base_dir = ent->last_damage_dir;
    if (v3_eqv_eps(base_dir, v3_of(0))) {
        ent->last_damage_dir = v3_of(0, 0, 1);
    }

    for (int i = 0; i < 3; i++) {
        // spread in impact direction
        const v3 dir = rand_v3_cone(&g->rand, ent->last_damage_dir, PI_3);

        entity_new(
            level,
            &(entity_t) {
                .itype = ENTITY_TYPE_EBALL,
                .pos_xyz = entity_center(level, ent),
                .vel_xyz = v3_scale(dir, 15.0f),
            });
    }

    for (int i = 0, n = rand_n(&g->rand, 30, 40); i < n; i++) {
        const v3 dir =
            rand_v3_cone(
                &g->rand,
                v3_slerp(
                    ent->last_damage_dir,
                    rand_v3_cone(&g->rand, v3_of(0, 0, 1), PI_2),
                    0.5f),
                PI_2);

        particle_new(
            level,
            ent->pos,
            &(particle_t) {
                .type = PARTICLE_TYPE_BLOOD,
                .duration = 2.0f * TICKS_PER_SECOND,
                .color = v4_of(0.5f, 0.1f, 0.05f, 1.0f),
                .pos_xyz = v3_add(entity_center(level, ent), v3_scale(dir, -0.25f)),
                .vel_xyz =
                    v3_add(
                        v3_scale(dir, rand_f32(&g->rand, 9.0f, 20.0f)),
                        v3_of(0, 0, 4)),
            });
    }

    return true;
}

f32 enemy_walk_towards(
        level_t *level,
        entity_t *ent,
        f32 base_speed,
        v2 dir,
        f32 dt) {
    const v2 forward = v2_normalize_from(ent->dir);
    f32 speed = base_speed;

    // scale speed according to how much we're pointing towards our desired
    // direction
    speed *= max(v2_dot(dir, forward), 0);

    if (ent->target_last_seen_tick == 0) {
        // haven't seen target yet or its been a while, move slowly
        speed *= 0.5f;
    }

    if (!ent->grounded) {
        speed *= 0.1f;
    }

    if (ent->pain_ticks > 0) {
        speed *= 0.25f;
    }

    ent->vel = v2_add(ent->vel, v2_scale(forward, speed * dt));

    return speed;
}

f32 enemy_fly_towards(
        level_t *level,
        entity_t *ent,
        f32 base_speed,
        v3 dir,
        f32 dt) {
    const v2 dir_2d = v2_normalize_from(dir);
    f32 speed = base_speed;

    speed *= max(v2_dot(dir_2d, v2_normalize_from(ent->dir)), 0);

    if (ent->target_last_seen_tick == 0) {
        speed *= 0.5f;
    }

    if (ent->pain_ticks > 0) {
        speed *= 0.25f;
    }

    ent->vel_xyz =
        v3_add(
            ent->vel_xyz,
            v3_scale(ent->dir, speed * dt));

    return speed;
}
