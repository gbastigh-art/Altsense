#include "entity/enemy.h"
#include "game.h"
#include "gfx/model.h"
#include "gfx/tex_atlas.h"
#include "level/entity.h"
#include "level/particle.h"

#define TURRET_EYE_DISTANCE 1.0f

#define TURRET_MAX_SIGHTLINE 16.0f

#define TURRET_CHARGE_TIME_S 0.5f

static void turret_tick(level_t *level, entity_t *ent) {
    enemy_pre_tick(level, ent);

    if (enemy_check_death_and_explode(level, ent)) {
        return;
    }

    const entity_t *target = g->player;

    if (secs_since_tick(ent->target_last_seen_tick) >= 15.0f) {
        // forget target after 15 seconds
        ent->target_last_seen_tick = 0;
    }

    if (target && ticks_since_tick(ent->target_last_update_tick) > 10) {
        enemy_update_sightline(level, ent, ent->turret_eye_pos, v3_of(0.25f));
    }

    // attack if:
    // * we aren't already starting an attack
    // * we've seen the target for a bit
    // * it's been a bit since we've attacked
    // * the shot within angle a reasonable angle
    // * check if direction is possible for us
    // * + random chance to offset multiple turrets
    if (ent->turret_attack_start_tick == 0
        && secs_since_tick(ent->target_last_seen_tick) <= 0.25f
        && secs_since_tick(ent->last_attack_tick) >= 2.0f
        && acosf(v3_dot(ent->turret_eye_dir, ent->target_last_seen_dir)) < PI_4
        && rand_chance(&g->rand, 0.6f)) {
        ent->turret_attack_start_tick = g->tick;
    }

    // charge attack -> shoot after timer
    if (ent->turret_attack_start_tick != 0
        && secs_since_tick(ent->turret_attack_start_tick)
            >= TURRET_CHARGE_TIME_S) {
        ent->turret_attack_start_tick = 0; // reset timer
        ent->last_attack_tick = g->tick;

        for (int i = 0; i < 10; i++) {
            v3 dir = rand_v3_disc(&g->rand, ent->turret_eye_dir);
            dir = v3_slerp(dir, ent->turret_eye_dir, 0.5f);
            particle_new(
                level,
                ent->pos,
                &(particle_t) {
                    .type = PARTICLE_TYPE_ENEMY_BULLET,
                    .duration =
                        (1.0f + rand_f32(&g->rand, -0.3f, 0.3f))
                            * TICKS_PER_SECOND,
                    .color = v4_of(1.0f), // TODO
                    .pos_xyz = v3_add(ent->turret_eye_pos, v3_scale(dir, 0.5f)),
                    .vel_xyz = v3_scale(dir, 3.0f),
                    .enemy_bullet.normal = ent->turret_eye_dir,
                });
        }

        entity_new(
            g->level,
            &(entity_t) {
                .itype = ENTITY_TYPE_ENEMY_BULLET,
                .pos_xyz = ent->turret_eye_pos,
                .vel_xyz = v3_scale(ent->turret_eye_dir, 10.0f),
                .projectile = {
                    .damage = 5.0f,
                    .dir = ent->turret_eye_dir,
                    .source = lptr_from(ent),
                    .fake_origin = ent->turret_eye_pos,
                    .fake_dir = ent->turret_eye_dir,
                },
            });
    }

    enemy_post_tick(level, ent);
}

static void compute_pos_and_dir(level_t *level, entity_t *ent, f32 dt) {
    if (v3_eqv_eps(ent->turret_dir, v3_of(0)) || !v3_isvalid(ent->turret_dir)) {
        ent->turret_dir = v3_of(1, 0, 0);
    }

    const v3 normal = entity_attach_surface_normal(level, ent);

    v3 target_dir;
    if (secs_since_tick(ent->target_last_seen_tick) <= 5.0f) {
        target_dir = ent->target_last_seen_dir;
    } else {
        rand_t rand = rand_create((g->tick + (ent->id * 17)) / 200);
        target_dir = rand_v3_cone(&rand, normal, PI_3);
    }

    v3 dir_wish = target_dir;
    dir_wish = v3_clamp_cone(dir_wish, normal, PI_4);
    ent->turret_dir =
        v3_dtslerp(
            ent->turret_dir,
            dir_wish,
            2.0f,
            dt);

    // desired turret eye pos...
    v3 eye_pos_wish =
        v3_add(ent->pos_xyz, v3_scale(ent->turret_dir, TURRET_EYE_DISTANCE));

    // oscillate a bit
    const f32 oscillate = 0.25f * sinf(time_s() + (ent->id * 17));
    if (ent->attach.sector) {
        rand_t rand = rand_create(ent->id);
        const v2 dir = rand_v2_dir(&rand);
        eye_pos_wish =
            v3_add(
                eye_pos_wish,
                v3_scale(v3_of(dir, 0.0f), oscillate));
    } else {
        eye_pos_wish.z += oscillate; 
    }

    if (v3_distance(ent->turret_eye_pos, eye_pos_wish) > TURRET_EYE_DISTANCE) {
        ent->turret_eye_pos = eye_pos_wish;
    } else {
        ent->turret_eye_pos =
            v3_dtlerp(
                ent->turret_eye_pos,
                eye_pos_wish,
                3.0f,
                dt);
    }

    // compute eye direction independently
    v3 eye_dir_wish = target_dir;
    eye_dir_wish = v3_clamp_cone(eye_dir_wish, normal, PI * 0.52f);
    ent->turret_eye_dir =
        v3_dtslerp(
            ent->turret_eye_dir,
            eye_dir_wish,
            3.2f,
            dt);
}

