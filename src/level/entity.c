#include "level/entity.h"
#include "gfx/model.h"
#include "gfx/screenshake.h"
#include "gfx/renderer.h"
#include "level/level_types.h"
#include "level/level.h"
#include "level/block.h"
#include "level/lptr.h"
#include "level/particle.h"
#include "level/portal.h"
#include "level/sector.h"
#include "level/side.h"
#include "util/math.h"
#include "entity/player.h"
#include "sound/sound.h"
#include "game.h"
#include "trace.h"

bool entity_is_active(const level_t *l, const entity_t *e) {
    // player is always active
    if (g->player && e == g->player) { return true; }

    // if ai is off, enemies do nothing
    if (g->debug.no_ai && e->ptype->is_enemy) { return false; }

    // if player is in entry or exit juice, all other entities do nothing
    if (g->player
        && g->player->in_liquid
        && (g->player->sector->type == SECTOR_TYPE_EXIT_JUICE
            || g->player->sector->type == SECTOR_TYPE_ENTRY_JUICE)) {
        return false;
    }

    if (e->ptype->is_enemy && g->player && e->room != g->player->room) {
        return false;
    }

    return true;
}

v3 entity_center(const level_t *l, const entity_t *e) {
    const entity_bounds_t bounds = entity_bounds(l, e);
    return
        v3_add(
            bounds.origin,
            v3_of(0, 0, bounds.height / 2.0f));
}

f32 entity_between_2d(
        const level_t *level,
        const entity_t *a,
        const entity_t *b) {
    // TODO: broken for bounds with origin
    const f32
        r_a = entity_bounds(level, a).radius,
        r_b = entity_bounds(level, b).radius;
    return max(v2_distance(a->pos, b->pos) - r_a - r_b, 0);
}

f32 entity_between_3d(
        const level_t *level,
        const entity_t *a,
        const entity_t *b) {
    // TODO: broken for bounds with origin

    const entity_bounds_t
        bounds_a = entity_bounds(level, a),
        bounds_b = entity_bounds(level, b);

    const f32 dist =
        max(v2_distance(a->pos, b->pos) - bounds_a.radius - bounds_b.radius, 0);

    const f32
        z0_a = a->z,
        z1_a = a->z + bounds_a.height,
        z0_b = b->z,
        z1_b = b->z + bounds_b.height;

    // only add z distance if there is no overlap between the ranges
    return dist
        + (z0_a > z1_b ? fabsf(z0_a - z1_b) : 0.0f)
        + (z0_b > z1_a ? fabsf(z0_b - z1_a) : 0.0f);
}

entity_t *entity_new(level_t *level, const entity_t *defaults) {
    entity_t *ent = level_try_alloc(level, &level->entities);

    if (defaults) {
        level_fields_t backup = ent->level_fields;
        *ent = *defaults;
        ent->level_fields = backup;
    }

    ent->itype = ENTITY_TYPE_PLACEHOLDER;
    ent->ptype = &ENTITY_TYPES[ENTITY_TYPE_PLACEHOLDER];
    ent->spawn_tick = g->tick;
    ent->spawn_ns = g->time.total_scaled_ns;

    // defaults
    ent->dir = v3_of(1, 0, 0);
    ent->walk_dir = v2_of(1, 0);
    ent->fly_dir = v3_of(1, 0, 0);
    ent->turret_dir = v3_of(1, 0, 0);
    ent->target_last_seen_dir = v3_of(1, 0, 0);
    ent->dash_dir = v3_of(1, 0, 0);

    if (defaults) {
        if (defaults->itype) {
            entity_set_type(level, ent, defaults->itype);
        }

        ent->pos_xyz = v3_of(0);

        if (!v2_eqv_eps(defaults->pos, v2_of(0))) {
            entity_try_move(level, ent, defaults->pos);
        }

        ent->z = defaults->z;
        ent->health = ent->ptype->max_health;
    }

    return ent;
}

void entity_delete(level_t *level, entity_t *ent) {
    // remove from type list
    if (ent->itype != ENTITY_TYPE_PLACEHOLDER) {
        // remove from type list
        dlist_remove(
            level_by_type_node,
            &level->entities_by_type[ent->itype],
            ent);
    }

    if (ent->sector) {
        dlist_remove(sector_node, &ent->sector->entities, ent);
    }

    if (ent->subsector) {
        dlist_remove(subsector_node, &ent->subsector->entities, ent);
    }

    level_blocks_remove_entity(level, ent);
    level_free(level, &level->entities, ent);
}

void entity_destroy(level_t *level, entity_t *ent) {
    if (ent->destroy) { return; }
    ent->destroy = true;
    level_enqueue_delete(level, lptr_from(ent));
}

void entity_set_type(
        level_t *level,
        entity_t *ent,
        entity_type_e type) {
    if (!entity_type_is_valid(type)) {
        WARN(
            "invalid entity type for entity @ %p, resetting",
            ent);
        type = ENTITY_TYPE_PLACEHOLDER;
    } else if (ent->itype == type) {
        // TODO: not helpful?
        // WARN("setting type for entity @ %p to the same?", entity);
    }

    if (ent->itype != ENTITY_TYPE_PLACEHOLDER) {
        // remove from old list
        dlist_remove(
            level_by_type_node,
            &level->entities_by_type[ent->itype],
            ent);
    }

    if (type != ENTITY_TYPE_PLACEHOLDER) {
        // add to new list (on back to maintain ordering)
        dlist_append(
            level_by_type_node,
            &level->entities_by_type[type],
            ent);
    }

    const entity_type_t *ty = &ENTITY_TYPES[type];

    ent->itype = type;
    ent->ptype = ty;

    // copy initial props
    ent->health = ty->max_health;
}

