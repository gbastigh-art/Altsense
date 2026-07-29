#include "frame_local.h"
#include "game.h"
#include "gfx/model.h"
#include "level/entity.h"
#include "level/particle.h"
#include "util/cube.h"
#include "vtext.h"

#define MODEL_NAME "enemy_bullet_model"

#define ENEMY_BULLET_RGB (v3_of(1.0f, 0.9f, 0.75f))

static void enemy_bullet_hit(
        level_t *level,
        entity_t *ent,
        const trace_3d_t *trace,
        const trace_hit_t *hit,
        entity_t *other) {
    if (other) {
        entity_try_damage(
            other,
            &(entity_damage_desc_t) {
                .amount = ent->projectile.damage,
                .knockback = 30.0f,
                .dir = ent->projectile.dir,
            });
    }

    v3 normal = v3_of(0, 0, 1);
    if (hit) {
        normal = hit->normal;
    } else if (other) {
        normal = v3_dir(other->pos_xyz, ent->pos_xyz);
    }

    const v3 pos =
        v3_add(
            ent->pos_xyz,
            v3_scale(ent->projectile.dir, -0.01f));

    for (int i = 0, n = rand_n(&g->rand, 3, 5); i < n; i++) {
        v3 vel =
            v3_scale(
                rand_v3_cone(&g->rand, normal, PI_3),
                rand_f32(&g->rand, 5.0f, 12.0f));

        if (normal.z > 0.0f) {
            vel.z += 2.0f;
        }

        particle_new(
            level,
            v2_from(pos),
            &(particle_t) {
                .type = PARTICLE_TYPE_SPARK,
                .duration =
                    (0.8f + rand_f32(&g->rand, -0.3f, 0.3f))
                        * TICKS_PER_SECOND,
                .color = v4_of(ENEMY_BULLET_RGB, 1),
                .pos_xyz = pos,
                .vel_xyz = vel,
            });
    }

    for (int i = 0, n = rand_n(&g->rand, 10, 15); i < n; i++) {
        const v3 dir = rand_v3_cone(&g->rand, normal, PI_3);
        particle_new(
            level,
            ent->pos,
            &(particle_t) {
                .type = PARTICLE_TYPE_ENEMY_BULLET,
                .duration =
                    (1.0f + rand_f32(&g->rand, -0.3f, 0.3f))
                        * TICKS_PER_SECOND,
                .color = v4_of(ENEMY_BULLET_RGB, 1.0f),
                .pos_xyz = v3_add(ent->pos_xyz, v3_scale(dir, 0.25f)),
                .vel_xyz = v3_scale(dir, 3.0f),
                .enemy_bullet.normal = normal,
            });
    }

    entity_destroy(level, ent);
}

static void enemy_bullet_tick(level_t *level, entity_t *ent) {
    if (rand_chance(&g->rand, 0.25f)) {
        v3 dir = rand_v3_disc(&g->rand, ent->projectile.dir);
        dir = v3_slerp(dir, ent->projectile.dir, 0.25f);
        particle_new(
            level,
            ent->pos,
            &(particle_t) {
                .type = PARTICLE_TYPE_ENEMY_BULLET,
                .duration =
                    (1.0f + rand_f32(&g->rand, -0.3f, 0.3f))
                        * TICKS_PER_SECOND,
                .color = v4_of(ENEMY_BULLET_RGB, 1.0f),
                .pos_xyz = v3_add(ent->pos_xyz, v3_scale(dir, 0.25f)),
                .vel_xyz = v3_scale(dir, 3.0f),
                .enemy_bullet.normal = ent->projectile.dir,
            });
    }
}

static void enemy_bullet_on_hit(level_t *level, entity_t *ent, entity_t *other) {
    enemy_bullet_hit(level, ent, NULL, NULL, other);
}

static bool enemy_bullet_collides(
        const level_t *level,
        const entity_t *ent,
        const entity_t *other) {
    if (other->itype == ENTITY_TYPE_ENEMY_BULLET) {
        return false;
    }

    return !lptr_eq(lptr_from(other), ent->projectile.source);
}

