#include "fingers.h"
#include "level/level.h"
#include "level/level_types.h"
#include "level/entity.h"
#include "level/room.h"
#include "level/sector.h"
#include "main_menu.h"
#include "sound/sound.h"
#include "util/math.h"
#include "util/time.h"
#include "game.h"
#include "config.h"
#include "defs.h"

#define PLAYER_SLIDE_TACKLE_MIN_SPEED 7.0f
#define PLAYER_BHOP_THRESHOLD_S       0.08f

// control influence from 0..1, used to remove control from player
static f32 get_control_factor(entity_t *e) {
    if (g->mode == GAMEMODE_MAIN_MENU) {
        return 0.0f;
    } else if (g->player_death_tick != 0) {
        return 0.0f;
    } else if (g_fingers->mode == FINGERS_MODE_EDIT) {
        return 0.0f;
    } else if (e->in_liquid && sector_type(e->sector)->liquid.no_move) {
        // remove all control within 0.5s of entering a no move liquid
        return 1.0f - satf(secs_since_tick(e->liquid_enter_tick) / 0.5f);
    }

    return 1.0f;
}

static entity_bounds_t player_bounds(
        const level_t*,
        const entity_t *e) {
    f32 height = PLAYER_HEIGHT;

    if (e->sliding) {
        height *= 0.4f;
    }

    return (entity_bounds_t) {
        .height = height,
        .radius = 0.20f,
        .hitbox_height = height,
        .hitbox_radius = 0.50f,
    };
}

static f32 player_step_height(const level_t*, const entity_t *e) {
    f32 step_height = STEP_HEIGHT_DEFAULT;
    if (e->sliding) {
        step_height *= 0.4f;
    }
    return step_height;
}

static v3 player_drag_floor(const level_t *l, const entity_t *e) {
    f32 scale = 1.0f;

    if (e->sliding) {
        if (v2_norm(e->vel) < 2.0f) {
            // at small velocities start slowing down more rapidly so that
            // the player doesn't slide forever
            scale = 0.4f;
        } else {
            scale = 0.1f;
        }
    }

    // less drag within bhop threshold after landing
    scale *=
        satf(
            secs_since_tick(e->last_grounded_start_tick)
                / PLAYER_BHOP_THRESHOLD_S);

    return v3_scale(e->ptype->drag_floor, scale);
}