bool entity_try_move(level_t *level, entity_t *ent, v2 pos) {
    if (v2_eqv_eps(pos, ent->pos)) {
        return true;
    }

    // no negative coords
    pos = v2_maxv(pos, v2_of(0));

    // update sector (move if sector is no longer correct)
    subsector_t *new_sub =
        level_find_point_subsector(level, pos, ent->subsector);

    // don't allow movement
    if (!new_sub) {
        WARN(
            "entity %d attempted move out of sector",
            lptr_to_index(level, lptr_from(ent)));

        return false;
    }

    // move
    ent->pos = pos;

    sector_t *new_sect = new_sub->parent;

    // move sectors
    if (ent->sector != new_sect) {
        if (ent->sector) {
            dlist_remove(sector_node, &ent->sector->entities, ent);
        }

        if (new_sect) {
            dlist_prepend(sector_node, &new_sect->entities, ent);
        }

        ent->sector = new_sect;
    }

    // move subsectors
    if (ent->subsector != new_sub) {
        if (ent->subsector) {
            dlist_remove(subsector_node, &ent->subsector->entities, ent);
        }

        if (new_sub) {
            dlist_prepend(subsector_node, &new_sub->entities, ent);
        }

        ent->subsector = new_sub;
    }

    // queue for block update
    ent->update_blocks = true;
    return true;
}

void entity_frame_update(level_t *level, entity_t *ent) {
    if (!ent->destroy && !ent->corpse && ent->ptype->frame_update_fn) {
        ent->ptype->frame_update_fn(level, ent);
    }
}

typedef struct {
    entity_t *ent;
    f32 dt;
} move_trace_resolve_userdata_t;