static void enemy_bullet_models(
        level_t *level,
        entity_t *ent,
        DYNLIST(model_t) *models) {
    const model_data_t *model;

    if (!model_atlas_contains(MODEL_NAME)) {
        DYNLIST(u16) indices = dynlist_create(u16, &g->frame_arena);
        DYNLIST(model_vertex_t) vertices =
            dynlist_create(model_vertex_t, &g->frame_arena);

        for (int face = 0; face < 6; face++) {
            const int base = dynlist_size(vertices);
            for (int i = 0; i < 4; i++) {
                *dynlist_push(vertices) = (model_vertex_t) {
                    .pos =
                        CUBE_VERTICES[
                            CUBE_INDICES[face][CUBE_UNIQUE_INDICES[i]]],
                    .normal = CUBE_NORMALS[face],
                    .uv = CUBE_UVS[face][i],
                };
            }

            for (int i = 0; i < 6; i++) {
                *dynlist_push(indices) = base + CUBE_FACE_INDICES[i];
            }
        }

        model = model_atlas_insert_raw(MODEL_NAME, &indices, &vertices);
    } else {
        model = model_atlas_lookup(MODEL_NAME);
    }

    ASSERT_DEBUG(model);

    FRAME_LOCAL tex_id_t overlay_id;
    FRAME_LOCAL_BLOCK {
        overlay_id = vtext_get_or_create("enemy_bullet_overlay", "$ITOUCH! ");
    }

    v3 size = v3_of(0.5f);

    rand_t rand = rand_create(ent->id * 13);
    const v3 rot_axis = rand_v3_dir(&rand);

    m4 tr = m4_identity();
    tr = m4_translate(tr, ent->pos_xyz);
    tr = m4_mul(tr, m4_rotate_make(time_s() * 1.5f, rot_axis));
    //tr = m4_translate(tr, v3_of(size.x * +0.5f, 0.0f, 0.0f));
    //tr = m4_mul(tr, m4_rotate_make_dir(ent->projectile.dir));
    //tr = m4_translate(tr, v3_of(size.x * -0.5f, 0.0f, 0.0f));
    tr = m4_scalev(tr, size);
    *dynlist_push(*models) =
        (model_t) {
            .id = lptr_from(ent),
            .data = model,
            .transform = tr,
            .tex = { .index = 0 },
            .hsv = v3_of(0, 0, 1),
            .tex_overlay = overlay_id,
            .overlay_alpha = 0.9f,
            .flags = 0
                | MRF_OVERLAY_SCROLL_V
                | MRF_OVERLAY_POST,
            .extra_light = v3_of(1.5f, 0.8f, 0.8f),
            .extra_bloom = v4_of(1.0f, 0.8f, 0.8f, 1.0f),
            .spawn_time = ns_to_secs(ent->spawn_ns),
        };
}

static light_desc_t enemy_bullet_light(const level_t *level, const entity_t *ent) {
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

static bool enemy_bullet_move_trace_resolve(
        level_t *level,
        entity_t *ent,
        trace_3d_t *trace,
        const trace_hit_t *hit,
        trace_resolve_result_e *res)  {
    enemy_bullet_hit(
        level,
        ent,
        trace,
        hit,
        hit->type == LT_ENTITY ? hit->entity.ptr : NULL);
    *res = TRACE_RESOLVE_STOP;
    return true;
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_ENEMY_BULLET,
    (entity_type_t) {
        .has_model = true,
        .has_light = true,
        .is_projectile = true,
        .bounds = {
            .radius = 0.4f,
            .height = 0.4f,
            .hitbox_radius = 0.4f,
            .hitbox_height = 0.4f,
        },
        .tick_fn = enemy_bullet_tick,
        .on_hit_fn = enemy_bullet_on_hit,
        .collides_fn = enemy_bullet_collides,
        .light_fn = enemy_bullet_light,
        .models_fn = enemy_bullet_models,
        .move_trace_resolve_fn = enemy_bullet_move_trace_resolve,
        .drag_air = v3_const(0.0f),
        .drag_floor = v3_const(0.0f),
        .gravity = v3_of(0, 0, 0.0f),
        .step_height = 0.0f,
        .mass = 0.0f,
    })
