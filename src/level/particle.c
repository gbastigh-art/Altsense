#include "level/particle.h"
#include "level/decal.h"
#include "level/level.h"
#include "level/lptr.h"
#include "level/entity.h"
#include "level/sector.h"
#include "gfx/renderer.h"
#include "game.h"
#include "trace.h"
#include "util/math.h"

// particles get updated every N (= PARTICLE_UPDATE_SPREAD) ticks at a scale of
// N to reduce fixed update load
#define PARTICLE_UPDATE_SPREAD 4

// TODO: maybe have an update spread of 10 if particles should feel more visceral?

#define FLOATER_MAX_RADIUS 24.0f
#define FLOATER_MIN_RADIUS 0.15f
#define MAX_FLOATERS_PER_TICK 100

#define TARGET_FLOATER_DENSITY 0.01f

void particles_tick(level_t *level) {
    if (g->debug.pause_particles) { return; }

    int count = 0;

    // compute total volume within current player radius
    // TODO: go across portal boundaries
    DYNLIST(subsector_t*) subs = dynlist_create(subsector_t*, &g->frame_arena);
    level_subsectors_in_radius(
        level,
        v2_from(g->cam.pos),
        FLOATER_MAX_RADIUS,
        &subs);

    f32 total_volume = 0.0f;
    DYNLIST(f32) volumes = dynlist_create(f32, &g->frame_arena);
    dynlist_resize(volumes, dynlist_size(subs));

    dynlist_each(subs, it) {
        const f32 v = subsector_volume(*it.el, 2.0f);
        volumes[it.i] = v;
        total_volume += v;
    }

    const int target = clamp(total_volume * TARGET_FLOATER_DENSITY, 0, 10000);

    while (level->particle_counts[PARTICLE_TYPE_AMBIENT_FLOATER] < target
           && count < MAX_FLOATERS_PER_TICK) {
        count++;

        // get random point in radius, distributed evenly by volume
        subsector_t *sub = NULL;

        const f32 cutoff = rand_f32(&g->rand, 0.0f, total_volume);
        f32 acc = 0.0f;
        for (int i = 0, n = dynlist_size(subs); i < n; i++) {
            acc += volumes[i];

            if (cutoff <= acc) {
                sub = subs[i];
                break;
            }
        }

        // generate random point inside of subsector bounds, reject if outside
        v2 pos_xy;
        do {
            pos_xy = rand_v2(&g->rand, sub->bounds.min, sub->bounds.max);
        } while (!box2f_contains(sub->bounds, pos_xy));

        const v3 pos =
            v3_of(
                pos_xy,
                rangef_lerp(
                    level_point_zs(level, pos_xy),
                    rand_f32(&g->rand, 0.0f, 1.0f)));
        particle_new(
            level,
            v2_from(pos),
            &(particle_t) {
                .type = PARTICLE_TYPE_AMBIENT_FLOATER,
                .z = pos.z,
                .duration = -1,
                .color = v4_of(v3_of(1.0f), 1.0f),
            });
    }
}

particle_t *particle_new(level_t *level, v2 pos, const particle_t *defaults) {
    ASSERT(defaults);

    sector_t *sector = level_find_point_sector(level, pos, NULL);
    if (!sector) {
        return NULL;
    }

    particle_t *p = dynlist_push(sector->particles);
    memset(p, 0, sizeof(*p));
    if (defaults) { *p = *defaults; }
    p->id = level->next_particle_id++;
    p->pos = pos;
    p->sector = sector;
    p->start = g->tick;
    if (v3_eqv_eps(p->dir, v3_of(0))) {
        p->dir =
            v3_eqv_eps(p->vel_xyz, v3_of(0)) ?
                v3_of(1, 0, 0)
                : v3_normalize(p->vel_xyz);
    }

    level->particle_counts[p->type]++;

    return p;
}

void particle_enqueue_delete(level_t *level, particle_t *p) {
    ASSERT_DEBUG(p->sector);
    p->delete = true;
}