static void player_frame_update(level_t *level, entity_t *ent) {
    const f32 control_factor = get_control_factor(ent);

    // TODO: mouse sensitivity
    if (g->mode == GAMEMODE_MAIN_MENU) {
        if (g_main_menu->last_play_transition_s != 0.0f) {
            // transition to forward look
            g->cam.pitch =
                dtlerp(g->cam.pitch, 0.0f, 5.0f, g->time.frame.dt_scaled); 
            g->cam.yaw =
                dtlerp(g->cam.yaw, PI_2, 5.0f, g->time.frame.dt_scaled); 
        } else {
            g->cam.pitch = g_main_menu->tilt_up * PI_2;
            g->cam.yaw = -PI_2;
        }
    } else {
        // mouse look
        f32 yaw = atan2f(ent->dir.y, ent->dir.x);
        yaw -=
            control_factor
                * 10.0f
                * (g->input->cursor.motion.x / g->target_size.x);

        g->cam.pitch +=
            control_factor
                * 8.0f
                * (g->input->cursor.motion.y / g->target_size.y);

        g->cam.yaw = yaw;
    }

    // player is a walker, so "dir" is 2D
    ent->dir = v3_of(fast_cos(g->cam.yaw), fast_sin(g->cam.yaw), 0.0f);

    if (g->controls.dash.state & INPUT_PRESS) {
        if (ent->stamina >= 40.0f) {
            ent->stamina -= 40.0f;
            ent->last_dash_abs_ns = g->time.total_ns;
            ent->dash_dir = g->cam.dir;
            ent->dashing = true;
            ent->dash_hold = true;

            g->slow_mo_time = 0.25f;
            renderer_add_tint(
                &(screen_tint_t) {
                    .fade = true,
                    .color = v4_of(v3_of(1), 0.7f),
                    .duration = 0.15f,
                });
        } else {
            sound_play("s_no_interact");
        }
    }

    // jump
    if (control_factor >= 1.0f
        && ent->jump < 0.0001f
        && !ent->in_liquid
        && ent->grounded
        && (g->controls.jump.state & INPUT_DOWN)
        && ns_to_secs(g->time.total_ns - g->controls.jump.time)
            <= g->time.frame.dt_scaled) {
        sound_play("jump");
        ent->last_jump_tick = g->tick;
        ent->jump = 1.0f;

        const f32 slide_duration_s =
            max(ent->last_slide_tick - ent->last_slide_begin_tick, 0)
                * (1.0f / TICKS_PER_SECOND);

        // boost in movement/look direction
        // boost more towards look direction if coming out of a slide
        f32 boost_dir_t = 0.8f;
        if (secs_since_tick(ent->last_slide_tick) <= 0.2f) {
            boost_dir_t += 0.2f * satf(slide_duration_s / 0.25f);
        }

        const v2 boost_dir =
            v2_slerp(
                ent->walk_dir,
                v2_normalize_from(g->cam.dir),
                boost_dir_t);

        if (!v2_eqv_eps(boost_dir, v2_of(0))) {
            f32 boost = 1.25f;

            // boost from bhop
            boost +=
                1.0f
                    * satf(
                        secs_since_tick(ent->last_grounded_start_tick)
                            / PLAYER_BHOP_THRESHOLD_S);

            // boost if recently sliding + slide was long enough (bigger bonus
            // for longer slides up to 0.25s)
            // boost from slide -> jump
            boost +=
                1.0f
                    * (1.0f
                        - satf(
                            secs_since_tick(ent->last_slide_tick)
                                / 0.10f))
                    * satf(slide_duration_s / 0.25f);
            
            // scale boost according to current movement speed
            boost *= satf(v2_norm(ent->vel) / (0.1f * PLAYER_MAX_SPEED));

            boost *= 0.6f;

            ent->vel_xyz =
                v3_add(
                    ent->vel_xyz,
                    v3_of(v2_scale(boost_dir, boost), 0.0f));
        }

        // if we're jumping out of a slide, rotate velocity to be towards
        // movement
        // this allows a slide-jump-around-corner sort of deal
        if (secs_since_tick(ent->last_slide_tick) <= 0.2f
            && slide_duration_s > 0.0f) {
            const f32 v_z = ent->vel_z;
            const f32 speed_xy = v2_norm(ent->vel);
            v2 dir =
                v2_slerp(
                    v2_normalize(ent->vel),
                    v2_normalize(v2_from(g->cam.dir)),
                    0.6f);
            ent->vel_xyz = v3_of(v2_scale(dir, speed_xy), v_z);
        }
    }

    // if != NULL, entity is in liquid of this type
    const sector_type_t *liquid = NULL;

    if (ent->sector && ent->in_liquid && sector_type(ent->sector)->is_liquid) {
        liquid = sector_type(ent->sector);
    }

    static lptr_t last_tint_liquid;

    // add liquid tint for current frame
    if (liquid
        && g->cam.pos.z <= ent->sector->floor.z + ent->sector->liquid_offset) {
        const sector_type_t *stype = sector_type(ent->sector);

        renderer_add_tint(
            &(screen_tint_t) {
                .color = stype->liquid.tint,
                .fade = false,
                .single_frame = true,
            });

        last_tint_liquid = lptr_from(ent->sector);
    } else if (!lptr_is_null(last_tint_liquid)) {
        // add tint when exiting liquid
        const sector_t *sect = lptr_sector(level, last_tint_liquid);
        if (sect && sector_type(ent->sector)->is_liquid) {
            const sector_type_t *stype = sector_type(ent->sector);

            renderer_add_tint(
                &(screen_tint_t) {
                    .color = stype->liquid.tint,
                    .fade = true,
                    .duration = 0.15f,
                });
        }

        last_tint_liquid = LPTR_NULL;
    }

    // no move liquid? move camera dir/angle to face directly forward
    if (liquid && liquid->liquid.no_move) {
        ent->dir =
            v3_dtslerp(
                ent->dir,
                v3_of(1, 0, 0),
                10.0f,
                g->time.frame.dt_scaled);
        g->cam.pitch =
            dtlerp(g->cam.pitch, 0.0f, 5.0f, g->time.frame.dt_scaled);
    }
}