static void turret_fixed_update(level_t *level, entity_t *ent, f32 dt) {
   compute_pos_and_dir(level, ent, dt);
}

static void turret_models(
        level_t *level,
        entity_t *ent,
        DYNLIST(model_t) *models) {
    if (g->mode != GAMEMODE_GAME) {
        compute_pos_and_dir(level, ent, g->time.frame.dt);
    }

    v3 hsv = v3_of(0.7f, -0.4f, 0.0f);

    v3 extra_light = v3_of(0.0f);
    v4 extra_bloom = v4_of(1.0f, 0.1f, 0.1f, 0.05f);


    if (ent->turret_attack_start_tick != 0) {
        // 0..1
        const f32 charge_t =
            secs_since_tick(ent->turret_attack_start_tick)
                / TURRET_CHARGE_TIME_S;

        f32 charge = 0.0f;

        // base charge
        charge += 0.3f * charge_t;
    
        rand_t rand = rand_create((ent->id * 17) + (g->tick / 3));
        
        // flash erratically with more intensity over time
        charge += 0.7f * rand_f32(&rand, 0.0f, 2.0f * charge_t);

        extra_light = v3_adds(extra_light, charge);
        extra_bloom.a += charge;
    }

    // eye
    {
        m4 tr = m4_identity();
        tr = m4_translate(tr, ent->turret_eye_pos);
        tr = m4_mul(tr, m4_rotate_make_dir(ent->turret_eye_dir));
        tr = m4_mul(tr, m4_scale_make(v3_of(0.75f)));
        *dynlist_push(*models) =
            (model_t) {
                .id = lptr_from(ent),
                .data = model_atlas_lookupf("turret_eye"),
                .tex = tex_atlas_lookup("turret_eye"),
                .tex_overlay = tex_atlas_lookup("x_missing"),
                .overlay_alpha = 0.5f,
                .flags = 0
                    | MRF_OVERLAY_SCROLL_V
                    | MRF_ENEMY,
                .transform = tr,
                .extra_light = extra_light,
                .extra_bloom = extra_bloom,
                .hsv = hsv,
                .corpse_tick = ent->corpse_tick,
            };
    }

    // cord
    {
        m4 tr = m4_identity();
        tr = m4_translate(tr, ent->pos_xyz);
        tr = m4_mul(tr, m4_rotate_make_dir(v3_dir(ent->pos_xyz, ent->turret_eye_pos)));
        tr = m4_mul(tr, m4_translate_make(v3_of(0.25f, 0.0f, 0.0f)));
        *dynlist_push(*models) =
            (model_t) {
                .id = lptr_from(ent),
                .data =
                    model_atlas_lookupf(
                        "turret_cord$pulse$%d",
                        (g->tick / 5) % 12),
                .tex = tex_atlas_lookup("turret_eye"),
                .tex_overlay = tex_atlas_lookup("x_missing"),
                .overlay_alpha = 0.5f,
                .flags = 0
                    | MRF_OVERLAY_SCROLL_V,
                .transform = tr,
                .extra_light = extra_light,
                .extra_bloom = extra_bloom,
                .hsv = hsv,
                .corpse_tick = ent->corpse_tick,
            };
    }

    // lump
    {
        const v3 normal = entity_attach_surface_normal(level, ent);
        m4 tr = m4_identity();
        tr = m4_translate(tr, ent->pos_xyz);

        if (!ent->attach.sector) {
            tr =
                m4_translate(
                    tr,
                    v3_scale(
                        normal,
                        -(entity_radius(level, ent) / 2.0f) + 0.01f));
        }

        tr = m4_mul(tr, m4_rotate_make_from_to(v3_of(0, 0, 1), normal));
        *dynlist_push(*models) =
            (model_t) {
                .id = lptr_from(ent),
                .data = model_atlas_lookup("turret_lump"),
                .tex = tex_atlas_lookup("turret_eye"),
                .tex_overlay = tex_atlas_lookup("x_missing"),
                .overlay_alpha = 0.5f,
                .flags = 0
                    | MRF_OVERLAY_SCROLL_V,
                .transform = tr,
                .extra_light = extra_light,
                .extra_bloom = extra_bloom,
                .hsv = hsv,
                .corpse_tick = ent->corpse_tick,
            };
    }
}

static entity_bounds_t turret_bounds(const level_t*, const entity_t *e) {
    entity_bounds_t bounds = e->ptype->bounds;
    bounds.origin = v3_add(e->turret_eye_pos, v3_of(0.0f, 0.0f, -(bounds.height / 2.0f)));
    return bounds;
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_TURRET,
    (entity_type_t) {
        .is_enemy = true,
        .is_damageable = true,
        .has_model = true,
        .is_attach = true,
        .max_health = 30.0f,
        .bounds = {
            .radius = 0.2f,
            .height = 0.9f,
            .hitbox_radius = 0.9f,
            .hitbox_height = 0.9f,
        },
        .bounds_fn = turret_bounds,
        .fixed_update_fn = turret_fixed_update,
        .tick_fn = turret_tick,
        .models_fn = turret_models,
    })