// moves particle, returns (potentially) moved pointer
static particle_t *particle_move(level_t *level, particle_t *p, v2 pos) {
    if (v2_eqv_eps(pos, p->pos)) {
        return p;
    }

    p->pos.x = max(pos.x, 0);
    p->pos.y = max(pos.y, 0);

    // update sector (move if sector is no longer correct)
    sector_t *new_sect = level_find_point_sector(level, p->pos, p->sector);

    if (!new_sect) {
        WARN("particle @ %p out of sector", p);
        particle_enqueue_delete(level, p);
    } else if (p->sector != new_sect) {
        // enqueue particle to be removed from the other sector on its next
        // update
        particle_t *moved = dynlist_push(new_sect->particles);
        *moved = *p;
        moved->sector = new_sect;

        // old particle is moved
        p->moved = true;

        return moved;
    }

    return p;
}

typedef struct {
    particle_t *p;
    bool did_hit;
    trace_hit_t hit;
} particle_trace_resolve_data_t;

static trace_resolve_result_e particle_trace_resolve(
        level_t *level,
        trace_3d_t *trace,
        const trace_hit_t *hit) {
    particle_trace_resolve_data_t *data = trace->userdata;
    particle_t *p = data->p;

    // check for portal
    f32 portal_angle;
    const trace_portal_result_e portal_result =
        trace_resolve_portal_3d(level, trace, hit, &portal_angle);
    switch (portal_result) {
    case TRACE_PORTAL_RESULT_IGNORE:
        break;
    case TRACE_PORTAL_RESULT_THROUGH:
    case TRACE_PORTAL_RESULT_REJECT:
        return TRACE_RESOLVE_CONTINUE;
    case TRACE_PORTAL_RESULT_STOP:
        break;
    case TRACE_PORTAL_RESULT_THROUGH_DISCONNECT:
        p->vel = v2_rotate(p->vel, portal_angle);
        return TRACE_RESOLVE_RETRY;
    }

    if (hit->type == LT_SECTOR) {
        const rangef_t zr = sector_point_zs(hit->sector.ptr, hit->swept_pos);

        p->vel_z = -p->vel_z * PARTICLE_TYPES[p->type].restitution.z;

        // move dst according to plane clamping
        if (hit->sector.plane == PLANE_TYPE_FLOOR) {
            p->grounded = true;
            trace->dst.z = zr.z0;
        } else {
            // ceil
            trace->dst.z = zr.z1 - trace->height;
        }

        data->did_hit = true;
        data->hit = *hit;
        return TRACE_RESOLVE_STOP;
    }

    // project hit movement if applicable
    const move_project_result_t project_result =
        move_project_hit_velocity(
            level,
            hit,
            (line2f_t) {
                .a = v2_from(trace->org),
                .b = v2_from(trace->dst),
            },
            p->vel,
            v2_from(PARTICLE_TYPES[p->type].restitution),
            PARTICLE_UPDATE_SPREAD * g->time.fixed.dt_scaled);

    if (project_result.changed) {
        trace->org = v3_of(project_result.movement.a, trace->org.z);
        trace->dst = v3_of(project_result.movement.b, trace->dst.z);
        p->vel = project_result.velocity;

        data->did_hit = true;
        data->hit = *hit;
        return TRACE_RESOLVE_RETRY;
    }

    trace->dst = v3_of(hit->swept_pos, lerp(trace->org.z, trace->dst.z, hit->t));

    data->did_hit = true;
    data->hit = *hit;

    return TRACE_RESOLVE_STOP;
}