static void player_fixed_update(level_t *level, entity_t *ent, f32 dt) {
    const f32 slide_target =
        ent->sliding ?
            ease_exp_in(
                satf(
                    secs_since_tick(ent->last_slide_begin_tick) / 0.1f))
            : 0.0f;

    g->cam.slide =
        dtlerp(
            g->cam.slide,
            slide_target,
            20.0f,
            dt);

    static f32 camera_height = CAMERA_HEIGHT;
    RELOAD_STATIC_RANGE(RANGE(camera_height));

    f32 target_camera_height =
        ent->z
            + CAMERA_HEIGHT
            + (g->cam.slide * SLIDE_Z_DIFF);

    if (g->player_death_tick) {
        target_camera_height = ent->z + 0.125f;
    }

    if (ticks_since_tick(ent->last_portal_tick) <= 1
        || fabsf(camera_height - target_camera_height) > STEP_HEIGHT_DEFAULT
        || g->mode == GAMEMODE_MAIN_MENU
        || g_fingers->mode == FINGERS_MODE_EDIT) {
        camera_height = target_camera_height;
    } else {
        camera_height =
            dtlerp(
                camera_height,
                target_camera_height,
                25.0f
                    + (25.0f * satf(fabsf(camera_height - target_camera_height) / 1.0f))
                    + (ent->sliding ? 50.0f : 0.0f),
                dt);
    }

    g->cam.pos = v3_of(ent->pos, target_camera_height);

    // TODO: optional configure camera bobbing
    f32 bob_power = 0.0f;

    if (ent->sliding) {
        bob_power = 0.0f;
    } else if (level->is_hub) {
        bob_power = 0.6f;
    } else {
        bob_power = 1.0f;
    }

    g->cam.pos.z +=
        bob_power
            * 0.035f
            * cosf(PI_2 + (4.0f * TAU * ent->swing / PLAYER_SWING_PERIOD)); 

    // recharge stamina according to current speed
    const f32 speed_scale = v2_norm(ent->vel) / PLAYER_MAX_SPEED;
    ent->stamina +=
        40.0f * ((0.75f * speed_scale) + 0.25f) * dt;
    ent->stamina = clamp(ent->stamina, 0.0f, 100.0f);

    // set when stamina bottoms out and player should be penalized
    bool stamina_bottomed = false;

    static i64 last_dash_hold = 0;
    const f32 dash_hold_cost = 150.0f * dt;

    if ((g->controls.dash.state & INPUT_DOWN) && ent->dash_hold) {
        if (ent->stamina >= dash_hold_cost) {
            ent->stamina -= dash_hold_cost;
            g->slow_mo_time += dt;
            last_dash_hold = g->time.total_ns;
        } else {
            stamina_bottomed = true;
            ent->dash_hold = false;
        }
    } else {
        ent->dash_hold = false;
    }

    if (ns_to_secs(g->time.total_ns - last_dash_hold) < 0.05f) {
        g->slow_mo_time += dt;
    }

    if (stamina_bottomed) {
        // TODO: penalize player for hitting zero
        renderer_add_tint(
            &(screen_tint_t) {
                .fade = true,
                .color = v4_of(1.0f, 0.1f, 0.1f, 0.7f),
                .duration = 0.25f,
            });
    }

    ent->stamina = clamp(ent->stamina, 0.0f, 100.0f);

    v2 movement = v2_of(0);
    const bool control = g->allow_control_input;

    // update for movement
    if (control) {
        f32 move_speed_base = 112.0f;

        if (level->is_hub) {
            move_speed_base = 60.0f;
        }

        if (ent->sliding) {
            // allow some movement in the first 0.25s of a slide
            move_speed_base = 30.0f;
            move_speed_base *=
                (1.0f
                     - satf(
                         secs_since_tick(ent->last_slide_begin_tick)
                            / 0.25f));
        }

        if (!ent->grounded && !ent->in_liquid) {
            move_speed_base *= 0.05f;
        }

        const f32
            control_influence = get_control_factor(ent),
            move_speed =
                move_speed_base
                    * control_influence
                    * dt,
            move_angle = atan2f(ent->dir.y, ent->dir.x);

        if (g->controls.up.state & INPUT_DOWN) {
            movement.x += cos(move_angle);
            movement.y += sin(move_angle);
        }

        if (g->controls.down.state & INPUT_DOWN) {
            movement.x -= cos(move_angle);
            movement.y -= sin(move_angle);
        }

        if (g->controls.left.state & INPUT_DOWN) {
            movement.x -= cos(move_angle - PI_2);
            movement.y -= sin(move_angle - PI_2);
        }

        if (g->controls.right.state & INPUT_DOWN) {
            movement.x += cos(move_angle - PI_2);
            movement.y += sin(move_angle - PI_2);
        }

        if (v2_norm(movement) > 0.001f && move_speed > 0.001f) {
            movement = v2_normalize(movement);

            // project 2D XY-movement vector onto plane tangent to get "true" XY
            // movement
            if (ent->grounded) {
                const v4 p = sector_plane_vec(ent->sector, PLANE_TYPE_FLOOR);
                movement =
                    v2_normalize_from(
                        v3_sub(
                            plane_project(
                                p,
                                v3_add(
                                    v3_of(movement, 0.0f),
                                    ent->pos_xyz)),
                            ent->pos_xyz));
            }

            ent->walk_dir = movement;
            ent->vel = v2_add(ent->vel, v2_scale(movement, move_speed));

            const f32 speed = v2_norm(ent->vel);

            if (!ent->grounded && speed > 0.005f) {
                // rotate velocity slightly towards desired move direction
                ent->vel =
                    v2_scale(
                        v2_slerp(
                            v2_normalize(ent->vel),
                            movement,
                            2.0f * dt),
                        speed);
            }
        }

        const f32 height = entity_height(level, ent);

        // move out of liquids if jump attempt
        // don't allow jumping in deep liquids (depth > 1/2 player height)
        if (get_control_factor(ent) >= 1.0f
            && (g->controls.jump.state & INPUT_DOWN)
            && ent->in_liquid
            && ent->sector->liquid_offset > (height * 0.5f)) {
            const f32 z_surface =
                sector_point_zs(ent->sector, ent->pos).z0
                    + ent->sector->liquid_offset;

            f32 amount = 0.0f;

            if (g->cam.pos.z > z_surface && ent->vel_z > 0.0f) {
                // water breaching
                const f32 scale = satf((g->cam.pos.z - z_surface) / 0.5f);
                amount = 100.0f + (80.0f * scale);
            } else if (ent->z < z_surface && (z_surface - ent->z) > 1.0f) {
                // normal water movement
                amount = 100.0f * satf((z_surface - ent->z) / 1.5f);
            }

            ent->vel_z += amount * dt;
        }
    }

    // update movement for jump
#define JUMP_ACCEL_TIME 0.005f

    if (ent->jump > 0.0f) {
        // remainder (in seconds) of jump time
        const f32 remainder = ent->jump * JUMP_ACCEL_TIME;

        const f32 power = 11.0f;
        ent->vel_z += (power / JUMP_ACCEL_TIME) * min(dt, remainder);

        // remove the dt'd jump time
        ent->jump -= dt / JUMP_ACCEL_TIME;
        ent->jump = max(ent->jump, 0.0f);
    }

    bool slide_started = false;

    if (control) {
        if (g->controls.slide.state & INPUT_DOWN) {
            // begin sliding iff:
            // * not hub level
            // * not in liquid
            // * not currently sliding
            // * didn't just slide -> jump
            if (!level->is_hub
                && !ent->in_liquid
                && !ent->sliding
                && !(ticks_since_tick(ent->last_jump_tick) <= 3
                     && ent->last_jump_tick - ent->last_slide_tick < 3)) {
                // begin slide
                ent->sliding = true;
                slide_started = true;

                // boost in slide/look dir if already moving in that direction
                // at a significant speed
                const v2 look_dir =
                    v2_normalize(v2_from(g->cam.dir));

                // set sliding_right according to velocity vs. our look angle
                {
                    const v2 vel_n = v2_normalize(ent->vel);
                    const f32 a_look = atan2f(look_dir.y, look_dir.x);
                    const f32 a_vel = atan2f(vel_n.y, vel_n.x);
                    ent->slide_angle = angle_min_diff(a_look, a_vel);
                }

                const f32 vel_on_look_dir = v2_dot(ent->vel, look_dir);

                // give bonus according to current velocity in look direction
                f32 boost =
                    lerp(
                        0.0f, 5.0f,
                        satf(vel_on_look_dir / PLAYER_MAX_SPEED));

                // boost diminishes if we we just sliding
                const f32 scale =
                    satf(
                        (secs_since_tick(ent->last_slide_begin_tick) - 0.33f)
                            / 0.33f);

                boost *= scale;

                ent->vel = v2_add(ent->vel, v2_scale(look_dir, boost));

                ent->last_slide_begin_tick = g->tick;
            }
        } else {
            // not sliding
            ent->sliding = false;
        }
    }

    // slide sound
    static sound_id_t slide_sound_id;
    if (ent->sliding) {
        playing_sound_t *sound;

        if (slide_sound_id == 0) {
            sound = sound_play("slide");
            if (sound) {
                sound->loop = true;
                slide_sound_id = sound->id;
            }
        } else {
            sound = sound_get(slide_sound_id);
        }

        if (sound) {
            // volume proportional to speed
            sound->volume = satf(v2_norm(ent->vel) / 10.0f);
        }
    } else {
        if (slide_sound_id != 0) {
            sound_stop(slide_sound_id);
        }
        slide_sound_id = 0;
    }

    if (ent->sliding) {
        ent->last_slide_tick = g->tick;

        // if the player is sliding, twist velocity slightly according to
        // movement. allow more player control as the slide proceeds, this also
        // helps with minimizing the "impulse" at the start of a slide in the
        // air when the player starts to control the movement this way
        if (!v2_eqv_eps(movement, v2_of(0))) {
            ent->vel =
                v2_scale(
                    v2_dtslerp(
                        v2_normalize(ent->vel),
                        movement,
                        lerp(
                            0.05f,
                            1.6f,
                            satf(
                                secs_since_tick(ent->last_slide_begin_tick)
                                    / 0.1f)),
                        dt),
                    v2_norm(ent->vel));
        }

        // if the player is sliding on an angled surface and looking towards
        // downwards vector, slide down
        const v3
            normal = sector_plane_normal(ent->sector, PLANE_TYPE_FLOOR),
            tangent = v3_normalize(v3_cross(v3_of(0, 0, 1), normal)),
            downward = v3_normalize(v3_cross(tangent, normal));

        if (v3_dot(normal, v3_of(0, 0, 1)) < 0.999f) {
            //&& v2_dot(
            //    v2_from(g->cam.dir),
            //    v2_from(downward)) > 0.25f) {
            ent->vel_xyz = v3_add(ent->vel_xyz, v3_scale(downward, 18.0f * dt));
        }

        // if we're really close to the floor and sliding, just snap to the
        // floor. prevents a bumpy ride while sliding.
        const f32 z_floor = sector_point_zs(ent->sector, ent->pos).z0;
        if (ent->z - z_floor < 0.04f) {
            ent->z = z_floor;
        }
    }

    // cancel slide if jumping
    if (ent->jump > 0.0f) {
        ent->sliding = false;
    }

    if (ent->dashing) {
        if (ns_to_secs(g->time.total_ns - ent->last_dash_abs_ns) < 0.15f) {
            ent->impulse = v3_scale(ent->dash_dir, 110.0f);
        } else {
            ent->dashing = false;
        }
    }

    // enforce max speed
    if (secs_since_tick(ent->last_liquid_teleport_tick) > 0.25f) {
        ent->vel = v2_clamp_mag(ent->vel, PLAYER_MAX_SPEED);
    }

    // update swing
    {
        if (!ent->sliding) {
            f32 swing_speed = v2_norm(ent->vel) / (0.75f * PLAYER_MAX_SPEED);
            swing_speed += 0.5f;
            swing_speed *= ent->grounded ? 1.0f : 0.25f;

            ent->swing += 250.0f * swing_speed * dt;
        }

        // move towards swing % period = 0.0f
        const f32 mult =
            roundf(
                ent->swing / (PLAYER_SWING_PERIOD / 2.0f))
                * (PLAYER_SWING_PERIOD / 2.0f);

        const f32 speed_2d = min(v2_norm(ent->vel), 15.0f);
        if (speed_2d < 4.0f || ent->sliding) {
            ent->swing =
                lerp(
                    ent->swing,
                    dtlerp(ent->swing, mult, 5.0f, dt),
                    invsatf(speed_2d / PLAYER_MAX_SPEED));
        }
    }
}

