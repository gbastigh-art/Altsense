#include "game.h"
#include "gfx/model.h"
#include "gfx/tex_atlas.h"
#include "level/decal.h"
#include "level/entity.h"
#include "level/particle.h"
#include "level/sector.h"
#include "util/cube.h"

#define MODEL_NAME "ebullet_model"

static void bullet_hit(
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
                .knockback = ent->bullet.is_mini ? 1.0f : 2.0f,
                .dir = ent->projectile.dir,
            });
    }

    if (hit) {
        decal_new_on_trace_hit(
            level,
            hit,
            lerp(trace->org.z, trace->dst.z, hit->t),
            &(decal_t) {
                .type = DECAL_TYPE_HOLE,
                .tex = tex_atlas_lookup("d_hole"),
                .ticks = 2 * TICKS_PER_SECOND,
            });

        // trigger doors
        const side_t *hit_side = hit ? lptr_side(level, hit->ptr) : NULL;
        if (hit_side
            && hit_side->portal
            && hit_side->portal->sector
            && sector_type(hit_side->portal->sector)->is_door
            && !hit_side->portal->sector->diff_trigger_tick) {
            hit_side->portal->sector->diff_trigger_tick = g->tick;
        }
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

    for (int i = 0, n = other ? 3 : rand_n(&g->rand, 5, 10); i < n; i++) {
        v3 vel =
            v3_scale(
                rand_v3_cone(
                    &g->rand,
                    v3_slerp(normal, ent->projectile.dir, 0.4f),
                    PI_3),
                rand_f32(&g->rand, 9.0f, 12.0f));

        if (normal.z > 0.0f) {
            // bounce extra from floor :)
            vel.z += 2.0f;
        }

        particle_new(
            level,
            v2_from(pos),
            &(particle_t) {
                .type = PARTICLE_TYPE_RICOCHET,
                .duration =
                    (0.8f + rand_f32(&g->rand, -0.3f, 0.3f))
                        * TICKS_PER_SECOND,
                .color = v4_of(1.0f, 0.7f, 0.1f, 1.0f),
                .pos_xyz = pos,
                .vel_xyz = vel,
            });
    }

    entity_destroy(level, ent);
}

static void bullet_fixed_update(level_t *level, entity_t *ent, f32 dt) {

}

static void bullet_on_hit(level_t *level, entity_t *ent, entity_t *other) {
    bullet_hit(level, ent, NULL, NULL, other);
}

static bool bullet_collides(
        const level_t *level,
        const entity_t *ent,
        const entity_t *other) {
    if (other->itype == ENTITY_TYPE_BULLET) {
        return false;
    }

    return !lptr_eq(lptr_from(other), ent->projectile.source);
}

static void bullet_models(
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

    f32 fake_time_s;
    if (ent->last_portal_tick != 0) {
        fake_time_s = secs_since_tick(ent->last_portal_tick);
    } else {
        fake_time_s = ns_to_secs(g->time.total_scaled_ns - ent->spawn_ns);
    }

    // TODO: standardize this calculation somewhere
    // maybe even just keep it on a variable in the entity??
    const v3 render_pos =
        v3_add(
            ent->projectile.fake_origin,
            v3_scale(
                ent->projectile.fake_dir,
                fake_time_s * v3_norm(ent->vel_xyz)));

    v3 size = v3_of(1.0f, 0.0625f, 0.0625f);
    if (ent->bullet.is_mini) {
        size = v3_scale(size, 0.5f);
    }

    m4 tr = m4_identity();
    tr = m4_translate(tr, render_pos);
    tr = m4_translate(tr, v3_of(-0.5f, 0.0f, 0.0f));
    tr = m4_translate(tr, v3_of(size.x * +0.5f, 0.0f, 0.0f));
    tr = m4_mul(tr, m4_rotate_make_dir(ent->projectile.dir));
    tr = m4_translate(tr, v3_of(size.x * -0.5f, 0.0f, 0.0f));
    tr = m4_scalev(tr, size);
    *dynlist_push(*models) =
        (model_t) {
            .id = lptr_from(ent),
            .data = model,
            .tex = { .index = 0 },
            .flags = MRF_BULLET,
            .hsv = v3_of(0, 0, 1),
            .transform = tr,
            .extra_light = v3_of(1),
            .extra_bloom = v4_of(1),
            .spawn_time = ns_to_secs(ent->spawn_ns),
        };
}

static light_desc_t bullet_light(const level_t *level, const entity_t *ent) {
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

static bool bullet_move_trace_resolve(
        level_t *level,
        entity_t *ent,
        trace_3d_t *trace,
        const trace_hit_t *hit,
        trace_resolve_result_e *res)  {
    bullet_hit(
        level,
        ent,
        trace,
        hit,
        hit->type == LT_ENTITY ? hit->entity.ptr : NULL);
    *res = TRACE_RESOLVE_STOP;
    return true;
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_BULLET,
    (entity_type_t) {
        .has_model = true,
        .has_light = true,
        .is_projectile = true,
        .bounds = {
            .radius = 0.2f,
            .height = 0.2f,
            .hitbox_radius = 0.2f,
            .hitbox_height = 0.2f,
        },
        .fixed_update_fn = bullet_fixed_update,
        .on_hit_fn = bullet_on_hit,
        .collides_fn = bullet_collides,
        .light_fn = bullet_light,
        .models_fn = bullet_models,
        .move_trace_resolve_fn = bullet_move_trace_resolve,
        .drag_air = v3_const(0.0f),
        .drag_floor = v3_const(0.0f),
        .gravity = v3_of(0, 0, 0.0f),
        .step_height = 0.0f,
        .mass = 0.0f,
    })