void particle_fixed_update(level_t *level, particle_t *p) {
    if (g->debug.pause_particles) { return; }

    if ((p->id % PARTICLE_UPDATE_SPREAD) !=
            (g->time.fixed.count % PARTICLE_UPDATE_SPREAD)) {
        // see PARTICLE_UPDATE_SPREAD
        return;
    }

    const f32 dt = PARTICLE_UPDATE_SPREAD * g->time.fixed.dt_scaled;

    switch (p->type) {
    case PARTICLE_TYPE_SAC_GAS: {
        // fixed vel z so everything else can get dragged
        p->vel_z = 1.5f;
    } break;
    case PARTICLE_TYPE_SPAWN: {
        const int alive_ticks = ticks_since_tick(p->start);

        const v3 pos_target =
            lptr_is_valid(level, p->spawn.target) ?
                entity_center(level, lptr_entity(level, p->spawn.target))
                : v3_of(0);

        const v2
            d_to_center =
                v2_normalize(
                    v2_sub(v2_from(pos_target), p->pos)),
            tangent = v2_rotate(d_to_center, PI_2);

        p->vel = v2_add(p->vel, v2_scale(tangent, 2.5f * dt));
        p->vel_z += 8.0f * dt;

        if (alive_ticks > p->duration * 0.25f) {
            const v3 d_to_center =
                v3_normalize(v3_sub(pos_target, p->pos_xyz));

            p->vel_xyz =
                v3_add(p->vel_xyz, v3_scale(d_to_center, 100.0f * dt));
        }

        {
            const int v = p->duration * 0.75f;
            const float t = satf((alive_ticks - v) / (f32) v);
            rand_t rand = rand_create(p->id);
            const v3 d = rand_v3_dir(&rand);
            p->vel_xyz =
                v3_add(
                    p->vel_xyz,
                    v3_scale(d, lerp(0.0f, 100.0f, t) * dt));
        }
    } break;
    case PARTICLE_TYPE_AMBIENT_FLOATER: {
        // ambient floaters are transferred around when they go out of view
        const v3_diff_t diff = v3_diff(g->cam.pos, p->pos_xyz);

        if (diff.dist > FLOATER_MAX_RADIUS || diff.dist < FLOATER_MIN_RADIUS) {
            // respawn
            p->duration = 0;
        }
    };
    // --- INTENTIONAL FALLTHROUGH ---
    case PARTICLE_TYPE_FLOATER: {
        rand_t rand = rand_create((g->tick + p->id * 100) / 3);

        const v2 dir = rand_v2_dir(&rand);
        p->vel_xyz =
            v3_add(
                p->vel_xyz,
                v3_scale(v3_of(v2_scale(dir, 2.0f), 1.0f), dt));
    } break;
    case PARTICLE_TYPE_PORTAL: {
        rand_t rand =
            rand_create((g->tick + p->id * 100) / (TICKS_PER_SECOND / 2));

        const v2 dir = rand_v2_dir(&rand);
        p->vel_xyz =
            v3_add(
                p->vel_xyz,
                v3_scale(v3_of(v2_scale(dir, 3.0f), 1.0f), dt));
    } break;
    case PARTICLE_TYPE_ENEMY_BULLET: {
        // update velocity every 0.25s
        if ((ticks_since_tick(p->start) % 20) == (p->id % 20)) {
            rand_t rand = rand_create(g->tick + (p->id * 17));
            const v2 r = rand_v2_cone(&rand, v2_of(0.0f, 1.0f), PI_2);

            const v3 basis0 = v3_normalize(p->vel_xyz);
            const v3 basis1 = p->enemy_bullet.normal;
            const v3 basis2 = v3_cross(basis0, basis1);
            p->vel_xyz =
                v3_scale(
                    v3_normalize(
                        v3_add(
                            v3_scale(basis0, r.y),
                            v3_scale(basis2, r.x))),
                    v3_norm(p->vel_xyz));
        }
    } break;
    default:
    }

    const particle_type_t *type = &PARTICLE_TYPES[p->type];

    if (v3_any_nan(p->vel_xyz)) {
        WARN(
            "particle %d (type %d) has NaN velocity",
            p->id,
            p->type);
        p->vel_xyz = v3_of(0);
    }

    p->vel_xyz = v3_add(p->vel_xyz, v3_scale(type->gravity, 55.0f * dt));

    // move according to velocity
    if (fabsf(p->vel.x) < 0.001f) { p->vel.x = 0.0f; }
    if (fabsf(p->vel.y) < 0.001f) { p->vel.y = 0.0f; }

    // not grounded until proven otherwise by sector collision
    p->grounded = false;

    if (v2_eqv_eps(p->vel, v2_of(0))) {
        goto move_done;
    }

    // attempt xy-axis movement
    v3
        dt_vel = v3_scale(p->vel_xyz, dt),
        org = p->pos_xyz,
        dst = v3_add(org, dt_vel);

    particle_trace_resolve_data_t trace_data = { .p = p };

    // try to skip a fully path-traced move: will our velocity keep us in the
    // same sector, not colliding with any planes?
    const sector_t *dst_sect =
        level_find_point_sector(level, v2_from(dst), p->sector);

    if (dst_sect == p->sector) {
        const rangef_t zr = sector_point_zs(dst_sect, v2_from(dst));

        const f32 z = p->z + (p->vel_z * dt);
        if (z > zr.z0 && z < zr.z1) {
            p->pos_xyz = dst;
            goto move_done;
        }
    }

    trace_3d_t trace = {
        .org = org,
        .dst = dst,
        .types = LTF_SIDE | LTF_SECTOR,
        .flags = TRACE_FLAG_XY_THEN_Z,
        .radius = 0.0f,
        .resolve_fn = particle_trace_resolve,
        .userdata = &trace_data,
    };
    trace_3d(level, &trace);

    if (!level_find_point_sector(level, v2_from(trace.dst), p->sector)) {
        particle_enqueue_delete(level, p);
    } else if (!v3_eqv_eps(p->pos_xyz, trace.dst)) {
        p->z = trace.dst.z;
        p = particle_move(level, p, v2_from(trace.dst));
    }

    if (trace_data.did_hit) {
        switch (p->type) {
        case PARTICLE_TYPE_SAC_GAS:
        case PARTICLE_TYPE_ENEMY_BULLET: {
            particle_enqueue_delete(level, p);
        } break;
        case PARTICLE_TYPE_BLOOD: {
            // 10% chance of creating decal
            if (rand_chance(&g->rand, 0.1f)) {
                // 60% chance of being large
                const bool is_large = rand_chance(&g->rand, 0.6f);
                const v2 dir_2d = v2_normalize(p->vel);

                const int index =
                    rand_n(&g->rand, 0, ARRLEN(g->blood_textures.small) - 1);
                decal_new_on_trace_hit(
                    level,
                    &trace_data.hit,
                    p->z,
                    &(decal_t) {
                        .type = DECAL_TYPE_BLOOD,
                        .tex =
                             is_large ?
                                g->blood_textures.large[index]
                                : g->blood_textures.small[index],
                        .rotation = atan2f(dir_2d.y, dir_2d.x),
                    });
            }
            particle_enqueue_delete(level, p);
        } break;
        default:
        }
    }

move_done:;
    // apply drag
    const v3 drag = p->grounded ? type->floor_drag : type->air_drag;

    // apply drag: vel -= vel * drag * dt
    p->vel =
        v2_sub(
            p->vel,
            v2_mul(
                p->vel,
                v2_scale(
                    v2_from(drag),
                    dt)));
    p->vel_z -= p->vel_z * drag.z * dt;
}