static trace_resolve_result_e move_trace_resolve(
        level_t *level,
        trace_3d_t *trace,
        const trace_hit_t *hit) {
    move_trace_resolve_userdata_t *userdata = trace->userdata;
    entity_t *ent = userdata->ent;
    const f32 dt = userdata->dt;

    if (ent->destroy || ent->corpse) {
        // stop
        return TRACE_RESOLVE_STOP;
    }

    if (hit->type == LT_ENTITY) {
        entity_t *other = hit->entity.ptr;
        ASSERT_DEBUG(other != ent);

        // check if player will slide tackle us...
        if (other->itype == ENTITY_TYPE_PLAYER
             && player_try_slide_tackle(level, other, ent)) {
            return TRACE_RESOLVE_STOP;
        }

        // process entity if hitbox (so on_hit_fn is only called once in case
        // a regular collision comes later on)
        if (hit->entity.is_hitbox) {
            if (ent->ptype->on_hit_fn) {
                ent->ptype->on_hit_fn(level, ent, other);
            }

            if (other->ptype->on_hit_fn) {
                other->ptype->on_hit_fn(level, other, ent);
            }
        }

        // react to potential changes from on_hit_fn
        if (ent->destroy) {
            return TRACE_RESOLVE_STOP;
        } else if (other->destroy) {
            return TRACE_RESOLVE_CONTINUE;
        }
    }

    // check for portal
    f32 portal_angle;
    const trace_portal_result_e portal_result =
        trace_resolve_portal_3d(level, trace, hit, &portal_angle);
    switch (portal_result) {
    case TRACE_PORTAL_RESULT_IGNORE:
        // ignore -> not a portal
        break;
    case TRACE_PORTAL_RESULT_THROUGH:
        // through -> just keep going
    case TRACE_PORTAL_RESULT_REJECT:
        // reject -> is portal but not applicable, keep going
        return TRACE_RESOLVE_CONTINUE;
    case TRACE_PORTAL_RESULT_STOP:
        // break, this will be projected
        break;
    case TRACE_PORTAL_RESULT_THROUGH_DISCONNECT:
        // update projectile directions, fake origin
        if (ent->ptype->is_projectile) {
            // TODO: standardize this calculation somewhere
            const v3 current_fake_pos =
                v3_add(
                    ent->projectile.fake_origin,
                    v3_scale(
                        ent->projectile.fake_dir,
                        ns_to_secs(g->time.total_scaled_ns - ent->spawn_ns)
                            * v3_norm(ent->vel_xyz)));

            ent->projectile.fake_origin =
                portal_transform_3d(
                    level,
                    hit->side.ptr,
                    hit->side.ptr->portal,
                    current_fake_pos);

            ent->projectile.fake_dir =
                v3_normalize_of(
                    v2_rotate(v2_from(ent->projectile.fake_dir), portal_angle),
                    ent->projectile.fake_dir.z);
            ent->projectile.dir =
                v3_normalize_of(
                    v2_rotate(v2_from(ent->projectile.dir), portal_angle),
                    ent->projectile.dir.z);
        }

        // moved through disconnected portal, retry movement
        ent->dir =
            v3_normalize_of(
                v2_rotate(v2_normalize_from(ent->dir), portal_angle),
                ent->dir.z);
        ent->vel = v2_rotate(ent->vel, portal_angle);
        ent->walk_dir = v2_rotate(ent->walk_dir, portal_angle);
        ent->fly_dir =
            v3_normalize_of(
                v2_rotate(v2_from(ent->fly_dir), portal_angle),
                ent->fly_dir.z);
        ent->last_portal_tick = g->tick;

        // TODO: in player specifics with on_move_portal_fn
        if (ent->itype == ENTITY_TYPE_PLAYER) {
            ent->dash_dir =
                v3_normalize_of(
                    v2_rotate(
                        v2_from(ent->dash_dir), portal_angle),
                    ent->dash_dir.z);
        }

        if (ent->ptype->on_move_portal_fn) {
            ent->ptype->on_move_portal_fn(level, ent, hit);
        }

        return TRACE_RESOLVE_RETRY;
    }

    trace_resolve_result_e res = TRACE_RESOLVE_STOP;

    // placement here means that entities can't respond to portal movement
    // but maybe that's OK? just move if it ends up being necessary...
    if (ent->ptype->move_trace_resolve_fn) {
        trace_resolve_result_e ent_res = TRACE_RESOLVE_CONTINUE;

        if (ent->ptype->move_trace_resolve_fn(
                level,
                ent,
                trace,
                hit,
                &ent_res)) {
            return ent_res;
        }
    }

    if (hit->type == LT_ENTITY && !hit->entity.is_collision) {
        // keep going if not a direct collision, don't stop/project
        return TRACE_RESOLVE_CONTINUE;
    }

    if (hit->type == LT_SECTOR) {
        const rangef_t zr = sector_point_zs(hit->sector.ptr, hit->swept_pos);

        ent->vel_z = 0.0f;

        // move dst according to plane clamping
        if (hit->sector.plane == PLANE_TYPE_FLOOR) {
            trace->dst.z = zr.z0 + 0.00001f;
            if (hit->t < 0.0001f) { trace->dst.z += 0.001f; }
        } else {
            // ceil
            trace->dst.z = zr.z1 - trace->height - 0.00001f;
            if (hit->t < 0.0001f) { trace->dst.z -= 0.001f; }
        }

        // NOTE: if we stop using TRACE_FLAG_XY_THEN_Z, then we should retry
        // here for XY movement
        // for now though, keep res == STOP
        return TRACE_RESOLVE_RETRY;
    } else {
        bool do_2d_project = true;

        // TODO
        //if (hit->type == LT_ENTITY) {
        //    const entity_t *other = hit->entity.ptr;

        //    // did we move zs into this entity? if we were initially colliding
        //    // even before we tried to move, that's probably the case - so
        //    // zero out our Z movement and retry
        //    const entity_bounds_t bounds = entity_bounds(level, ent);
        //    const entity_bounds_t other_bounds = entity_bounds(level, other);
        //    if (fabsf(trace->dst.z - trace->org.z) > 0.0001f) {
        //        do_2d_project = false;
        //        res = TRACE_RESOLVE_RETRY;
        //        trace->dst.z = trace->org.z;
        //        ent->vel_z = 0.0f;
        //        LOG("avoiding and just cancelling z movement %d vs. %d", ent->id, other->id);
        //    }
        //}

        if (do_2d_project) {
            // project hit movement if applicable
            const move_project_result_t project_result =
                move_project_hit_velocity(
                    level,
                    hit,
                    (line2f_t) {
                        .a = v2_from(trace->org),
                        .b = v2_from(trace->dst),
                    },
                    ent->vel,
                    v2_of(0.0f),
                    dt);

            if (project_result.changed) {
                // velocity projected, update movement and retry
                res = TRACE_RESOLVE_RETRY;

                trace->org = v3_of(project_result.movement.a, trace->org.z);
                trace->dst = v3_of(project_result.movement.b, trace->dst.z);
                ent->vel = project_result.velocity;
            } else {
                trace->dst =
                    v3_of(hit->swept_pos, lerp(trace->org.z, trace->dst.z, hit->t));
            }
        }
    }

    ASSERT_DEBUG(
        res == TRACE_RESOLVE_STOP || res == TRACE_RESOLVE_RETRY,
        "%d",
        res);

    if (ent->ptype->move_on_hit_fn) {
        ent->ptype->move_on_hit_fn(level, ent, hit);
    }

    return res;
}

static bool move_trace_filter(level_t *level, lptr_t ptr, void *userdata) {
    const entity_t *ent = userdata;

    if (lptr_is(ptr, LT_ENTITY)) {
        const entity_t *other = lptr_entity(level, ptr);

        if (other == ent
            || other->ptype->is_edit_only
            || other->destroy
            || other->corpse) {
            return false;
        }

        // TODO: can short circuit
        // check that entities want to collide
        const bool
            ent_collides =
                !ent->ptype->collides_fn
                || ent->ptype->collides_fn(level, ent, other),
           other_collides =
                !other->ptype->collides_fn
                || other->ptype->collides_fn(level, other, ent);

        if (!(ent_collides && other_collides)) {
            // ignore
            return false;
        }
    }

    return true;
}

