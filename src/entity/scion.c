#include "entity/enemy.h"
#include "gfx/model.h"
#include "gfx/tex_atlas.h"
#include "level/entity.h"
#include "game.h"
#include "level/level.h"
#include "level/particle.h"
#include "level/side.h"
#include "trace.h"
#include "vtext.h"

#define CHARGE_TICKS 45
#define DECHARGE_DELAY_S 0.5f
#define CHARGE_DIST 2.0f
#define EXPLODE_RADIUS 3.0f

// compute burst effect while exploding, in 0..1
static f32 scion_burst_effect(const entity_t *ent) {
    // "divisor" is a power of two which gets smaller as charge ticks increases
    int divisor = (1 << 6);
    divisor >>= max(ent->scion_charge_ticks / (CHARGE_TICKS / 3), 4);
    // use sin^4(x) to get a steeper and always positive wave
    return powf(sinf(PI_2 * (ent->scion_charge_ticks / (f32) divisor)), 4.0f);
}

static void scion_explode(level_t *level, entity_t *ent) {
    ent->health = 0;
    ent->corpse = true;
    ent->corpse_tick = g->tick;

    for (int i = 0; i < 80; i++) {
        v3 dir;
        if (ticks_since_tick(ent->last_damage_tick) <= 1) {
            dir =
                v3_slerp(
                    ent->last_damage_dir,
                    v3_of(0, 0, 1),
                    0.5f);
        } else {
            dir = v3_of(0, 0, 1);
        }

        dir = rand_v3_cone(&g->rand, dir, PI_2);

        particle_new(
            level,
            ent->pos,
            &(particle_t) {
                .type = PARTICLE_TYPE_BLOOD,
                .duration = 2.0f * TICKS_PER_SECOND,
                .color = v4_of(0.5f, 0.1f, 0.05f, 1.0f),
                .pos_xyz = v3_add(entity_center(level, ent), v3_scale(dir, -0.25f)),
                .vel_xyz = v3_scale(dir, rand_f32(&g->rand, 16.0f, 24.0f)),
            });
    }

    for (int i = 0; i < 25; i++) {
        v3 dir;
        if (ticks_since_tick(ent->last_damage_tick) <= 1) {
            dir =
                v3_slerp(
                    ent->last_damage_dir,
                    v3_of(0, 0, 1),
                    0.5f);
        } else {
            dir = v3_of(0, 0, 1);
        }

        dir = rand_v3_cone(&g->rand, dir, PI_2);

        particle_new(
            level,
            ent->pos,
            &(particle_t) {
                .type = PARTICLE_TYPE_RICOCHET,
                .duration = rand_f32(&g->rand, 1.2f, 1.6f) * TICKS_PER_SECOND,
                .color = v4_of(1.0f, 0.1f, 0.05f, 1.0f),
                .pos_xyz = v3_add(entity_center(level, ent), v3_scale(dir, -0.25f)),
                .vel_xyz =
                    v3_add(
                        v3_scale(dir, rand_f32(&g->rand, 10.0f, 16.0f)),
                        v3_of(0.0f, 0.0f, 2.0f)),
            });
    }

    const v3 center = entity_center(level, ent);
    DYNLIST(entity_t*) others = dynlist_create(entity_t*, &g->frame_arena);
    level_entities_in_radius_3d(level, center, EXPLODE_RADIUS, &others);

    dynlist_each(others, it) {
        entity_t *other = *it.el;
        const v3 to_other = v3_sub(entity_center(level, other), center);
        f32 d2 = v3_norm2(to_other);
        if (isnan(d2) || isinf(d2)) { d2 = 0.001f; }
        d2 = max(d2, 0.001f);
        const f32 intensity =  1.0f / d2;

        const f32 d = sqrtf(d2);
        v3 dir_to_other = v3_divs(to_other, d);

        if (other->ptype->is_damageable) {
            entity_try_damage(
                other,
                &(entity_damage_desc_t) {
                    .amount = 100.0f * intensity,
                    .dir = dir_to_other,
                    .knockback = 10.0f * intensity,
                });
        } else {
            other->vel_xyz =
                v3_add(
                    other->vel_xyz,
                    v3_scale(dir_to_other, 5.0f * intensity));
        }

        other->vel_xyz =
            v3_add(
                other->vel_xyz,
                v3_of(
                    0,
                    0,
                    8.0f * intensity + (1.0f / (1.0f + other->ptype->mass))));
    }
}

