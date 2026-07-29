#include "entity/enemy.h"
#include "gfx/model.h"
#include "gfx/tex_atlas.h"
#include "level/entity.h"
#include "game.h"
#include "level/sector.h"
#include "level/side.h"
#include "trace.h"
#include "vtext.h"

#define FLOAT_HEIGHT 2.0f

static v3 compute_fly_dir(level_t *level, entity_t *ent) {
    v3 dir;

    if (!ent->target_last_seen_tick) {
        // keep moving wherever
        dir =
            v3_of(
                rand_v2_cone(
                    &g->rand,
                    v2_normalize_from(ent->fly_dir),
                    PI_3),
                0.0f);
    }

    if (ent->target_last_seen_dist > 12.0f) {
        // move closer to goal
        dir = ent->target_last_seen_dir;
    } else {
        // move tangential to goal so we can stay out of range while we spew
        const v2 tangent =
            v2_rotate(
                v2_normalize_from(ent->target_last_seen_dir),
                PI_2);

        // find out which direction we're currently going relative to
        // tangent
        const f32 sgn = sign(v2_dot(tangent, v2_normalize_from(ent->dir)));

        // move in that direction
        const v2 dir_2d = v2_scale(tangent, sgn);

        dir = v3_normalize(v3_of(dir_2d, ent->target_last_seen_dir.z));

        // add some mild 2D randomness
        rand_t rand = rand_create(ent->id + (g->tick / 60));
        dir =
            v3_normalize_of(
                rand_v2_cone(
                    &rand,
                    v2_normalize_from(dir),
                    PI_8),
                dir.z);
    }

    // check that we are above surface
    f32 z_surface = sector_point_zs(ent->sector, ent->pos).z0;

    if (sector_type(ent->sector)->is_liquid) {
        z_surface += ent->sector->liquid_offset;
    }

    // move towards desired z
    f32 z_diff = (z_surface + FLOAT_HEIGHT + 0.25f) - ent->z;
    dir.z += sign(z_diff) * satf(fabsf(z_diff) / 2.0f);
    dir = v3_normalize(dir);
    LOG("dir is %" PRIv3, FMTv3(dir));

    // smooth interpolate
    return v3_slerp(ent->fly_dir, dir, 0.6f);
}

static void mother_tick(level_t *level, entity_t *ent) {
    enemy_pre_tick(level, ent);

    if (enemy_check_death_and_explode(level, ent)) {
        if (ent->corpse_tick == g->tick) {
            // explode into sacs
            for (int i = 0; i < 30; i++) {
                const v3 dir = rand_v3_cone(&g->rand, v3_of(0, 0, 1), PI_3);
                entity_new(
                    level,
                    &(entity_t) {
                        .itype = ENTITY_TYPE_SAC,
                        .pos_xyz =
                            v3_add(
                                ent->pos_xyz,
                                v3_add(
                                    v3_of(0, 0, 1),
                                    v3_scale(dir, 0.05f))),
                        .vel_xyz =
                            v3_add(
                                v3_of(
                                    v2_scale(rand_v2_dir(&g->rand), 24.0f),
                                    0.0f),
                                v3_scale(dir, 16.0f)),
                    });
            }
        }
        return;
    }

    const entity_t *target = g->player;

    if (secs_since_tick(ent->target_last_seen_tick) >= 10.0f) {
        ent->target_last_seen_tick = 0;
    }

    if (target && ticks_since_tick(ent->target_last_update_tick) > 10) {
        enemy_update_sightline(level, ent, v3_of(0), v3_of(0));
    }

    if (ent->move_ticks == 0) {
        ent->fly_dir = compute_fly_dir(level, ent);
        ent->move_ticks = 15;
    }

    if (secs_since_tick(ent->last_attack_tick) >= 8.0f
        && ent->target_last_seen_dist <= 16.0f
        && rand_chance(&g->rand, 0.05f)) {
        // do attack
        ent->last_attack_tick = g->tick;
    }

    if (secs_since_tick(ent->last_attack_tick) <= 2.0f
        && (ticks_since_tick(ent->last_attack_tick) % 2) == 0) {
        // spew
        const v3 dir = rand_v3_cone(&g->rand, v3_of(0, 0, 1), PI_4);
        entity_new(
            level,
            &(entity_t) {
                .itype = ENTITY_TYPE_SAC,
                .pos_xyz =
                    v3_add(
                        ent->pos_xyz,
                        v3_add(
                            v3_of(0, 0, 1),
                            v3_scale(dir, 0.05f))),
                .vel_xyz = v3_scale(dir, 12.0f),
            });
    }

    enemy_post_tick(level, ent);
}