void entity_fixed_update(level_t *level, entity_t *ent) {
    if (ent->destroy || ent->corpse) { return; }

    f32 dt = g->time.fixed.dt_scaled;

    if (ent->ptype->distributed_fixed_update) {
        // only update 1/4 fixed updates with 4x dt
        if ((g->time.fixed.count % 4) != (ent->id % 4)) {
            return;
        }

        dt *= 4.0f;
    }

    // attach entities are fixed to the level
    if (ent->ptype->is_attach && !ent->attach.disabled) {
        // auto-attach to nearest point
        if (!ent->attach.side && !ent->attach.sector) {
            WARN("%d (%s) is not attached, auto-attaching",
                 ent->id,
                 entity_type_to_str(ent->itype));
            const v2 point = level_clamp_point(level, ent->pos);
            sector_t *sector =
                level_find_point_sector(level, point, NULL);

            if (sector) {
                entity_attach_sector(
                    level, ent, sector, point, PLANE_TYPE_FLOOR);
                WARN("  moved to %" PRIv2, FMTv2(point));
            } else {
                WARN("  could not move!");
            }
        }
    }

    if (g->mode == GAMEMODE_EDITOR) { return; }

    // entity liquid handling
    if (ent->sector && sector_type(ent->sector)->is_liquid) {
        const sector_type_t *stype = sector_type(ent->sector);

        const f32 z_surface =
            sector_point_zs(ent->sector, ent->pos).z0
                + ent->sector->liquid_offset;
        if (ent->grounded || ent->z < z_surface) {
            if (!ent->in_liquid) {
                ent->in_liquid = true;

                // just entered
                ent->liquid_enter_tick = g->tick;

                if (stype->liquid.on_enter_fn) {
                    stype->liquid.on_enter_fn(level, ent->sector, ent);
                }
            }
        } else {
            ent->in_liquid = false;
        }
    } else {
        ent->in_liquid = false;
    }

    // handle liquid entry/exit
    if (ent->in_liquid) {
        if (lptr_is_null(ent->last_liquid)) {
           // entry
           const sector_type_t *stype = sector_type(ent->sector);
           if (stype->liquid.on_enter_fn) {
               stype->liquid.on_enter_fn(level, ent->sector, ent);
           }
        }

        ent->last_liquid = lptr_from(ent->sector);
    } else if (!ent->in_liquid) {
        // cannot use ent->sector since entity may have moved out of sector
        sector_t *sect;
        if ((sect = lptr_sector(level, ent->last_liquid))) {
            // exit
            const sector_type_t *stype = sector_type(sect);
            if (stype->liquid.on_exit_fn) {
                stype->liquid.on_exit_fn(level, sect, ent);
            }
        }

        ent->last_liquid = LPTR_NULL;
    }

    if (ent->ptype->fixed_update_fn) {
        ent->ptype->fixed_update_fn(level, ent, dt);
    }

    // liquid teleport movement
    if (ent->last_liquid_teleport_tick
        && secs_since_tick(ent->last_liquid_teleport_tick) < 0.35f) {
        const f32 since =
            secs_since_tick(ent->last_liquid_teleport_tick) / 0.35f;

        const f32 mass_factor = 1.0f + (ent->ptype->mass * 0.25f);
        if (ent->z < ent->liquid_teleport_target.z) {
            ent->vel_z += 1200.0f * since * mass_factor * dt;
        }

        const v2_diff_t diff =
            v2_diff(ent->pos, v2_from(ent->liquid_teleport_target));
        ent->vel =
            v2_add(
                ent->vel,
                v2_scale(
                    diff.dir,
                    max(diff.dist, 0.5f)
                        * 150.0f
                        * since
                        * mass_factor
                        * dt));

        sector_t *exit_sector =
            lptr_sector(level, ent->liquid_teleport_sector);

        if (exit_sector) {
            const v2 look_target = box2f_center(exit_sector->bounds);
            const v3 look_dir =
                v3_normalize_of(
                    v2_dir(ent->pos, look_target),
                    ent->dir.z);
            ent->dir = v3_dtslerp(ent->dir, look_dir, 50.0f, dt);
        }
    }

    // if != NULL, entity liquid
    const sector_type_t *liquid = NULL;

    if (ent->in_liquid && ent->sector && sector_type(ent->sector)->is_liquid) {
        liquid = sector_type(ent->sector);
    }

    if (liquid && liquid->liquid.inside_fn) {
        liquid->liquid.inside_fn(level, ent->sector, ent);
    }

    // no physics for no_phyics and attached entities
    if (ent->ptype->no_physics
        || (ent->ptype->is_attach && !ent->attach.disabled)) {
        ent->vel_xyz = v3_of(0);

        if (ent->sector) {
            const f32 z0 = sector_point_zs(ent->sector, ent->pos).z0;
            ent->grounded = fabsf(ent->z - z0) < 0.0001f;
        }

        return;
    }

    // apply gravity, drag
    v3 gravity = entity_gravity(level, ent);

    ent->vel_xyz =
        v3_add(
            ent->vel_xyz,
            v3_scale(gravity, 50.0f * dt));

    v3 drag;

    const bool floor_drag = ent->grounded || liquid;
    if (floor_drag) {
        drag = entity_drag_floor(level, ent);
    } else {
        drag = entity_drag_air(level, ent);
    }

    if (!v3_eqv_eps(drag, v3_of(0))) {
        if (liquid) {
            drag = v3_scale(drag, 1.2f * (1.0f + liquid->liquid.viscosity));
        }

        // if grounded, drag also reacts to plane orientation
        if (ent->grounded && !liquid) {
            const v3 floor_normal =
                sector_plane_normal(ent->sector, PLANE_TYPE_FLOOR);

            if (v3_dot(floor_normal, v3_of(0, 0, 1.0)) < 0.999f) {
                const v3 into_plane = v3_scale(floor_normal, -1.0f);
                drag =
                    v3_add(
                        drag,
                        v3_scale(
                            v3_mul(
                                v3_normalize(ent->vel_xyz),
                                into_plane),
                            0.125f));
            }
        }

        // sector drag goes here if we want it eventaully
        drag = v3_scale(drag, 12.0f);

        ent->vel_xyz =
            v3_sub(
                ent->vel_xyz,
                v3_mul(
                    ent->vel_xyz,
                    v3_scale(drag, dt)));
    }

    if (v3_any_nan(ent->vel_xyz)) {
        WARN(
            "entity %d (%s) has NaN velocity!",
            ent->id,
            entity_type_to_str(ent->itype));
        ent->vel_xyz = v3_of(0);
    }

    if (fabsf(ent->vel.x) < 0.001f) { ent->vel.x = 0.0f; }
    if (fabsf(ent->vel.y) < 0.001f) { ent->vel.y = 0.0f; }

    const v3 movement = v3_add(ent->vel_xyz, ent->impulse);
    ent->impulse = v3_of(0);

    if (!v3_eqv_eps(movement, v3_of(0.0f))) {
        const v3 dt_movement = v3_scale(movement, dt);

        // keep in case movement fails entirely
        const v3 old_pos = ent->pos_xyz;
        const v3 old_dir = ent->dir;

        const entity_bounds_t bounds = entity_bounds(level, ent);

        move_trace_resolve_userdata_t userdata = {
            .ent = ent,
            .dt = dt,
        };

        trace_3d_t trace = {
            .org = ent->pos_xyz,
            .dst = v3_add(ent->pos_xyz, dt_movement),
            .radius = bounds.radius,
            .height = bounds.height,
            .step_height = entity_step_height(level, ent),
            .types = LTF_SIDE | LTF_SECTOR | LTF_ENTITY,
            .flags = 0
                | TRACE_FLAG_XY_THEN_Z
                | TRACE_FLAG_FORCE_PORTAL_AWAY
                | (ent->itype == ENTITY_TYPE_PLAYER ?
                    TRACE_FLAG_FORCE_PORTALS_NEAR_TO_CAM
                    : 0)
                | TRACE_FLAG_ENTITY_HITBOXES,
            .resolve_fn = move_trace_resolve,
            .userdata = &userdata,
            .filter_fn = move_trace_filter,
            .filter_userdata = ent,
        };
        const trace_result_e trace_result = trace_3d(level, &trace);

        switch (trace_result) {
        case TRACE_RESULT_NOT_STOPPED: {
            // TODO: cleanup
            const v3 new_pos = trace.dst;

            if (!v2_eqv_eps(v2_from(new_pos), v2_from(old_pos))) {
                if (entity_try_move(level, ent, v2_from(new_pos))) {
                    ent->z = new_pos.z;
                }
            } else {
                ent->z = new_pos.z;
            }
        } break;
        case TRACE_RESULT_STOPPED: {
            // TODO: cleanup
            const v3 new_pos = trace.org;

            if (!v2_eqv_eps(v2_from(new_pos), v2_from(old_pos))) {
                if (entity_try_move(level, ent, v2_from(new_pos))) {
                    ent->z = new_pos.z;
                }
            } else {
                ent->z = new_pos.z;
            }
        } break;
        case TRACE_RESULT_TOO_MANY_RETRIES:
            ent->pos_xyz = old_pos;
            ent->dir = old_dir;

            const entity_bounds_t bounds = entity_bounds(level, ent);
            if (!v3_eqv_eps(bounds.origin, ent->pos_xyz)) {
                // TODO: handle this case (offset bounds entities collision resolution)
                WARN(
                    "too many retries and offset bounds for entity %d/%s",
                    ent->id,
                    entity_type_to_str(ent->itype));
                break;
            }

            v3 new_pos = ent->pos_xyz;

            // try to resolve collision
            switch (trace.last_hit.type) {
            case LT_SIDE: {
                const side_t *side = trace.last_hit.side.ptr;
                vertex_t *vs[2];
                side_get_vertices(side, vs);

                v2 resolved;
                if (intersect_circle_seg(
                        ent->pos,
                        bounds.radius,
                        vs[0]->pos,
                        vs[1]->pos,
                        NULL,
                        &resolved)) {
                    v2 pos = resolved;
                    pos = v2_add(pos, v2_scale(side_normal(side), 0.01f));

                    const v3 pos_3d =
                        level_clamp_point_3d(
                            level,
                            v3_of(pos, ent->z),
                            bounds.height);

                    if (fabsf(pos_3d.z - ent->z) >= 0.01f) {
                        WARN(
                            "cannot unstick entity %d/%s from side %d",
                            ent->id,
                            entity_type_to_str(ent->itype),
                            side->id);
                    } else {
                        new_pos = pos_3d;
                    }
                }
            } break;
            case LT_SECTOR: {
                const sector_t *sect = trace.last_hit.sector.ptr;
                const rangef_t zs = sector_point_zs(sect, v2_from(bounds.origin));

                if (ent->z < zs.z0) {
                    new_pos.z = zs.z0;
                } else if (ent->z + bounds.height > zs.z1) {
                    new_pos.z = zs.z1 - bounds.height;
                }
            } break;
            case LT_ENTITY: {
                break; // TODO TODO TODO TODO
                const entity_t *other = trace.last_hit.entity.ptr;
                const f32 other_radius = entity_radius(level, other);

                LOG("have to resolve too many hits between %d and %d",
                    ent->id,
                    trace.last_hit.entity.ptr->id);

                if (intersect_circle_circle(
                        ent->pos,
                        bounds.radius,
                        other->pos,
                        other_radius)) {
                    const f32 desired_dist = bounds.radius + other_radius;
                    const f32 true_dist = v2_distance(ent->pos, other->pos);

                    ASSERT_DEBUG(true_dist <= desired_dist);

                    const v2 normal = v2_dir(other->pos, ent->pos);
                    v2 resolved = ent->pos;
                    resolved =
                        v2_add(
                            resolved,
                            v2_scale(normal, desired_dist - true_dist));


                    const v3 pos_3d =
                        level_clamp_point_3d(
                            level,
                            v3_of(resolved, ent->z),
                            bounds.height);

                    if (fabsf(pos_3d.z - ent->z) >= 0.01f) {
                        WARN(
                            "cannot unstick entity %d/%s from entity %d",
                            ent->id,
                            entity_type_to_str(ent->itype),
                            ent->id);
                    } else {
                        new_pos = pos_3d;
                    }
                }
            } break;
            default:
            }

            if (!entity_try_move(level, ent, v2_from(new_pos))) {
                WARN(
                    "cannot unstick entity %d/%s from %s",
                    ent->id,
                    entity_type_to_str(ent->itype),
                    lptr_to_str(level, trace.last_hit.ptr, tlscratch()));
            } else {
                ent->z = new_pos.z;
            }

            break;
        }
    }

    if (ent->destroy || ent->corpse) {
        // done, entity was destroyed by movement
        return;
    }

    const rangef_t zr = sector_point_zs(ent->sector, ent->pos);

    // on sloped surfaces: if we have negative gravity, are moving downwards,
    // and we aren't trying any upwards movement, snap z to that surface
    if (ticks_since_tick(ent->last_grounded_tick) <= 1
        && gravity.z < 0.0f
        && ent->sector->floor.slope_side
        && v3_norm(ent->vel_xyz) > 0.5f
        && ent->vel_z <= 0.0f
        && (ent->itype != ENTITY_TYPE_PLAYER || ent->jump <= 0.0f)
        && ent->z - sector_point_zs(ent->sector, ent->pos).z0 < -gravity.z) {
        const v3
            normal = sector_plane_normal(ent->sector, PLANE_TYPE_FLOOR),
            tangent = v3_normalize(v3_cross(v3_of(0, 0, 1), normal)),
            downward = v3_normalize(v3_cross(tangent, normal));

        if (v2_dot(
                v2_normalize(ent->vel),
                v2_normalize(v2_from(downward))) > 0.0f) {
            ent->z = sector_point_zs(ent->sector, ent->pos).z0;
        }
    }

    // on sloped surfaces: check that, if we were to move straight down for a
    // few frames, we would still be on the floor
    const bool fake_grounded =
        gravity.z < 0.0f
        && ent->vel_z <= 0.0f
        && secs_since_tick(ent->last_grounded_start_tick) < 0.1
        && fabsf(ent->z - zr.z0) <= -gravity.z * dt * 20.0f;

    const bool last_grounded = ent->grounded;
    ent->grounded =
        fake_grounded
        || fabsf(ent->z - zr.z0) < 0.001f;

    if (ent->grounded) {
        ent->last_grounded_tick = g->tick;

        if (!last_grounded) {
            ent->last_grounded_start_tick = g->tick;
        }
    }

    if (!ent->grounded) {
        ent->last_airborne_tick = g->tick;

        if (last_grounded) {
            ent->last_airborne_start_tick = g->tick;
        }
    }

    if (!ent->grounded
        && ent->last_vel.z >= 0.0f
        && ent->vel_z < 0.0f) {
        // entity has crested
        ent->z_vel_crest_tick = g->tick;
        ent->z_crest = ent->z;
    }

    ent->last_vel = ent->vel_xyz;
}