static void scion_tick(level_t *level, entity_t *ent) {
    enemy_pre_tick(level, ent);

    if (ent->health <= 0.0f || ent->scion_charge_ticks >= CHARGE_TICKS) {
        scion_explode(level, ent);
        return;
    }

    const entity_t *target = g->player;

    if (secs_since_tick(ent->target_last_seen_tick) >= 10.0f) {
        // forget target after 10 seconds
        ent->target_last_seen_tick = 0;
    }

    if (target && ticks_since_tick(ent->target_last_update_tick) > 10) {
        enemy_update_sightline(level, ent, v3_of(0), v3_of(0));
    }

    if (ent->target_last_seen_tick != 0
        && ent->move_frustration > 30
        && secs_since_tick(ent->last_path_tick) >= 1.0f) {

        if (enemy_try_find_path(level, ent, ent->target_last_seen_pos)) {
            // follow path
            ent->path_ticks = 120;
            ent->move_ticks = 0;
        }
    }

    // try to abandon path if movement is trivial
    if (ent->path_ticks > 0
        && (g->tick % 10) == (ent->id % 10)
        && enemy_trivial_to_point(
            level,
            ent,
            v2_from(ent->target_last_seen_pos))) {
        // get new move dir without path
        ent->path.n = 0;
        ent->path_ticks = 0;
        ent->move_ticks = 0;
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
        const v2 dir_xy = v2_safedir(v2_from(ent->last_tick_pos), ent->pos);
        ent->walk_dir = rand_v2_cone(&g->rand, v2_scale(dir_xy, -1), PI_4);
        ent->move_ticks = 40;

        // frustration to encourage pathing
        ent->move_frustration += 40;
    }

    // are we near our target?
    if (entity_between_3d(level, ent, target) < CHARGE_DIST) {
        ent->scion_charge_ticks++;

        if (ent->scion_last_charge_tick != g->tick - 1) {
            ent->scion_start_charge_tick = g->tick;
        }

        ent->scion_last_charge_tick = g->tick;
    } else if (
        !ent->scion_last_charge_tick
        || secs_since_tick(ent->scion_last_charge_tick) >= DECHARGE_DELAY_S) {
        ent->scion_charge_ticks = max(ent->scion_charge_ticks - 1, 0);
    }

    enemy_post_tick(level, ent);
}

static void scion_fixed_update(level_t *l, entity_t *e, f32 dt) {
    entity_face_dir(l, e, v3_of(e->walk_dir, 0.0f), 1.3f, dt);

    f32 speed = 120.0f;
    if (e->scion_last_charge_tick) {
        f32 scale = satf(secs_since_tick(e->scion_last_charge_tick) / 0.5f);
        scale += 0.5f * (1.0f - satf(secs_since_tick(e->scion_start_charge_tick) / 0.25f));
        speed *= scale;
    }

    const f32 moved_speed = enemy_walk_towards(l, e, speed, e->walk_dir, dt);
    e->anim_time += (moved_speed * dt) / 14.0f;
}

static void scion_move_on_hit(
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
        const v2 normal = v2_dir_or(hit->entity.ptr->pos, e->pos, V2_AXIS_X);

        // only move if we're moving towards the entity
        // (hit normal and move dir are opposites)
        if (v2_dot(normal, e->walk_dir) < 0.0f) {
            const v2 dir = v2_slerp(e->walk_dir, normal, 0.3f);
            e->walk_dir = rand_v2_cone(&g->rand, dir, PI_4);
            e->move_ticks = 20;
            e->move_frustration += 10;
        }
    }
}

static void scion_on_move_portal(
        level_t *level,
        entity_t *ent,
        const trace_hit_t *hit) {
    enemy_update_sightline(level, ent, v3_of(0), v3_of(0));

    // keep moving in the new direction
    ent->move_ticks = 30;
}

static void scion_models(
        level_t *l,
        entity_t *e,
        DYNLIST(model_t) *models) {
    const model_data_t *md =
        model_atlas_lookupf(
            "scion$walk$%d",
            (((int) (e->anim_time * 5.0f)) + e->id) % 6);

    m4 tr = m4_identity();
    tr = m4_translate(tr, e->pos_xyz);
    tr = m4_mul(tr, m4_rotate_make_from_to(v3_of(1, 0, 0), e->dir));

    tr = m4_translate(tr, v3_scale(md->default_group.centroid, +1));
    {
        f32 t = e->scion_charge_ticks / (f32) CHARGE_TICKS;
        const f32 scale = 1.0f + (0.2f * t * scion_burst_effect(e));
        tr = m4_mul(tr, m4_scale_make(v3_of(scale)));
    }
    tr = m4_translate(tr, v3_scale(md->default_group.centroid, -1));

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
    } else if (ticks_since_tick(e->scion_last_charge_tick) <= 10) {
        if (scion_burst_effect(e) >= 0.5f) {
            extra_color = pain_color;
            extra_scale =
                0.4f * satf(e->scion_charge_ticks / (CHARGE_TICKS / 2.0f));
        }
    }

    *dynlist_push(*models) =
        (model_t) {
            .id = lptr_from(e),
            .data = md,
            .tex = tex_atlas_lookup("scion"),
            .overlay_alpha = 0.5f,
            .tex_overlay =
                vtext_get_or_create(
                    "scion_overlay",
                    "$ITSUFFER FROM SENSATION "),
            .flags = 0
                | MRF_OVERLAY_SCROLL_H
                | MRF_ENEMY,
            .hsv = v3_of(0.15, -0.2, -0.2),
            .transform = tr,
            .tint = v4_of(extra_color, extra_scale),
            .corpse_tick = e->corpse_tick,
            .extra_light = v3_scale(extra_color, extra_scale),
            .extra_bloom = v4_of(extra_color, 0.05f + extra_scale),
        };
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_SCION,
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
        .fixed_update_fn = scion_fixed_update,
        .tick_fn = scion_tick,
        .move_on_hit_fn = scion_move_on_hit,
        .on_move_portal_fn = scion_on_move_portal,
        .drag_air = v3_const(0.15f),
        .drag_floor = v3_const(1.0f),
        .gravity = v3_of(0, 0, -1),
        .step_height = 0.75f,
        .mass = -0.5f,
        .max_health = 20.0f,
        .models_fn = scion_models,
    })
