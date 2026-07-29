#include "entity/enemy.h"
#include "frame_local.h"
#include "gfx/model.h"
#include "gfx/tex_atlas.h"
#include "level/entity.h"
#include "game.h"
#include "level/level.h"
#include "level/particle.h"
#include "trace.h"
#include "vtext.h"

#define SAC_MAX_SIGHTLINE 20.0f
#define SAC_EXPLODE_RADIUS 2.0f
#define SAC_EXPLODE_SECS 8.0f

static void sac_explode(level_t *level, entity_t *ent) {
    const v3 center = entity_center(level, ent);
    DYNLIST(entity_t*) others = dynlist_create(entity_t*, &g->frame_arena);
    level_entities_in_radius_3d(level, center, SAC_EXPLODE_RADIUS, &others);

    dynlist_each(others, it) {
        entity_t *other = *it.el;
        const v3 to_other = v3_sub(entity_center(level, other), center);
        f32 d2 = v3_norm2(to_other);
        if (isnan(d2) || isinf(d2)) { d2 = 0.001f; }
        d2 = max(d2, 0.001f);
        const f32 intensity =  1.0f / d2;

        const f32 d = sqrtf(d2);
        v3 dir_to_other = v3_divs(to_other, d);

        if (other->ptype->is_damageable && other->itype != ENTITY_TYPE_SAC) {
            entity_try_damage(
                other,
                &(entity_damage_desc_t) {
                    .amount = 5.0f * intensity,
                    .dir = dir_to_other,
                    .knockback = 1.0f,
                });
        } else {
            other->vel_xyz =
                v3_add(
                    other->vel_xyz,
                    v3_scale(dir_to_other, 5.0f * intensity));
        }
    }

    // go away
    ent->corpse = true;
    ent->corpse_tick = g->tick;

    // light
    *dynlist_push(g_renderer->env_lights) =
        (env_light_t) {
            .pos = entity_center(level, ent),
            .duration_secs = 0.5f,
            .params = {
                .attenuation = 0.8,
                .power = 1.5f,
                .color = v3_of(0.25f, 1.0f, 0.25f),
                .c1 = 4.0f,
                .c2 = 15.0f,
                .ambient = 0.0f,
                .flags = LIGHT_FLAG_NO_SHADOWS | LIGHT_FLAG_IGNORE_NEAR,
            },
        };

    // gas
    for (int i = 0, n = rand_n(&g->rand, 10, 14); i < n; i++) {
        const v3 dir = rand_v3_cone(&g->rand, v3_of(0, 0, 1), PI_2);
        particle_new(
            level,
            ent->pos,
            &(particle_t) {
                .type = PARTICLE_TYPE_SAC_GAS,
                .duration =
                    (1.0f + rand_f32(&g->rand, -0.3f, 0.3f))
                        * TICKS_PER_SECOND,
                .color = v4_of(0.3f, 1.0f, 0.3f, 1.0f),
                .pos_xyz = ent->pos_xyz,
                .vel_xyz =
                    v3_add(
                        v3_scale(dir, 5.0f),
                        v3_of(0, 0, 2.0f)),
                .dir = v3_of(1, 0, 0),
            });
    }
}

static void sac_tick(level_t *level, entity_t *ent) {
    if (ent->health <= 0.0f
        || (secs_since_tick(ent->spawn_tick) >= SAC_EXPLODE_SECS
            && rand_chance(&g->rand, 0.1f))) {
        sac_explode(level, ent);
        return;
    }

    entity_t *target = g->player;

    if (target
        && ticks_since_tick(ent->target_last_update_tick) > 10
        && rand_chance(&g->rand, 0.33f)) {
        enemy_update_sightline(level, ent, v3_of(0), v3_of(0, 0, 1.5f));

        if (ent->target_last_seen_dist <= 1.0f) {
            sac_explode(level, ent);
            return;
        }
    }

    v3 dir = ent->target_last_seen_dir;

    if (v3_eqv_eps(ent->fly_dir, v3_of(0))) {
        ent->fly_dir = dir;
    } else {
        if (ent->target_last_seen_dist > 2.0f) {
            // a little random flight
            rand_t rand = rand_create((g->tick / 30) + (ent->id * 17));
            dir = rand_v3_cone(&rand, dir, PI_10);
            ent->fly_dir =
                v3_dtslerp(
                    ent->fly_dir,
                    dir,
                    8.0f,
                    1.0f / TICKS_PER_SECOND);
        }
    }
}

