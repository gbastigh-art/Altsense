#include "entity/enemy.h"
#include "gfx/model.h"
#include "level/entity.h"
#include "game.h"
#include "level/side.h"
#include "trace.h"
#include "vtext.h"

static void crawler_tick(level_t *level, entity_t *ent) {
    enemy_pre_tick(level, ent);

    if (enemy_check_death_and_explode(level, ent)) {
        return;
    }

    const entity_t *target = g->player;

    if (secs_since_tick(ent->target_last_seen_tick) >= 10.0f) {
        // forget target after 10 seconds
        ent->target_last_seen_tick = 0;
    }

    if (target && secs_since_tick(ent->target_last_update_tick) > 0.15f) {
        enemy_update_sightline(level, ent, v3_of(0), v3_of(0));
    }

    if (ent->target_last_seen_tick != 0
        && ent->move_frustration > 30
        && ticks_since_tick(ent->last_path_tick) >= 15) {
        // we aren't able to move properly, construct a path to the goal and
        // follow it

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
            ent->walk_dir =
                v2_slerp(
                    ent->walk_dir,
                    v2_safedir(ent->pos, ent->path.arr[0].point),
                    0.9f + (nearness_to_goal * 0.1f));
            ent->move_ticks = 10;
        }

        // check if we still don't have a direction...
        if (ent->move_ticks == 0) {
            if (ent->target_last_seen_tick == 0) {
                // no target yet, just move around in approximately the same
                // direction we have been
                ent->walk_dir = rand_v2_cone(&g->rand, ent->walk_dir, PI_4);
                ent->move_ticks = 30;
            } else {
                // have goal, move mostly towards goal
                ent->walk_dir =
                    rand_v2_cone(
                        &g->rand,
                        v2_slerp(
                            ent->walk_dir,
                            v2_normalize_from(ent->target_last_seen_dir),
                            0.85f + (nearness_to_goal * 0.15f)),
                        (1.0f - nearness_to_goal) * PI_4);
                ent->move_ticks = 8;
            } 
        }
    }

    if (enemy_should_move_back(level, ent)) {
        // move backwards from current direction
        const v2 dir_xy = v2_dir(v2_from(ent->last_tick_pos), ent->pos);
        ent->walk_dir = rand_v2_cone(&g->rand, v2_scale(dir_xy, -1), PI_4);
        ent->move_ticks = 40;

        // frustration to encourage pathing
        ent->move_frustration += 40;
    }

    enemy_post_tick(level, ent);
}

static void crawler_fixed_update(level_t *l, entity_t *e, f32 dt) {
    entity_face_dir(l, e, v3_of(e->walk_dir, 0.0f), 1.3f, dt);

    const f32 moved_speed = enemy_walk_towards(l, e, 100.0f, e->walk_dir, dt);

    // TODO: use anim_time for crawler
    e->anim_time += (moved_speed * dt) / 10.0f;
}

static void crawler_move_on_hit(
        level_t *l,
        entity_t *e,
        const trace_hit_t *hit) {
    if (hit->type == LT_SIDE) {
        // move away from side
        e->walk_dir = rand_v2_cone(&g->rand, side_normal(hit->side.ptr), PI_3);
        e->move_ticks = 20;
        e->move_frustration += 20;
    }

    if (hit->type == LT_ENTITY
        && hit->entity.is_collision
        && hit->entity.ptr != g->player) {
        // hit another entity, move away from it slightly
        const v2 normal =
            v2_valid_or(v2_dir(hit->entity.ptr->pos, e->pos), v2_of(1, 0));

        // only move if we're moving towards the entity (hit normal and move dir
        // are opposites)
        if (v2_dot(normal, e->walk_dir) < 0.0f) {
            const v2 dir = v2_slerp(e->walk_dir, normal, 0.3f);
            e->walk_dir = rand_v2_cone(&g->rand, dir, PI_4);
            e->move_ticks = 20;
            e->move_frustration += 10;
        }
    }
}

static void crawler_on_move_portal(
        level_t *level,
        entity_t *ent,
        const trace_hit_t *hit) {
    enemy_update_sightline(level, ent, v3_of(0), v3_of(0));

    // keep moving in the new direction
    ent->move_ticks = 30;
}

static void crawler_on_hit(
        level_t *l,
        entity_t *ent,
        entity_t *other) {
    if (other != g->player) { return; }

    if (ent->pain_ticks > 0 || ticks_since_tick(ent->last_attack_tick) < 15) {
        // don't attack again if in pain/just attacked
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
    ent->walk_dir = rand_v2_cone(&g->rand, v2_scale(ent->walk_dir, -1), PI_3);
    ent->move_ticks = 20;
    ent->last_attack_tick = g->tick;
}

static void crawler_models(
        level_t *l,
        entity_t *e,
        DYNLIST(model_t) *models) {
    m4 tr = m4_identity();
    tr = m4_translate(tr, e->pos_xyz);
    tr = m4_mul(tr, m4_rotate_make_from_to(v3_of(1, 0, 0), e->dir));

    const v3 pain_color = v3_of(1.0f, 0.2f, 0.2f);
    const v3 hit_color = v3_of(1.0f, 0.8f, 0.2f);

    f32 extra_scale = 0.0f;
    v3 extra_color = v3_of(0);

    if (e->corpse_tick != 0) {
        extra_color = v3_of(0.6f, 0.1f, 0.05f);
        extra_scale = 0.8f;
    } else if (secs_since_tick(e->last_damage_tick) <= 0.066f) {
        extra_color = hit_color;
        extra_scale = 0.5f;
    } else if (e->pain_ticks > 0) {
        extra_color = pain_color;
        extra_scale = 0.5f;
    }

    *dynlist_push(*models) =
        (model_t) {
            .id = lptr_from(e),
            .data = model_atlas_lookupf("crawler$walk$%d", ((g->tick + (e->id * 17)) / 5) % 5),
            .tex = { .index = 0 },
            .overlay_alpha = 0.5f,
            .tex_overlay =
                vtext_get_or_create(
                    "crawler_overlay",
                    "$ITSUFFER FROM SENSATION "),
            .flags = 0
                | MRF_OVERLAY_SCROLL_H
                | MRF_ENEMY,
            .hsv = v3_of(0, 0, 1),
            .transform = tr,
            .tint = v4_of(extra_color, extra_scale),
            .extra_light = v3_scale(extra_color, extra_scale),
            .extra_bloom = v4_of(extra_color, 0.05f + extra_scale),
            .corpse_tick = e->corpse_tick,
        };
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_CRAWLER,
    (entity_type_t) {
        .is_enemy = true,
        .has_model = true,
        .is_damageable = true,
        .bounds = {
            .radius = 0.2f,
            .height = 1.2f,
            .hitbox_radius = 0.8f,
            .hitbox_height = 1.2f,
        },
        .fixed_update_fn = crawler_fixed_update,
        .tick_fn = crawler_tick,
        .move_on_hit_fn = crawler_move_on_hit,
        .on_hit_fn = crawler_on_hit,
        .on_move_portal_fn = crawler_on_move_portal,
        .drag_air = v3_const(0.15f),
        .drag_floor = v3_const(1.0f),
        .gravity = v3_of(0, 0, -1),
        .step_height = 0.75f,
        .mass = -0.5f,
        .max_health = 20.0f,
        .models_fn = crawler_models,
    })
