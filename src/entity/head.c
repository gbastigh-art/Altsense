#include "game.h"
#include "gfx/model.h"
#include "gfx/tex_atlas.h"
#include "level/entity.h"
#include "entity/enemy.h"
#include "level/level.h"
#include "level/sector.h"
#include "vtext.h"

#define MIN_FLOOR_DIST 0.3f

static void head_fixed_update(
        level_t *level,
        entity_t *ent,
        f32 dt) {
    if (!ent->attach.disabled) { return; }

    if (secs_since_tick(ent->target_last_update_tick) > 0.15f) {
        enemy_update_sightline(level, ent, v3_of(0), v3_of(0));
    }

    if (secs_since_tick(ent->target_last_seen_tick) >= 3.0f) {
        // can't move towards player we can't see
        return;
    }

    v3 dir;
    f32 dist;
    if (ent->target_through_portal) {
        dir = ent->target_last_seen_dir;
        dist = ent->target_last_seen_dist;
    } else {
        dir = v3_dir(entity_center(level, ent), entity_center(level, g->player));
        dir = v3_valid_or(dir, v3_of(1, 0, 0));
        dist = v3_distance(ent->pos_xyz, g->player->pos_xyz);
    }

    // TODO TODO TODO: check that model lines up after detached

    // face 2D direction
    entity_face_dir(level, ent, v3_normalize_of(v2_from(dir), 0), 0.9f, dt);

    // move towards 3D location
    f32 speed = 15.0f + (30.0f * (1.0f - satf(dist / 16.0f)));
    ent->vel_xyz = v3_add(ent->vel_xyz, v3_scale(dir, speed * dt));

    const f32 goal_z = entity_center(level, g->player).z;
    ent->vel_xyz = v3_add(ent->vel_xyz, v3_of(0.0f, 0.0f, 1.0f * sign(goal_z - ent->z) * dt));

    // force minimum z
    f32 min_z = level_point_zs(level, ent->pos).z0 + MIN_FLOOR_DIST;
    if (sector_type(ent->sector)->is_liquid) {
        min_z += ent->sector->liquid_offset;
    }

    ent->z = max(ent->z, min_z);
}

static v3 head_gravity(const level_t*, const entity_t *ent) {
    v3 gravity = ent->ptype->gravity;

    if (ent->attach.disabled) {
        // goes to 0 as time increases
        f32 factor =
            1.0f - satf(secs_since_tick(ent->head_shot_tick) / 0.5f);
        gravity = v3_scale(gravity, factor);
    }

    return gravity;
}

static void head_models(
        level_t *level,
        entity_t *ent,
        DYNLIST(model_t) *models) {
    v3 hsv = v3_of(0.2f, 0.2f, 0.0f);

    v3 extra_light = v3_of(0.0f);
    v4 extra_bloom = v4_of(1.0f, 0.7f, 0.1f, 0.30f);
    extra_light = v3_scale(v3_from(extra_bloom), 0.25f);

    // do flash effect on shoot
    if (ent->head_shot_tick != 0) {
        f32 factor = 1.0f - (secs_since_tick(ent->head_shot_tick) / 0.33f);
        factor = ease_cubic_out(factor);

        if (factor > 0.0f) {
            extra_bloom.a += factor * 0.7f;
            extra_light = v3_scale(v3_from(extra_bloom), 0.05f + factor);
        }
    }

    m4 tr = m4_identity();
    tr = m4_translate(tr, ent->pos_xyz);

    if (ent->attach.disabled) {
        f32 float_z = 0.3f;
        float_z += 0.22f * fast_sin((g->time.total_scaled_s * 2.0f) + ent->id);
        tr = m4_mul(tr, m4_translate_make(v3_of(0, 0, float_z)));
        tr = m4_mul(tr, m4_rotate_make(fast_atan2(ent->dir.y, ent->dir.x), V3_AXIS_Z));
    } else {
        const v3 normal = entity_attach_surface_normal(level, ent);

        if (!ent->attach.sector) {
            tr =
                m4_translate(
                    tr,
                    v3_scale(
                        normal,
                        -(entity_radius(level, ent) / 2.0f) + 0.01f));
        }

        tr = m4_mul(tr, m4_rotate_make_from_to(v3_of(1, 0, 0), normal));
        tr = m4_mul(tr, m4_translate_make(v3_of(0.6, 0, 0)));
    }

    *dynlist_push(*models) =
        (model_t) {
            .id = lptr_from(ent),
            .data = model_atlas_lookup("head"),
            .tex = tex_atlas_lookup("head"),
            .tex_overlay = vtext_get_or_create("head_overlay", "$IT$30OMINOUS TEXT "),
            .overlay_alpha = 0.5f,
            .flags = 0
                | MRF_OVERLAY_SCROLL_V,
            .transform = tr,
            .extra_light = extra_light,
            .extra_bloom = extra_bloom,
            .hsv = hsv,
        };
}

