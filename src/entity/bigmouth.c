#include "entity/enemy.h"
#include "gfx/model.h"
#include "gfx/tex_atlas.h"
#include "level/entity.h"
#include "game.h"
#include "level/level.h"
#include "level/side.h"
#include "trace.h"
#include "vtext.h"

static void bigmouth_tick(level_t *level, entity_t *ent) {
    enemy_pre_tick(level, ent);

    if (enemy_check_death_and_explode(level, ent)) {
        return;
    }

    const entity_t *target = g->player;

    if (secs_since_tick(ent->target_last_seen_tick) >= 10.0f) {
        ent->target_last_seen_tick = 0;
    }

    if (target && ticks_since_tick(ent->target_last_update_tick) > 10) {
        enemy_update_sightline(level, ent, v3_of(0), v3_of(0));
    }

    if (ent->target_last_seen_tick != 0
        && ent->move_frustration > 30
        && ticks_since_tick(ent->last_path_tick) >= 15) {

        if (enemy_try_find_path(level, ent, ent->target_last_seen_pos)) {
            // follow path
            ent->path_ticks = 120;
            ent->move_ticks = 0;
        }
    }

    if (ent->move_ticks == 0) {
        // 0..1, how near are we to our goal position?
        // use to make movements more precise (less random) as goal gets closer
        f32 nearness_to_goal;

        if (ent->target_last_seen_tick != 0) {
            nearness_to_goal =
                1.0f -
                    satf(
                        v2_distance(
                            ent->pos,
                            v2_from(ent->target_last_seen_pos)) / 8.0f);
        } else {
            nearness_to_goal = 0.0f;
        }

        if (nearness_to_goal > 0.9f) {
            // reset frustration if close
            ent->move_frustration = 0;
        }

        // check if we should path:
        // want to path, have a path, and aren't very close to goal
        if (ent->target_last_seen_tick != 0
            && ent->path_ticks > 0
            && ent->path.n > 0
            && nearness_to_goal < 0.9f) {
            v3 goal = v3_of(ent->path.arr[0].point, 0.0f);

            // TODO: maybe try to maintain current z level?
            const rangef_t goal_zs = level_point_zs(level, v2_from(goal));
            goal.z =
                rangef_lerp(
                    goal_zs,
                    0.8f + rand_f32(&g->rand, -0.1f, 0.1f));

            ent->fly_dir =
                v3_slerp(
                    ent->fly_dir,
                    v3_dir(entity_center(level, ent), goal),
                    0.9f + (nearness_to_goal * 0.1f));
            ent->move_ticks = 15;
        }

        // check if we still don't have a direction...
        if (ent->move_ticks == 0) {
            if (ent->target_last_seen_tick == 0) {
                // no target yet, just move around in approximately the same
                // direction we have been
                ent->fly_dir = rand_v3_cone(&g->rand, ent->fly_dir, PI_3);
                ent->move_ticks = 45;
            } else {
                // have goal, move mostly towards goal
                v3 dir = ent->target_last_seen_dir;

                const rangef_t cur_zs = level_point_zs(level, ent->pos);
                const f32 goal_z =
                    lerp(
                        rangef_clamp(
                            cur_zs,
                            min(cur_zs.z0 + 3.0f, cur_zs.z1 - 1.5f)),
                        ent->target_last_seen_pos.z,
                        nearness_to_goal);

                // want to fly up if close to floor
                if (ent->z <= cur_zs.z0 + 1.5f
                    && cur_zs.z1 - cur_zs.z0 >= 2.0f) {
                    dir =
                        v3_slerp(
                            dir,
                            v3_of(0, 0, 1),
                            0.25f);
                }

                // want to fly to goal z when not frustrated
                if (ent->move_frustration < 10) {
                    dir =
                        v3_slerp(
                            dir,
                            v3_of(0, 0, sign(goal_z - ent->z)),
                            0.25f);
                }

                // some 2D randomness
                dir =
                    v3_normalize(
                        v3_of(
                            rand_v2_cone(&g->rand, v2_normalize_from(dir), PI_8),
                            dir.z));

                // smooth interpolate
                ent->fly_dir =
                    v3_slerp(
                        ent->fly_dir,
                        dir,
                        0.6f + (nearness_to_goal * 0.4f));

                ent->move_ticks = 15;
            }
        }
    }

    enemy_post_tick(level, ent);
}

static void bigmouth_fixed_update(level_t *l, entity_t *e, f32 dt) {
    entity_face_dir(l, e, e->fly_dir, 0.9f, dt);
    enemy_fly_towards(l, e, 90.0f, e->fly_dir, dt);
}

static void bigmouth_move_on_hit(
        level_t *l,
        entity_t *e,
        const trace_hit_t *hit) {
    if (hit->type == LT_SIDE) {
        // move away from side
        v3 dir =
            v3_normalize(
                v3_of(
                    rand_v2_cone(&g->rand, side_normal(hit->side.ptr), PI_3),
                    e->fly_dir.z));

        // if we hit top/bottom of a slide, move away from that segment
        side_segment_t seg;
        if (side_get_z_segment(l, hit->side.ptr, hit->side.hit_pos, e->z, &seg)) {
            if (seg.index == SIDE_SEGMENT_BOTTOM) {
                dir = v3_normalize(v3_of(v2_from(dir), 0.5));
            } else if (seg.index == SIDE_SEGMENT_TOP) {
                dir = v3_normalize(v3_of(v2_from(dir), -0.5));
            }
        }

        e->fly_dir = dir;
        e->move_ticks = 30;
        e->move_frustration += 30;
    }

    if (hit->type == LT_ENTITY
        && hit->entity.is_collision
        && hit->entity.ptr != g->player) {
        // hit another entity, move away from it slightly
        const v2 normal = v2_dir(hit->entity.ptr->pos, e->pos);

        // only move if we're moving towards the entity (hit normal and move dir
        // are opposites)
        if (v2_dot(normal, v2_normalize_from(e->fly_dir)) < 0.0f) {
            const v3 dir = v3_slerp(e->fly_dir, v3_of(normal, 0.0f), 0.3f);
            e->fly_dir = rand_v3_cone(&g->rand, dir, PI_4);
            e->move_ticks = 20;
            e->move_frustration += 10;
        }
    }
}