void particle_tick(level_t *level, particle_t *p) {
    if (g->debug.pause_particles) { return; }

    if (p->duration != -1
        && (ticks_since_tick(p->start) >= p->duration || p->duration == 0)) {
        particle_enqueue_delete(level, p);
        return;
    }

    switch (p->type) {
    case PARTICLE_TYPE_AMBIENT_FLOATER:
    case PARTICLE_TYPE_FLOATER: {
        // manually delete since we don't ever hit due to our height
        if (p->z >= sector_point_zs(p->sector, p->pos).z1) {
            particle_enqueue_delete(level, p);
            return;
        }
    } break;
    default:
    }
}

void particle_inst_desc(
        level_t *level,
        particle_t *p,
        particle_inst_desc_t *desc) {
    switch (p->type) {
    case PARTICLE_TYPE_AMBIENT_FLOATER: {
        desc->color = v4_of(level->palette[ARRLEN(level->palette) - 1], 1);
    } break;
    case PARTICLE_TYPE_SMOKE: {
        desc->color = p->color;
        desc->color.a = 0.6f;
    } break;
    default: {
        desc->color = p->color;
    } break;
    }
}

light_desc_t particle_light(const level_t *level, const particle_t *p) {
    light_desc_t desc = { 0 };

    switch (p->type) {
    case PARTICLE_TYPE_SPARK:
    case PARTICLE_TYPE_SPAWN:
    case PARTICLE_TYPE_PORTAL:
    case PARTICLE_TYPE_RICOCHET:
    case PARTICLE_TYPE_ENEMY_BULLET:
        desc = (light_desc_t) {
            .pos = p->pos_xyz,
            .params = {
                .attenuation =
                    1.1f
                        * (1.0f -
                            ease_exp_in(ticks_since_tick(p->start) / (f32) p->duration)),
                .power = 0.0f,
                .color = v3_from(p->color),
                .c1 = 1.0f,
                .c2 = 4.0f,
                .ambient = 0.0f,
                .flags = LIGHT_FLAG_NO_SHADOWS | LIGHT_FLAG_IGNORE_NEAR,
            }
        };
        break;
    default:
    }

    // bit of a weird proxy to check if anything was set, but OK
    if (desc.params.attenuation == 0.0f) {
        return (light_desc_t) { 0 };
    }

    desc.id = LIGHT_ID_FROM(LIGHT_TYPE_PARTICLE, p->id);
    return desc;
}

