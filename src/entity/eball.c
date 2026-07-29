#include "level/entity.h"
#include "game.h"

#define EBALL_ATTRACT_DISTANCE 12.0f

static void eball_fixed_update(level_t *level, entity_t *ent, f32 dt) {
    const entity_t *target = g->player;

    if (!target) { return; }

    const f32 dist2 = v3_distance2(ent->pos_xyz, target->pos_xyz);

    if (secs_since_tick(target->last_shot) <= 0.75f) {
        // only attact when not shooting
        return;
    } else if (dist2 > EBALL_ATTRACT_DISTANCE * EBALL_ATTRACT_DISTANCE) {
        // not close enough
        return;
    } else if (isnan(dist2) || isinf(dist2) || dist2 < 0.00001f) {
        entity_destroy(level, ent);
        return;
    }

    const f32 closeness =
        ease_quart_out(
            satf(1.0f - (sqrtf(dist2) / EBALL_ATTRACT_DISTANCE)));

    v3 dir =
        v3_dir(
            ent->pos_xyz,
            v3_add(target->pos_xyz, v3_of(0, 0, 0.25f)));

    if (!v3_isvalid(dir)) {
        dir = v3_of(0);
    }

    const f32 speed = 45.0f * closeness * dt;

    // accelerate towards target
    ent->vel_xyz = v3_add(ent->vel_xyz, v3_scale(dir, speed));
}

static void eball_on_hit(level_t *level, entity_t *ent, entity_t *other) {
    ASSERT_DEBUG(other->itype == ENTITY_TYPE_PLAYER);
    ASSERT_DEBUG(other == g->player);

    other->health = min(other->health + 10.0f, other->ptype->max_health);
    other->last_heal = g->tick;
    renderer_add_tint(
        &(screen_tint_t) {
            .duration = 0.15f,
            .fade = true,
            .color = v4_of(1.2f, 2.0f, 1.2f, 0.2f)
        });
    entity_destroy(level, ent);
}

static bool eball_collides(
        const level_t *level,
        const entity_t *ent,
        const entity_t *other) {
    return other->itype == ENTITY_TYPE_PLAYER;
}

static void eball_sprite(
        level_t *level,
        entity_t *ent,
        sprite_inst_desc_t *desc) {
    desc->extra_bloom = v4_of(1.5f, 0.2f, 0.3f, 2.0f);
    desc->extra_light = v3_of(1.0f, 0.2f, 0.3f);
}

static light_desc_t eball_light(const level_t *level, const entity_t *ent) {
    return (light_desc_t) {
        .id = LIGHT_ID_FROM(LIGHT_TYPE_ENTITY, ent->id),
        .pos = entity_center(level, ent),
        .params = {
            .attenuation = 1.2f,
            .power = 1.0f,
            .color = v3_of(1.0f, 0.2f, 0.3f),
            .c1 = 1.0f,
            .c2 = 3.0f,
            .ambient = 0.0,
            .flags = LIGHT_FLAG_NO_SHADOWS,
        },
    };
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_EBALL,
    (entity_type_t) {
        .sprite = "e_energy_ball",
        .has_light = true,
        .bounds = {
            .radius = 0.2f,
            .height = 0.2f,
            .hitbox_radius = 0.2f,
            .hitbox_height = 0.2f,
        },
        .fixed_update_fn = eball_fixed_update,
        .on_hit_fn = eball_on_hit,
        .collides_fn = eball_collides,
        .light_fn = eball_light,
        .sprite_fn = eball_sprite,
        .drag_air = v3_const(0.15f),
        .drag_floor = v3_const(0.25f),
        .gravity = v3_of(0, 0, -0.05f),
        .step_height = 0.0f,
        .mass = 0.0f,
    })