static void sac_fixed_update(
        level_t *level,
        entity_t *ent,
        f32 dt) {
    if (v3_eqv(ent->fly_dir, v3_of(0))) {
        return;
    }

    const f32 base_speed = 80.0f;

    f32 speed = base_speed * dt;

    ent->vel_xyz = v3_add(ent->vel_xyz, v3_scale(ent->fly_dir, speed));
}

static void sac_models(
        level_t *l,
        entity_t *e,
        DYNLIST(model_t) *models) {
    FRAME_LOCAL model_id_t model_ids[3];
    FRAME_LOCAL tex_id_t tex_id;
    FRAME_LOCAL tex_id_t overlay_id;
    FRAME_LOCAL_BLOCK {
        for (int i = 0; i < 3; i++) {
            model_ids[i] = model_atlas_lookupf("sac%d", i)->id;
        }
        tex_id = tex_atlas_lookup("x_pink");
        overlay_id =
            vtext_get_or_create(
                "sac_overlay",
                "$ITYEARNING FOR ESCAPE ");
    }

    // goes from SAC_EXPLODE_SECS -> 0.0f
    const f32 secs_to_burst =
        max(SAC_EXPLODE_SECS - secs_since_tick(e->spawn_tick), 0);

    // compute burst effect for flashing
    f32 burst_effect;
    {
        // charge goes to 0 -> 60 as secs_to_burst goes from 1.5 -> 0.0
        const int charge = max(1.5f - secs_to_burst, 0.0f) * 60;

        // divisor gets smaller as ticks closeness increases
        int divisor = (1 << 6) >> max(charge / 20, 3);
        burst_effect = powf(sinf(PI_2 * (charge / (f32) divisor)), 4.0f);
    }

    const v3 center = entity_center(l, e);

    m4 tr = m4_identity();
    tr = m4_translate(tr, center);

    rand_t rand = rand_create(e->id);
    tr = m4_rotate(tr, g->time.total_scaled_s / 0.8f, rand_v3_dir(&rand));

    // pulsate
    {
        const f32 pulse =
            powf(sinf(((g->time.total_scaled_s * TAU) / 1.1f) + e->id), 4.0f);
        tr = m4_mul(tr, m4_scale_make(v3_of(1.0f + (0.2f * pulse) + (0.3f * burst_effect))));
    }

    v4 extra_bloom = v4_of(0.1f, 0.8f, 0.1f, 0.15f);
    v3 extra_light =
        v3_scale(
            v3_of(0.1f, 0.8f, 0.1f),
            0.15f + (burst_effect * 0.5f));

    *dynlist_push(*models) =
        (model_t) {
            .id = lptr_from(e),
            .data = model_atlas_get(model_ids[e->id % 3]),
            .tex = tex_id,
            .overlay_alpha = 0.5f,
            .tex_overlay = overlay_id,
            .flags = 0
                | MRF_OVERLAY_SCROLL_V
                | MRF_ENEMY,
            .hsv = v3_of(0.45, 0.25, -0.4),
            .transform = tr,
            .extra_bloom = extra_bloom,
            .extra_light  = extra_light,
            .corpse_tick = e->corpse_tick,
        };
}

static bool sac_collides(
        const level_t *l,
        const entity_t *ent,
        const entity_t *other) {
    return other->itype == ENTITY_TYPE_PLAYER
        || other->ptype->is_projectile;
}

static void sac_move_on_hit_fn(
        level_t *level,
        entity_t *ent,
        const trace_hit_t *hit) {
    if (hit->type != LT_ENTITY) { return; }
    if (hit->entity.ptr->itype == ENTITY_TYPE_SAC) { return; }

    sac_explode(level, ent);
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_SAC,
    (entity_type_t) {
        .is_enemy = true,
        .has_model = true,
        .is_damageable = true,
        .distributed_fixed_update = true,
        .bounds = {
            .radius = 0.35f,
            .height = 0.35f,
            .hitbox_radius = 0.35f,
            .hitbox_height = 0.35f,
        },
        .drag_air = v3_const(1.0f),
        .drag_floor = v3_const(1.0f),
        .mass = 0.0f,
        .max_health = 1.0f,
        .models_fn = sac_models,
        .tick_fn = sac_tick,
        .fixed_update_fn = sac_fixed_update,
        .move_on_hit_fn = sac_move_on_hit_fn,
        .collides_fn = sac_collides,
    })