static void player_tick(level_t *level, entity_t *ent) {
    // check death
    if (ent->health <= 0.0f) {
        if (g->player_death_tick == 0) {
            g->player_death_tick = g->tick;
        }

        return;
    }

    // add movement boost when hitting ground sliding assuming:
    // * grounded (and recently grounded)
    // * started slide before hitting the ground
    // * started slide at most 0.25s before jump crest
    // * started slide max 0.33s before hitting the ground
    if (ent->grounded
        && ticks_since_tick(ent->last_grounded_start_tick) <= 1
        && ent->sliding
        && ent->last_slide_begin_tick - 5 < ent->last_grounded_start_tick
        && ent->last_slide_begin_tick > ent->z_vel_crest_tick - 10
        && (ent->last_grounded_start_tick - ent->last_slide_begin_tick) < 10) {
        ent->vel =
            v2_add(
                ent->vel,
                v2_scale(v2_normalize(ent->vel), 2.0f));
    }
}

static light_desc_t player_light(const level_t *level, const entity_t *ent) {
    const f32 since = secs_since_tick(ent->last_shot);
    if (since > 0.25f) {
        return (light_desc_t) { 0 };
    }

    const f32 power = 1.0f - ease_cubic_out(since / 0.25f);

    return (light_desc_t) {
        .id = LIGHT_ID_FROM(LIGHT_TYPE_ENTITY, ent->id),
        .pos = entity_center(level, ent),
        .params = {
            .attenuation = 2.0f + (2.0f * power),
            .power = 4.0f * power,
            .color = v3_of(1.0f),
            .c1 = 1.0f,
            .c2 = 3.0f,
            .ambient = 0.0,
            .flags = LIGHT_FLAG_NO_SHADOWS,
        },
    };
}