static void mother_fixed_update(level_t *level, entity_t *ent, f32 dt) {
    // face fly dir, but fly towards current dir!
    entity_face_dir(level, ent, ent->fly_dir, 0.1f, dt);
    enemy_fly_towards(level, ent, 20.0f, ent->dir, dt);

    const rangef_t zs = sector_point_zs(ent->sector, ent->pos);
    f32 z_surface = zs.z0;

    if (sector_type(ent->sector)->is_liquid) {
        z_surface += ent->sector->liquid_offset;
    }

    ent->z = clamp(ent->z, z_surface + FLOAT_HEIGHT, zs.z1);
}

static void mother_move_on_hit(
        level_t *level,
        entity_t *ent,
        const trace_hit_t *hit) {
    if (hit->type == LT_SIDE) {
        // move away from side
        rand_t rand = rand_create(ent->id + (g->tick % 60));
        v3 dir =
            v3_normalize(
                v3_of(
                    rand_v2_cone(&rand, side_normal(hit->side.ptr), PI_3),
                    ent->fly_dir.z));

        // if we hit top/bottom of a slide, move away from that segment
        side_segment_t seg;
        if (side_get_z_segment(
                level,
                hit->side.ptr,
                hit->side.hit_pos,
                ent->z,
                &seg)) {
            if (seg.index == SIDE_SEGMENT_BOTTOM) {
                dir = v3_normalize(v3_of(v2_from(dir), 2.0));
            } else if (seg.index == SIDE_SEGMENT_TOP) {
                dir = v3_normalize(v3_of(v2_from(dir), -2.0));
            }
        }

        ent->fly_dir = dir;
        ent->move_ticks = 30;
        ent->move_frustration += 30;
    }

    if (hit->type == LT_ENTITY
        && hit->entity.is_collision
        && hit->entity.ptr != g->player) {
        // hit another entity, move away from it slightly
        const v2 normal = v2_dir(hit->entity.ptr->pos, ent->pos);

        // only move if we're moving towards the entity (hit normal and move dir
        // are opposites)
        if (v2_dot(normal, v2_normalize_from(ent->fly_dir)) < 0.0f) {
            const v3 dir = v3_slerp(ent->fly_dir, v3_of(normal, 0.0f), 0.3f);
            ent->fly_dir = rand_v3_cone(&g->rand, dir, PI_4);
            ent->move_ticks = 20;
            ent->move_frustration += 10;
        }
    }
}

static void mother_on_move_portal(
        level_t *level,
        entity_t *ent,
        const trace_hit_t *hit) {
    enemy_update_sightline(level, ent, v3_of(0), v3_of(0, 0, 1));

    // keep moving in the new direction
    ent->move_ticks = 30;
}

static void mother_models(
        level_t *l,
        entity_t *e,
        DYNLIST(model_t) *models) {
    const model_data_t *md =
        model_atlas_lookupf(
            "mother$idle$%d",
            ((g->tick + (e->id * 17)) / 10) % 9);

    m4 tr = m4_identity();
    tr = m4_translate(tr, e->pos_xyz);
    tr = m4_translate(tr, v3_of(0.0f, 0.0f, -1.75f));
    tr =
        m4_mul(
            tr,
            m4_rotate_make_from_to(
                v3_of(1, 0, 0),
                v3_normalize(v3_of(e->dir.x, e->dir.y, 0.0f))));

    const v3 pain_color = v3_of(1.0f, 0.2f, 0.2f);
    const v3 hit_color = v3_of(1.0f, 0.8f, 0.2f);

    f32 extra_scale = 0.0f;
    v3 extra_color = v3_of(0.4f, 0.2f, 0.9f);

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
            .data = md,
            .tex = tex_atlas_lookup("x_pink"),
            .overlay_alpha = 0.5f,
            .tex_overlay =
                vtext_get_or_create(
                    "mother_overlay",
                    "$ITYEARNING FOR ESCAPE "),
            .flags = 0
                | MRF_OVERLAY_SCROLL_V
                | MRF_ENEMY,
            .hsv = v3_of(-0.73, 0.7, -0.6),
            .transform = tr,
            .tint = v4_of(extra_color, extra_scale),
            .extra_light = v3_scale(extra_color, extra_scale),
            .extra_bloom = v4_of(extra_color, 0.05f + extra_scale),
            .corpse_tick = e->corpse_tick,
        };
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_MOTHER,
    (entity_type_t) {
        .is_enemy = true,
        .has_model = true,
        .is_damageable = true,
        .bounds = {
            .radius = 1.0f,
            .height = 1.4f,
            .hitbox_radius = 2.6f,
            .hitbox_height = 1.4f,
        },
        .drag_air = v3_const(1.0f),
        .drag_floor = v3_const(1.0f),
        .mass = 0.0f,
        .max_health = 20.0f,
        .tick_fn = mother_tick,
        .fixed_update_fn = mother_fixed_update,
        .on_move_portal_fn = mother_on_move_portal,
        .move_on_hit_fn = mother_move_on_hit,
        .models_fn = mother_models,
    })