void entity_tick(level_t *level, entity_t *ent) {
    if (ent->last_liquid_teleport_tick
        && secs_since_tick(ent->last_liquid_teleport_tick) < 0.05f) {

        for (int i = 0, n = rand_n(&g->rand, 10, 30); i < n; i++) {
            particle_new(
                level,
                v2_add(
                    ent->pos,
                    v2_scale(rand_v2_dir(&g->rand), 0.25f)),
                &(particle_t) {
                    .type = PARTICLE_TYPE_SPAWN,
                    .z = ent->z,
                    .duration =
                        (3 * TICKS_PER_SECOND) + rand_n(&g->rand, -30, 30),
                    .color = v4_of(1),
                    .vel_xyz =
                        v3_scale(
                            v3_slerp(
                                v3_normalize(ent->vel_xyz),
                                rand_v3_dir(&g->rand),
                                0.8f),
                            8.0f),
                    .spawn = {
                        .target = lptr_from(ent)
                    }
                });
        }
    }

    // apply liquid damage
    if (ent->in_liquid) {
        const sector_type_t *ptype = sector_type(ent->sector);

        if (ptype->liquid.damage_ticks
            && (ticks_since_tick(ent->liquid_enter_tick)
                    % ptype->liquid.damage_ticks)
                == 1
            && ticks_since_tick(ent->last_damage_tick) >= 20) {
            entity_try_damage(
                ent,
                &(entity_damage_desc_t) {
                    .amount = ptype->liquid.damage_per_hit,
                    .knockback = 0.0f,
                });
        }
    }

    if (ent->ptype->tick_fn) {
        ent->ptype->tick_fn(level, ent);
    }
}