bool player_try_slide_tackle(
        level_t *level,
        entity_t *player,
        entity_t *other) {
    if (!other->ptype->is_damageable) {
        return false;
    }

    if (other->last_slide_hit_tick > other->last_grounded_start_tick) {
        // mob has ben slide tackled while being in the air already
        return false;
    }

    if (ticks_since_tick(other->last_slide_begin_tick) <= 2) {
        // no double hits
        return false;
    }

    // can only slide tackle if sliding + grounded with enough speed or dashing
    if (!player->dashing && !player->sliding) {
        // only if dashing or sliding
        return false;
    }

    const v2 normal = v2_dir(other->pos, player->pos);

    if (player->sliding) {
        if (secs_since_tick(player->last_slide_begin_tick) < 0.05f) {
            // must have been sliding for a bit
            return false;
        }

        if (secs_since_tick(player->last_grounded_tick) > 0.05f) {
            // must be on ground
            return false;
        }

        if (v2_norm(player->vel) < PLAYER_SLIDE_TACKLE_MIN_SPEED
            || v2_dot(v2_normalize(player->vel), normal) > 0.0f) {
            // must be going fast enough towards other
            return false;
        }
    }

    // project player velocity onto hit normal, use as base of scale
    // (a more direct hit means further backwards travel)
    const v2 projected = v2_proj(player->vel, normal);

    const f32 scale = satf(v2_norm(projected) / 10.0f);

    entity_try_damage(
        other,
        &(entity_damage_desc_t) {
            .amount = scale * 5.0f,
            .knockback = min(25.0f * scale, 12.0f),
            .dir =
                v3_of(
                    v2_slerp(
                        v2_normalize(projected),
                        v2_from(g->cam.dir),
                        0.4f),
                    0.0f),
        });

    const f32 mass_scale = 0.5f * (1.0f / (1.0f + other->ptype->mass));

    // add extra knockback...
    // move opposite of normal
    v3 knockback = v3_of(v2_scale(normal, -5.0f * scale), 0);

    // move slightly to the right or left of the camera to give the
    // effect of mobs being cleared away by the slide tackle
    knockback =
        v3_add(
            knockback,
            v3_of(
                v2_scale(
                    v2_proj(
                        normal,
                        v2_from(g->cam.right)),
                    -20.0f * scale),
               0.0f));

    if (player->dashing) {
        // if dashing, add extra knockback in dash direction
        knockback = v3_add(knockback, v3_scale(player->dash_dir, 25.0f));
        other->impulse =
            v3_scale(
                player->dash_dir,
                max(2000.0f * mass_scale, 1000.0f));
    }

    knockback = v3_scale(knockback, mass_scale);

    other->vel_xyz = v3_add(other->vel_xyz, knockback);

    // a little extra popup, greater if sliding
    other->vel_z += (player->sliding ? 15.0f : 7.5f) * scale * mass_scale;

    other->last_slide_hit_tick = g->tick;

    // extra pain when slide tackled
    other->pain_ticks += 10;

    renderer_add_tint(
        &(screen_tint_t) {
            .duration = 0.15f,
            .fade = true,
            .color = v4_of(v3_of(1.0f), 0.6f * scale)
        });

    screenshake_add(
        &(screenshake_t) {
            .strength = 3.0f,
            .duration = 0.20f,
        });

    if (other->health > 0.0f && g->slow_mo_time < 0.75f) {
        g->slow_mo_time = player->dashing ? 1.0f : 0.75f;
    }

    // did tackle
    return true;
}