static void bigmouth_on_move_portal(
        level_t *level,
        entity_t *ent,
        const trace_hit_t *hit) {
    enemy_update_sightline(level, ent, v3_of(0), v3_of(0, 0, 1));

    // keep moving in the new direction
    ent->move_ticks = 30;
}

static void bigmouth_on_hit(
        level_t *l,
        entity_t *ent,
        entity_t *other) {
    if (other != g->player) { return; }

    if (ent->pain_ticks > 0 || ticks_since_tick(ent->last_attack_tick) < 30) {
        return;
    }

    // damage player
    entity_try_damage(
        other,
        &(entity_damage_desc_t) {
            .source = ent,
            .dir =
                v3_slerp(
                    v3_dir(ent->pos_xyz, other->pos_xyz),
                    v3_of(0.0f, 0.0f, 1.0f),
                    0.2f),
            .amount = 20.0f,
            .knockback = 10.0f,
            .is_melee = true,
            .tint.params = {
                .color = v4_of(2.0f, 0.2f, 0.2f, 0.3f),
                .duration = 0.2f,
                .fade = true,
            },
            .screenshake = {
                .intensity = 4.0f,
                .duration = 0.4f,
            },
        });

    // move backwards a bit
    ent->fly_dir =
        v3_slerp(
            ent->fly_dir,
            rand_v3_cone(
                &g->rand,
                v3_scale(ent->target_last_seen_dir, -1),
                PI_3),
            0.9f);
    ent->move_ticks = 45;
    ent->last_attack_tick = g->tick;
}

static void bigmouth_models(
        level_t *l,
        entity_t *e,
        DYNLIST(model_t) *models) {
    m4 tr = m4_identity();
    tr = m4_translate(tr, e->pos_xyz);
    tr =
        m4_mul(
            tr,
            m4_rotate_make_from_to(
                v3_of(1, 0, 0),
                v3_normalize(v3_of(e->dir.x, e->dir.y, 0.0f))));
    tr = m4_mul(tr, m4_rotate_make(clamp(e->dir.z, -0.15f, 0.15f), v3_of(1, 0, 0)));

    const v3 pain_color = v3_of(1.0f, 0.2f, 0.2f);
    const v3 hit_color = v3_of(1.0f, 0.8f, 0.2f);

    f32 extra_scale = 0.0f;
    v3 extra_color = v3_of(0.9f, 0.1f, 0.0f);

    if (e->corpse_tick != 0) {
        extra_color = v3_of(0.6f, 0.1f, 0.05f);
        extra_scale = 0.8f;
    } else if (secs_since_tick(e->last_damage_tick) <= 0.066f) {
        extra_color = hit_color;
        extra_scale = 0.2f;
    } else if (e->pain_ticks > 0) {
        extra_color = pain_color;
        extra_scale = 0.2f;
    }

    *dynlist_push(*models) =
        (model_t) {
            .id = lptr_from(e),
            .data =
                model_atlas_lookupf(
                    "bigmouth$gnash$%d",
                    ((g->tick + (e->id * 17)) / 5) % 6),
            .tex = tex_atlas_lookup("p_stonee"),
            .overlay_alpha = 0.5f,
            .tex_overlay =
                vtext_get_or_create(
                    "bigmouth_overlay",
                    "$ITDEEP NEUROTRAUMA "),
            .flags = 0
                | MRF_OVERLAY_SCROLL_H
                | MRF_ENEMY,
            .hsv = v3_of(0.8, 0.3, -0.2),
            .transform = tr,
            .tint = v4_of(extra_color, extra_scale),
            .extra_light = v3_scale(extra_color, extra_scale),
            .extra_bloom = v4_of(extra_color, 0.05f + extra_scale),
            .corpse_tick = e->corpse_tick,
        };
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_BIGMOUTH,
    (entity_type_t) {
        .is_enemy = true,
        .has_model = true,
        .is_damageable = true,
        .bounds = {
            .radius = 0.4f,
            .height = 1.5f,
            .hitbox_radius = 1.2f,
            .hitbox_height = 1.5f,
        },
        .fixed_update_fn = bigmouth_fixed_update,
        .tick_fn = bigmouth_tick,
        .move_on_hit_fn = bigmouth_move_on_hit,
        .on_move_portal_fn = bigmouth_on_move_portal,
        .on_hit_fn = bigmouth_on_hit,
        .drag_air = v3_const(1.0f),
        .drag_floor = v3_const(1.0f),
        .gravity = v3_of(0, 0, 0),
        .step_height = 0.0f,
        .mass = -0.5f,
        .max_health = 20.0f,
        .models_fn = bigmouth_models,
    })