void entity_try_damage(entity_t *ent, const entity_damage_desc_t *desc) {
    if (!ent->ptype->is_damageable) { return; }

    if (desc->amount <= 0.0f) { return; }

    // no melee damage if sliding, should be popped up
    if (ent->itype == ENTITY_TYPE_PLAYER) {
        if (desc->is_melee
            && ent->sliding
            && v3_norm(ent->vel_xyz) > 3.0f) {
            return;
        }
    }

    // player is invulnerable while dashing
    if (ent->dashing) {
        return;
    }

    ent->pain_ticks += 10;

    f32 slide_hit_bonus = 1.0f;

    if (!ent->grounded
        && ent->last_slide_hit_tick
        && ent->last_slide_hit_tick == ent->last_airborne_start_tick) {
        slide_hit_bonus = 5.0f;
    }

    if (ent->itype == ENTITY_TYPE_PLAYER) {
        g->slow_mo_time += 0.5f;
        sound_play("s_hit");
    }

    ent->health -= desc->amount * slide_hit_bonus;

    f32 knockback_power = 0.0f;
    v3 knockback_dir = v3_of(0, 0, 1);

    if (desc->knockback > 0.0f) {
        knockback_power = desc->knockback;

        if (v3_eqv_eps(desc->dir, v3_of(0)) && desc->source) {
            if (desc->source && desc->source->ptype->is_projectile) {
                knockback_dir = v3_normalize(desc->source->vel_xyz);
            } else {
                knockback_dir =
                    v3_diff(desc->source->pos_xyz, ent->pos_xyz).dir;
            }
        } else {
            knockback_dir = desc->dir;
        }
    } else if (desc->source && desc->source->ptype->is_projectile) {
        // auto-knockback with projectiles if not already specified
        knockback_power = satf(desc->amount / 15.0f) * 10.0f;
        knockback_dir = v3_normalize(desc->source->vel_xyz);
    } else {
        knockback_power = satf(desc->amount / 15.0f) * 10.0f;
        knockback_dir = v3_eq(desc->dir, 0.0f) ? v3_of(0, 0, 1) : desc->dir;
    }

    if (!ent->ptype->no_physics && knockback_power > 0.0f) {
        // add scaled knockback according to mass
        ent->vel_xyz =
            v3_add(
                ent->vel_xyz,
                v3_scale(
                    knockback_dir,
                    knockback_power * (1.0f / (ent->ptype->mass + 1.0f))));
    }

    if (ent->itype == ENTITY_TYPE_PLAYER) {
        if (!desc->tint.disabled) {
            screen_tint_t tint;

            if (desc->tint.params.duration) {
                tint = desc->tint.params;
            } else {
                tint = (screen_tint_t) {
                    .duration = 0.33f,
                    .fade = true,
                    .color = v4_of(2.0f, 1.0f, 0.4f, 0.8f)
                };
            }

            renderer_add_tint(&tint);
        }

        if (!desc->screenshake.disabled) {
            f32 intensity;
            f32 duration;

            if (desc->screenshake.intensity != 0.0f) {
                intensity = desc->screenshake.intensity;
                duration = desc->screenshake.duration;
            } else {
                intensity = clamp(desc->amount / 10.0f, 0.5f, 3.0f) * 1.4f;
                duration = 0.25f;
            }

            screenshake_add(
                &(screenshake_t) {
                    .strength = intensity,
                    .duration = duration
                });
        }
    }

    ent->last_damage_dir = knockback_dir;
    ent->last_damage_tick = g->tick;
}