static bool player_move_trace_resolve(
        level_t *level,
        entity_t *ent,
        trace_3d_t *trace,
        const trace_hit_t *hit,
        trace_resolve_result_e *res)  {
    if (hit->type == LT_ENTITY) {
        // try to slide tackle
        if (player_try_slide_tackle(level, ent, hit->entity.ptr)) {
            // ignore this hit
            *res = TRACE_RESOLVE_CONTINUE;
            return true;
        }
    }

    // didn't write res, keep going
    return false;
}

static bool player_collides(
        const level_t*,
        const entity_t *player,
        const entity_t *other) {
    return !other->last_slide_hit_tick
        || ticks_since_tick(other->last_slide_hit_tick) >= 10;
}

ENTITY_TYPE_REGISTER(
    ENTITY_TYPE_PLAYER,
    (entity_type_t) {
        .is_invisible = true,
        .is_damageable = true,
        .has_light = true,
        .light_fn = player_light,
        .bounds_fn = player_bounds,
        .frame_update_fn = player_frame_update,
        .fixed_update_fn = player_fixed_update,
        .tick_fn = player_tick,
        .move_trace_resolve_fn = player_move_trace_resolve,
        .collides_fn = player_collides,
        .drag_air = v3_const(0.1f),
        .drag_floor = v3_const(1.0f),
        .drag_floor_fn = player_drag_floor,
        .gravity = v3_of(0, 0, -1),
        .step_height_fn = player_step_height,
        .mass = 0.0f,
        .max_health = 100.0f,
    })