particle_type_t PARTICLE_TYPES[PARTICLE_TYPE_COUNT] = {
    [PARTICLE_TYPE_SMOKE] = {
        .tex = "t_pix",
        .gravity = v3_const(0.0f, 0.0f, 0.05f),
        .floor_drag = v3_const(0.0f),
        .air_drag = v3_const(0.25f),
        .restitution = v3_const(0.0f),
    },
    [PARTICLE_TYPE_SPAWN] = {
        .tex = "t_pix",
        .gravity = v3_const(0.0f, 0.0f, 0.00f),
        .floor_drag = v3_const(0.75f),
        .air_drag = v3_const(0.7f),
        .restitution = v3_const(0.0f),
        .has_light = true,
    },
    [PARTICLE_TYPE_SPARK] = {
        .tex = "t_pix",
        .gravity = v3_const(0.0f, 0.0f, -0.6f),
        .floor_drag = v3_const(10.0f, 10.0f, 0.5f),
        .air_drag = v3_const(1.8f, 1.8f, 0.5f),
        .restitution = v3_const(0.6f, 0.6f, 0.5f),
        .has_light = true,
    },
    [PARTICLE_TYPE_FLOATER] = {
        .tex = "t_pix",
        .air_drag = v3_const(1.0f),
        .floor_drag = v3_const(1.0f),
    },
    [PARTICLE_TYPE_AMBIENT_FLOATER] = {
        .tex = "t_pix",
        .air_drag = v3_const(1.0f),
        .floor_drag = v3_const(1.0f),
    },
    [PARTICLE_TYPE_PORTAL] = {
        .tex = "t_pix",
        .air_drag = v3_const(1.0f),
        .has_light = true,
    },
    [PARTICLE_TYPE_ENEMY_BULLET] = {
        .tex = "t_pix",
        .gravity = v3_const(0.0f, 0.0f, 0.0f),
        .floor_drag = v3_const(0.0f),
        .air_drag = v3_const(0.25f),
        .restitution = v3_const(0.0f),
        .has_light = true,
    },
    [PARTICLE_TYPE_RICOCHET] = {
        .tex = "t_pix",
        .gravity = v3_const(0.0f, 0.0f, -0.6f),
        .floor_drag = v3_const(10.0f, 10.0f, 0.5f),
        .air_drag = v3_const(1.8f, 1.8f, 0.5f),
        .restitution = v3_const(0.6f, 0.6f, 0.5f),
        .has_light = true,
    },
    [PARTICLE_TYPE_BLOOD] = {
        .tex = "t_pix",
        .gravity = v3_const(0.0f, 0.0f, -0.6f),
        .floor_drag = v3_const(10.0f, 10.0f, 0.5f),
        .air_drag = v3_const(1.8f, 1.8f, 0.5f),
        .restitution = v3_const(0.6f, 0.6f, 0.5f),
    },
    [PARTICLE_TYPE_SAC_GAS] = {
        .tex = "t_pix",
        .gravity = v3_const(0.0f, 0.0f, 0.0f),
        .floor_drag = v3_const(1.0f),
        .air_drag = v3_const(1.0f),
        .restitution = v3_const(0.0f),
    },
};