void entity_attach_side(
        level_t *level,
        entity_t *ent,
        side_t *side,
        v2 offset) {
    if (!ent->ptype->is_attach) {
        ERROR("trying to attach unattachable %d (%s)",
              ent->id,
              entity_type_to_str(ent->itype));
        return;
    }

    ent->attach = (typeof(ent->attach)) { .disabled = false };
    ent->attach.side = side;
    ent->attach.offset = offset;

    vertex_t *vs[2];
    side_get_vertices(ent->attach.side, vs);
    const v2 pos =
        v2_add(
            v2_lerp(
                vs[0]->pos, vs[1]->pos,
                ent->attach.offset.x / ent->attach.side->wall->len),
            v2_scale(
                side_normal(ent->attach.side),
                0.01f));

    entity_try_move(level, ent, pos);
    ent->z = ent->attach.side->sector->floor.z + ent->attach.offset.y;
}

void entity_attach_sector(
        level_t *level,
        entity_t *ent,
        sector_t *sector,
        v2 offset,
        plane_type_e plane) {
    if (!ent->ptype->is_attach) {
        ERROR("trying to attach unattachable %d (%s)",
              ent->id,
              entity_type_to_str(ent->itype));
        return;
    }

    ent->attach = (typeof(ent->attach)) { .disabled = false };
    ent->attach.sector = sector;
    ent->attach.offset = offset;
    ent->attach.plane = plane;

    entity_try_move(
        level, ent,
        ent->attach.offset);
    ent->z =
        ent->attach.sector->planes[ent->attach.plane].z
            - (ent->attach.plane == PLANE_TYPE_CEIL ?
                entity_height(level, ent) : 0.0f);
}

void entity_attach_copy(
        level_t *level,
        entity_t *dst,
        const entity_t *src) {
    if (!dst->ptype->is_attach) {
        ERROR(
            "trying to attach unattachable %d (%s)",
            dst->id,
            entity_type_to_str(dst->itype));
        return;
    }

    if (!src->ptype->is_attach) {
        ERROR(
            "trying to attach copy form unattachable %d (%s)",
            src->id,
            entity_type_to_str(src->itype));
        return;
    }

    if (src->attach.disabled) {
        if (entity_try_move(level, dst, src->pos)) {
            dst->z = src->z;
        }
    } else if (src->attach.side) {
        entity_attach_side(level, dst, src->attach.side, src->attach.offset);
    } else if (src->attach.sector) {
        entity_attach_sector(
            level,
            dst,
            src->attach.sector,
            src->attach.offset,
            src->attach.plane);
    } else {
        ASSERT(false);
    }
}