static entity_bounds_t head_bounds(const level_t *level, const entity_t *ent) {
    entity_bounds_t bounds = { .height = 1.3f };

    if (ent->attach.disabled) {
        bounds.radius = 0.7f;
    } else {
        bounds.radius = 1.3f;
    }

    bounds.origin = ent->pos_xyz;

    if (ent->attach.disabled) {

    } else {
        const v3 normal = entity_attach_surface_normal(level, ent);
        bounds.origin = v3_sub(bounds.origin, v3_scale(normal, 0.2));
    }

    return bounds;
}

static bool head_collides(
        const level_t*,
        const entity_t *ent,
        const entity_t *other) {
    if (ent->attach.disabled) {
        return other->itype == ENTITY_TYPE_PLAYER
            || other->itype == ENTITY_TYPE_HEAD;
    } else {
        return other->itype == ENTITY_TYPE_BULLET;
    }
}

static void head_on_hit(
        level_t *level,
        entity_t *ent,
        entity_t *other) {
    if (ent->attach.disabled) {
        if (other->itype != ENTITY_TYPE_PLAYER) {
            return;
        }

        entity_destroy(level, ent);
        renderer_add_tint(
            &(screen_tint_t) {
                .duration = 0.66f,
                .fade = true,
                .color = v4_of(1.4f, 0.9f, 0.2f, 0.75f),
            });
    } else {
        const v3 normal = entity_attach_surface_normal(level, ent);

        if (ent->attach.sector) {
            ent->dir = v3_dir(ent->pos_xyz, g->player->pos_xyz);
        } else {
            ent->dir = normal;
        }

        const v3 new_pos = v3_add(ent->pos_xyz, v3_scale(normal, 0.1f));
        if (entity_try_move(level, ent, v2_from(new_pos))) {
            ent->z = new_pos.z;
        }

        v3 pop_vel = v3_scale(ent->dir, 5.0f);

        if (!ent->attach.sector) {
            pop_vel = v3_add(pop_vel, v3_scale(rand_v3_cone(&g->rand, ent->dir, PI_3), 10.0f));
            pop_vel = v3_add(pop_vel, v3_of(0.0f, 0.0f, 3.0f));
        }

        ent->vel_xyz =
            v3_add(
                ent->vel_xyz,
                pop_vel);

        ent->attach.disabled = true;
        ent->head_shot_tick = g->tick;
    }
}

static light_desc_t head_light(const level_t *level, const entity_t *ent) {
    light_desc_t desc = {
        .id = LIGHT_ID_FROM(LIGHT_TYPE_ENTITY, ent->id),
        .params = {
            .attenuation = 2.5f,
            .power = 0.5f - 0.2f * fast_sin((g->time.total_s * 2.0f) + ent->id),
            .color = v3_of(1.0f, 0.6f, 0.2f),
            .c1 = 1.0f,
            .c2 = 5.0f,
            .ambient = 0.0,
        },
    };

    if (ent->attach.disabled) {
        // no shadows while moving
        desc.params.flags |= LIGHT_FLAG_NO_SHADOWS;
        desc.pos = entity_center(level, ent);
    } else {
        const v3 normal = entity_attach_surface_normal(level, ent);
        desc.pos = v3_add(entity_center(level, ent), v3_scale(normal, 0.4f));
    }

    return desc;
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_HEAD,
    (entity_type_t) {
        .has_model = true,
        .has_light = true,
        .is_attach = true,
        .fixed_update_fn = head_fixed_update,
        .gravity_fn = head_gravity,
        .models_fn = head_models,
        .bounds_fn = head_bounds,
        .collides_fn = head_collides,
        .on_hit_fn = head_on_hit,
        .light_fn = head_light,
        .gravity = v3_of(0, 0, -1),
        .drag_air = v3_of(0.15f),
        .drag_floor = v3_of(0.15f),
        .mass = 0.0f,
    })

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_HEAD_POINT,
    (entity_type_t) {
        .is_attach = true,
        .is_edit_only = true,
        .has_model = true,
        .models_fn = entity_edit_cube_models,
        .edit_cube_hsv = v3_of(0.4f, 0.0f, 0.0f),
    })