v3 entity_attach_surface_normal(const level_t *level, const entity_t *ent) {
    if (!ent->attach.disabled) {
        if (ent->attach.side) {
            return v3_of(side_normal(ent->attach.side), 0);
        } else if (ent->attach.sector) {
            return sector_plane_normal(ent->attach.sector, ent->attach.plane);
        }
    }

    WARN(
        "getting attach surface normal for a non-attached entity? %d/%s",
        ent->id,
        entity_type_to_str(ent->itype));
    return v3_of(0, 0, 1);
}

void entity_do_liquid_exit(level_t *level, entity_t *ent) {
    const sector_t *sect = ent->sector;

    // center of exit sector
    // TODO: point-in-subsector check bad
    const v2 center =
        v2_add(
            box2f_center(sect->bounds),
            v2_of(0.01f, 0.01f));

    // TODO: check result
    if (entity_try_move(level, ent, center)) {
        ent->z =
            sector_point_zs(sect, ent->pos).z0
                + ent->sector->liquid_offset
                - entity_height(level, ent);
    } else {
        WARN("could not move to center");
        return;
    }

    // get a (random) point on the edge of the sector to hop towards
    const v2 out_target =
        sector_project_onto_edge(
            sect,
            v2_add(
                center,
                v2_scale(
                    rand_v2_dir(&g->rand),
                    v2_max(box2f_size(sect->bounds)) / 2.0f)));

    const v2 out_dir = v2_dir(center, out_target);

    ent->liquid_teleport_target =
        level_clamp_point_3d(
            level,
            v3_of(
                v2_add(out_target, v2_scale(out_dir, 0.5f)),
                ent->z),
            entity_height(level, ent));
    ent->liquid_enter_tick = INT_MAX;
    ent->last_liquid_teleport_tick = g->tick;
    ent->liquid_teleport_sector = lptr_from(sect);
    ent->vel_xyz = v3_of(0);

    const v2 dir = v2_dir(out_target, center);
    ent->dir = v3_normalize_of(dir, ent->dir.z);
    g->cam.yaw = atan2f(dir.y, dir.x);

    g->liquid_fall.effect = 0.0f;
    renderer_add_tint(
        &(screen_tint_t) {
            .duration = 1.5f,
            .fade = true,
            .color = v4_of(2.0f, 1.0f, 0.4f, 1.0f),
        });
}

void entity_face_dir(
        level_t *level,
        entity_t *ent,
        v3 dir,
        f32 rot_speed,
        f32 dt) {
    // TODO ASSERT_DEBUG(fabsf(v3_norm(dir) - 1.0f) < 0.0001f);
    // TODO ASSERT_DEBUG(fabsf(v3_norm(ent->dir) - 1.0f) < 0.0001f);

    // get spherical angle between two vectors
    const f32 angle = acosf(v3_dot(ent->dir, dir));

    // maximum difference we can move this update
    const f32 max_diff_angle = (TAU * rot_speed) * dt;

    // compute absolute difference we move this update, in units of the maximum
    // possible diff vs. the whole angle. keep in 0..1.
    const f32 abs_diff = satf(max_diff_angle / fabsf(angle));

    // slerp by abs_diff
    ent->dir = v3_slerp(ent->dir, dir, abs_diff);
}

void entity_edit_cube_models(
        level_t *level,
        entity_t *ent,
        DYNLIST(model_t) *models) {
    *dynlist_push(*models) =
        (model_t) {
            .id = lptr_from(ent),
            .data = model_atlas_lookup("unit_cube"),
            .transform =
                m4_mul(
                    m4_translate_make(ent->pos_xyz),
                    m4_scale_make(v3_of(0.5f))),
            .hsv = ent->ptype->edit_cube_hsv,
        };
}

static light_desc_t fill_light_light(
        const level_t *level,
        const entity_t *ent) {
    return (light_desc_t) {
        .id = LIGHT_ID_FROM(LIGHT_TYPE_ENTITY, ent->id),
        .pos = entity_center(level, ent),
        .params = ent->light,
    };
}

// filled in by ENTITY_TYPE_REGISTER(...)
entity_type_t ENTITY_TYPES[ENTITY_TYPE_COUNT] = {
    [ENTITY_TYPE_PLACEHOLDER] = {
        .sprite = "notex",
        .bounds = {
            .radius = 1.0f,
            .height = 1.0f,
        },
    },
    [ENTITY_TYPE_BOOKMARK] = {
        .sprite = "o_bookmark",
        .is_edit_only = true,
        .bounds = {
            .radius = 1.0f,
            .height = 1.0f,
        },
    },
    [ENTITY_TYPE_FADE_ORIGIN] = {
        .is_edit_only = true,
        .has_model = true,
        .models_fn = entity_edit_cube_models,
        .edit_cube_hsv = v3_of(-0.3f, 0.0f, 0.0f),
    },
    [ENTITY_TYPE_FILL_LIGHT] = {
        .is_edit_only = true,
        .is_z_free = true,
        .has_model = true,
        .models_fn = entity_edit_cube_models,
        .edit_cube_hsv = v3_of(0.8f, 0.3f, 0.3f),
        .has_light = true,
        .light_fn = fill_light_light,
    },
    [ENTITY_TYPE_SPAWN_POINT] = {
        .is_edit_only = true,
        .has_model = true,
        .models_fn = entity_edit_cube_models,
        .edit_cube_hsv = v3_of(-0.7f, 0.3f, 0.6f),
    },
};
