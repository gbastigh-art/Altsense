#include "fingers.h"
#include "cam.h"
#include "game.h"
#include "gfx/model.h"
#include "gfx/tex_atlas.h"
#include "level/entity.h"
#include "level/level_types.h"
#include "main_menu.h"
#include "particle_sim.h"
#include "sound/sound.h"
#include "vtext.h"

// global fingers state
fingers_state_t *g_fingers;
RELOAD_STATIC_GLOBAL(g_fingers)

static fingers_state_t *state;
RELOAD_STATIC_GLOBAL(state)

// default/neutral finger to render for otherwise undefined fingers
#define DEFAULT_FINGER FINGER_POINTER
#define DEFAULT_FINGERS_MODEL FINGERS_MODEL_SHOW

typedef enum {
    FINGERS_MODEL_SHOW,
    FINGERS_MODEL_SHOOT,
} finger_model_e;

typedef struct {
    const char *name;
    v3 dru;
    v3 dru_menu;
} finger_model_t;

static finger_model_t models[] = {
    [FINGERS_MODEL_SHOW] = {
        .name = "hand$show$0",
        .dru = v3_const(0.0f, -0.24f, 0.0f),
        .dru_menu = v3_const(-0.06f, -0.24f, 0.1f),
    },
    [FINGERS_MODEL_SHOOT] = {
        .name = "hand$p$0",
    },
};

static const char *finger_names[] = {
    [FINGER_PINKY]   = "pinky",
    [FINGER_RING]    = "ring",
    [FINGER_MIDDLE]  = "middle",
    [FINGER_POINTER] = "pointer",
    [FINGER_THUMB]   = "thumb",
};

void fingers_init() {
    state = mem_calloc(&g->arena, sizeof(*state));
    g_fingers = state;
}

static const model_index_group_t *finger_index_group(
        finger_e index,
        finger_model_e model) {
    if (index >= FINGER_EX_FIRST && index <= FINGER_EX_LAST) {
        // ex -> default
        index = DEFAULT_FINGER;
        model = DEFAULT_FINGERS_MODEL;
    }

    const model_data_t *data = model_atlas_lookupf(models[model].name);

    const char *group_name =
        mem_strfmt(&g->frame_arena, "f_%s", finger_names[index]);
    const model_index_group_t *group =
        model_data_try_get_index_group(data, group_name);
    if (group->is_default) {
        WARN(
            "could not get index group %s on model %s?",
            group_name,
            data->name);
    }
    return group;
}

// get current transform for side/set
static m4 fingers_transform(finger_model_e model) {
    const entity_t *ent = g->player;
    const f32
        t_swing = ent->swing / PLAYER_SWING_PERIOD,
        swing_angle = TAU * t_swing,
        t_shot =
            ease_back_out(
                satf(secs_since_ns(state->last_shot_ns) / 0.2f)),
        t_dash =
            ease_exp_out(
                1.0f - satf(abs_secs_since_ns(ent->last_dash_abs_ns) / 0.4f));

    const finger_model_t *pmodel = &models[model];

    v3 dru;

    if (state->mode == FINGERS_MODE_MENU) {
        dru = pmodel->dru_menu;
    } else {
        dru = pmodel->dru;
    }

    if (state->mode == FINGERS_MODE_MENU) {
        dru.z -= 0.8f * g_main_menu->tilt_up;

        if (g_main_menu->last_play_transition_s != 0.0f) {
            const stime_t since =
                secs_since_s(g_main_menu->last_play_transition_s);

            dru.z -= 0.8f * ease_exp_out(since / PLAY_TRANSITION_TIME_S);
        }
    }

    const f32 sgn = 1;
    v3 pos =
        v3_add(
            g->cam.pos,
            cam_dir_right_up(
                0.17f
                    - (0.17f * sinf(t_shot * PI))
                    - (0.10f * g->cam.slide)
                    + (0.03f * sinf(t_swing))
                    - (0.05f * (v2_norm(ent->vel) / (0.6f * PLAYER_MAX_SPEED)))
                    + (dru.x),
                (+0.18f
                    + (0.05f * cosf(PI_2 + swing_angle))
                    + (0.16f * g->cam.slide)
                    + (0.25f * t_dash)
                    - (0.05f * (v2_norm(ent->vel) / (0.6f * PLAYER_MAX_SPEED)))
                    + (dru.y))
                    * sgn,
                -0.36f
                    - (0.05f * fabsf(sinf(PI_2 + swing_angle)))
                    + (0.05f * sinf(t_shot * PI))
                    - (0.10f * g->cam.slide)
                    - (0.05f * t_dash)
                    + (0.05f * -1.0f * satf(ent->vel_z / 5.0f))
                    + (dru.z)));

    if (state->mode != FINGERS_MODE_SHOOT) {
        v2 center_to_cursor =
            v2_divs(
                v2_sub(
                    v2_from_i(g->input->cursor.pos),
                    v2_divs(v2_from_i(g->target_size), 2.0f)),
                v2i_max(g->target_size));

        static v2 ctc;
        ctc = v2_dtlerp(ctc, center_to_cursor, 1.0f, g->time.frame.dt_scaled);

        pos =
            v3_add(
                pos,
                cam_dir_right_up(
                    0.0f,
                    ctc.x * 0.12f,
                    (ctc.y * 0.18f)
                        + (0.015f * sinf(time_s()))));
    }

    m4 m = m4_identity();
    m = m4_mul(m, m4_translate_make(pos));
    m = m4_mul(m, m4_rotate_make_dir(g->cam.dir));
    // m = m4_mul(m, m4_rotate_make(sgn * pset->rot.z, v3_of(0, 0, 1)));
    // m = m4_mul(m, m4_rotate_make(pset->rot.y, v3_of(0, 1, 0)));
    // m = m4_mul(m, m4_rotate_make(sgn * pset->rot.x, v3_of(1, 0, 0)));

    return m;
}

static m4 finger_ex_transform(finger_e index) {
    ASSERT_DEBUG(index >= FINGER_EX_FIRST && index <= FINGER_EX_LAST);
    const int i = index - FINGER_EX_FIRST;

    m4 m = m4_identity();
    m = m4_mul(m, m4_translate_make(g->cam.pos));

    const v3 offset =
        cam_dir_right_up(
            0.5f,
            -0.5f + ((1.0f / (FINGER_EX_COUNT - 1)) * i),
            0.27f + (0.02f * sinf((2.0f * time_s()) + i)));

    m = m4_mul(m, m4_translate_make(offset));

    const model_index_group_t *index_group =
        finger_index_group(DEFAULT_FINGER, FINGERS_MODEL_SHOW);
    if (!index_group) { return m; }

    // center on index group
    rand_t rand = rand_create(hash_combine(0x1235, index));
    const v3 axis = rand_v3_cone_slice(&rand, v3_of(0, 0, 1), PI_16, PI_8);
    m = m4_mul(m, m4_rotate_make_from_to(v3_of(0, 0, 1), axis));
    m = m4_mul(m, m4_rotate_make(time_s() * 2.0f, v3_of(0, 0, 1)));

    if (state->hover.enabled && state->hover.index == index) {
        m = m4_mul(m, m4_scale_make(v3_of(1.25f)));
    }

    m = m4_mul(m, m4_translate_make(v3_scale(index_group->centroid, -1)));

    return m;
}

// get position of a finger tip for the specified side
static v3 finger_tip_pos(
        finger_e index,
        finger_model_e model) {
    const finger_model_t *pmodel = &models[model];
    const model_data_t *data = model_atlas_lookupf(pmodel->name);

    const model_index_group_t *ig =
        model_data_try_get_index_group(
            data,
            mem_strfmt(&g->frame_arena, "tip_%s", finger_names[index]));

    if (!ig) {
        WARN(
            "no index group tip_%s on hand$%s",
            finger_names[index],
            pmodel->name);
        return g->cam.pos;
    }

    const m4 m = fingers_transform(model);
    const v3 tip_l = data->vertices[ig->indices[0]].pos;
    return v3_from(m4_mulv(m, v4_of(tip_l, 1.0f)));
}

static box2f_t finger_screen_bounds(
        finger_e index,
        finger_model_e model,
        const m4 *transform) {
    const model_index_group_t *group = finger_index_group(index, model);

    box2f_t box = { .min = v2_of(1e10f), .max = v2_of(-1e10f) };
    dynlist_each(group->indices, it) {
        const v3 pos_l = group->model->vertices[*it.el].pos;
        const v3 pos_w = v3_from(m4_mulv(*transform, v4_of(pos_l, 1.0f)));
        const v2 pos_px =
            world_pos_to_clamped_pixel(
                &g->cam.view_proj,
                &g->cam.frustum,
                g->target_size,
                pos_w);

        box.min = v2_minv(box.min, pos_px);
        box.max = v2_maxv(box.max, pos_px);
    }

    return box;
}

static v3 cursor_pos_for_finger(
        finger_e index,
        const m4 *transform) {
    const model_index_group_t *group =
        finger_index_group(index, FINGERS_MODEL_SHOW);

    ray3f_t cursor_ray =
        cam_ray_from_pixel(
            &g->cam.inv_view_proj,
            g->cam.pos,
            v2_from_i(g->input->cursor.pos),
            g->target_size);

    const v3 center_l = group->centroid;
    const v3 center_w = v3_from(m4_mulv(*transform, v4_of(center_l, 1.0f)));

    const v4 plane =
        plane_from_point_normal(
            center_w,
            g->cam.dir);

    v3 hit;
    if (intersect_ray_plane(
            cursor_ray,
            plane,
            &hit,
            NULL)) {
        return hit;
    }

    return v3_of(0);
}

typedef struct {
    v3 dir;
    v3 fake_origin;
    f32 speed;
    f32 damage;
    bool is_mini;
} spawn_bullet_params_t;

static void spawn_bullet(const spawn_bullet_params_t *params) {
    const v3 shot_dst = v3_add(g->cam.pos, v3_scale(params->dir, 1000.0f));

    entity_new(
        g->level,
        &(entity_t) {
            .itype = ENTITY_TYPE_BULLET,
            .pos_xyz = g->cam.pos,
            .vel_xyz = v3_scale(params->dir, params->speed),
            .projectile = {
                .damage = params->damage,
                .dir = params->dir,
                .source = lptr_from(g->player),
                .fake_origin = params->fake_origin,
                .fake_dir = v3_dir(params->fake_origin, shot_dst),
            },
        });
}

// TODO: lmao, clean this up
static void update_on_render() {
    if (state->mode == FINGERS_MODE_SHOOT) { return; }

    const u8 select_button = input_get(g->input, "mouse1");
    bool any_hovered = false;

    // update boxes
    const m4 base_transform = fingers_transform(FINGERS_MODEL_SHOW);

    // compute for base + ex fingers, click/release state
    for (finger_e idx = FINGER_ALL_FIRST;
         idx <= FINGER_ALL_LAST;
         idx++) { 
        m4 transform;

        if (idx >= FINGER_BASE_FIRST && idx <= FINGER_BASE_LAST) {
            transform = base_transform;
        } else {
            transform = finger_ex_transform(idx);
        }

        state->boxes[idx] =
            finger_screen_bounds(
                idx,
                FINGERS_MODEL_SHOW,
                &transform);

        // extend down
        state->boxes[idx].max.x += 1;
        state->boxes[idx].min.x -= 1;
        state->boxes[idx].min.y -= 20;

        state->clicked[idx] = false;
        state->released[idx] = false;
        state->hovered[idx] =
            g->cursor_id == ((int) (RENDER_SPECIAL_ID_TAG | idx));
        
        if (!any_hovered
            && box2f_contains(
                state->boxes[idx],
                v2_from_i(g->input->cursor.pos))
            && box2f_distance_to_edge(
                state->boxes[idx],
                v2_from_i(g->input->cursor.pos)) >= 3.0f) {
            state->hovered[idx] = true;
        }

        any_hovered |= state->hovered[idx];

        const model_index_group_t *group =
            finger_index_group(idx, FINGERS_MODEL_SHOW);

        if (group) {
            state->centers[idx] =
                v3_from(
                    m4_mulv(
                        transform,
                        v4_of(group->centroid, 1.0f)));
        }

        g->boxes[idx] = state->boxes[idx];

        // update selection
        if (state->hovered[idx]
            && state->fingers[idx].present) {

            if (select_button & INPUT_PRESS) {
                if (state->mode == FINGERS_MODE_MENU) {
                    state->clicked[idx] = true;
                } else {
                    state->held.enabled = true;
                    state->held.index = idx;

                    state->held.diff = v3_of(0);
                    state->held.offset =
                        v3_sub(
                            state->centers[idx],
                            cursor_pos_for_finger(idx, &transform));
                }
            }

            if (select_button & INPUT_RELEASE) {
                if (state->mode == FINGERS_MODE_MENU) {
                    state->released[idx] = true;
                }
            }
        }
    }

    // update release
    if (state->held.enabled && (select_button & INPUT_RELEASE)) {
        state->held.enabled = false;

        // are we in another box?
        for (finger_e idx = FINGER_ALL_FIRST;
            idx <= FINGER_ALL_LAST;
            idx++) {
            if (!box2f_contains(
                    state->boxes[idx],
                    v2_from_i(g->input->cursor.pos))) {
                continue;
            }

            // inside of this box...
            if (idx == state->held.index) {
                // same box
                break;
            }

            finger_t *slot_dst = &state->fingers[idx];
            finger_t *slot_src = &state->fingers[state->held.index];

            const finger_t old = *slot_dst;

            *slot_dst = *slot_src;
            slot_src->present = false;

            // swap if there was a finger there before
            if (old.present) {
                *slot_src = old;
            }

            // done, found collision box
            break;
        }
    }

    const bool last_hover = state->hover.enabled;
    const finger_e last_hover_finger = state->hover.index;
    state->hover.enabled = false;

    if (state->held.enabled) {
        state->hover.enabled = true;
        state->hover.index = state->held.index;
    } else {
        for (finger_e idx = FINGER_ALL_FIRST; idx <= FINGER_ALL_LAST; idx++) {
            if (state->hovered[idx]) {
                state->hover.enabled = true;
                state->hover.index = idx;

                if (!last_hover || idx != last_hover_finger) {
                    state->hover.begin_s = g->time.total_scaled_s;
                }

                break;
            }
        }
    }

    if (state->hover.enabled && !last_hover) {
        static stime_t last_mouse_over_sound_s;
        if (g->time.total_s - last_mouse_over_sound_s > 0.1f) {
            last_mouse_over_sound_s = g->time.total_s;
            sound_play("mouse_over");
        }
    }

    f32 text_alpha = 0.15f;
    f32 text_mix = 0.0f;

    strbuf_set(
        &g->liquid_fall.texts[0],
        "$ITSUFFER FROM SENSATION ");

    strbuf_set(
        &g->liquid_fall.texts[1],
        "$IT$AEMORE BLOOD $F7FROM FLESH ");

    if (state->hover.enabled) {
        text_alpha = 1.0f;
        text_mix = 1.0f;
    }

    text_alpha *= 0.45f;

    g->liquid_fall.alpha = text_alpha;
    g->liquid_fall.mix = text_mix;
}

void fingers_update(level_t *level, entity_t *ent) {
    if (g->mode != GAMEMODE_MAIN_MENU && level->is_hub) { return; }

    if (state->mode == FINGERS_MODE_SHOOT) {
        const input_state_t input = g->controls.shoot;

        const v3 dir = g->cam.dir;
        f32 rate_scale = 1.0f;
        f32 kickback_scale = 1.0f;
        f32 spread_scale = 1.0f;
        f32 proj_speed_scale = 1.0f;
        f32 count_scale = 1.0f;
        f32 damage_scale = 1.0f;

        if (!(input.state & INPUT_PRESS)) {
            return;
        }

        if (ticks_since_tick(state->last_shot_tick)
                < (18 * rate_scale)) {
            return;
        }

        ent->last_shot = g->tick;
        state->last_shot_tick = g->tick;
        state->last_shot_ns = g->time.total_scaled_ns;
        state->last_finger_shot_tick[FINGER_POINTER] = g->tick;

        g->cam.pitch += 0.01f;
        g->cam.yaw += rand_sign(&g->rand) * 0.0125f;

        ent->vel_xyz =
            v3_add(ent->vel_xyz, v3_scale(dir, -2.0f * kickback_scale));

        const v3 fingertip = finger_tip_pos(FINGER_POINTER, FINGERS_MODEL_SHOOT);
        sound_play("s_boltgun");
        spawn_bullet(
            &(spawn_bullet_params_t) {
                .dir = rand_v3_cone(&g->rand, dir, 0.05f * spread_scale),
                .fake_origin = fingertip,
                .speed = 80.0f * proj_speed_scale,
                .damage = 50.0f * damage_scale,
            });

        screenshake_add(
            &(screenshake_t) {
                .strength = 1.0f,
                .duration = 0.4f,
            });

        renderer_add_tint(
            &(screen_tint_t) {
                .duration = 0.125f,
                .fade = true,
                .color = v4_of(v3_of(1.0f), 0.3f)
            });
    }
}

static void finger_render(
        finger_e index,
        finger_model_e model,
        const m4 *mmat) {
    finger_e render_index = index;
    finger_model_e render_model = model;

    if (index >= FINGER_EX_FIRST && index <= FINGER_EX_LAST) {
        // if EX finger, draw as "default" from "show" model
        render_index = DEFAULT_FINGER;
        render_model = FINGERS_MODEL_SHOW;
    }

    const model_index_group_t *index_group =
        finger_index_group(render_index, render_model);
    if (!index_group) { return; }

    const bool held =
        state->mode != FINGERS_MODE_SHOOT
        && state->held.enabled
        && state->held.index == index;
    const bool highlight =
        state->mode != FINGERS_MODE_SHOOT
        && state->hover.enabled
        && state->hover.index == index;

    m4 fmat = *mmat;

    if (held) {
        v3 diff_target =
            v3_add(
                v3_sub(
                    cursor_pos_for_finger(index, &fmat),
                    state->centers[index]),
                state->held.offset);

        // lerp to fix jitter (jitter happens since ray/plane intersection,
        // finger transforms, etc. are computed in world space)
        state->held.diff =
            v3_dtlerp(
                state->held.diff,
                diff_target,
                9.0f,
                g->time.frame.dt_scaled);
        fmat = m4_mul(m4_translate_make(state->held.diff), fmat);
    }

    f32 bloom_ex = 0.0f;
    if (index >= FINGER_EX_FIRST && index <= FINGER_EX_LAST) {
        bloom_ex = fabsf(sinf((time_s() * 1.5f) + (13.0f * (f32) index)));
    } else {
        bloom_ex = 1.0f;
    }

    *dynlist_push(g_renderer->frame_models) =
        (model_t) {
            .render_flags = RENDER_FLAG_SPECIAL_ID,
            .special_id = RENDER_SPECIAL_ID_TAG | index,
            .tex = tex_atlas_lookup("p_mixx"),
            .tex_overlay = state->hand_overlay_tex,
            .overlay_alpha = 0.25f,
            .hsv = v3_of(0.15f * (f32) index, 0.3, 0),
            .flags = 0
                | MRF_OVERLAY_SCROLL_V
                | MRF_OVERLAY_SCROLL_H
                | MRF_OVERLAY_POST
                | MRF_FIRST_PERSON
                | MRF_FINGER,
            .transform = fmat,
            .data = index_group->model,
            .index_group = index_group->name,
            .extra_bloom =
                v4_of(
                    v3_of(1.0f),
                    0.4f
                        + (0.4f * bloom_ex) 
                        + (highlight ? 0.3f : 0.0f)),
            .extra_light = v3_of(0.05f + (highlight ? 0.3f : 0.0f)),
        };

    if (index >= FINGER_BASE_FIRST && index <= FINGER_BASE_LAST) {
        const int since_shot =
            ticks_since_tick(state->last_finger_shot_tick[index]);
        if (since_shot < 6) {
            const v3 tip = finger_tip_pos(index, model);

            const tex_atlas_entry_t *entry =
                tex_atlas_entry_by_namef("shoot%d", (since_shot / 2) % 3);
            const v2 entry_size_units =
                v2_divs(v2_from_i(box2i_size(entry->box_px)), PX_PER_UNIT);

            *dynlist_push(g_renderer->frame_sprites) =
                (sprite_inst_desc_t) {
                    .pos =
                        v3_add(
                            tip,
                            cam_dir_right_up(
                                0.0f,
                                -(entry_size_units.x / 2.0f),
                                -(entry_size_units.y / 2.0f))),
                    .size = entry_size_units,
                    .color = v4_of(1),
                    .hsva = v4_of(0),
                    .id = LPTR_NULL,
                    .render_flags = RENDER_FLAG_NONE,
                    .flags = SPRITE_FLAG_FACE_CAMERA,
                    .tex_id = entry->id,
                    .rotation = 0.0f,
                    .extra_light = v3_of(0.25f),
                    .extra_bloom = v4_of(4.0f),
                };
        }
    }
}

static void fingers_render_model(
        level_t *level,
        entity_t *ent,
        finger_model_e model,
        finger_t *fingers) {
    const m4 mmat = fingers_transform(model);
    const model_data_t *pmodel = model_atlas_lookup(models[model].name);

    // draw base
    model_t *base_model;
    *(base_model = dynlist_push(g_renderer->frame_models)) =
        (model_t) {
            .id = lptr_from(ent),
            .tex = tex_atlas_lookup("x_pink"),
            .tex_overlay = state->hand_overlay_tex,
            .overlay_alpha = 0.35f,
            .hsv = v3_of(0, -1.0, -1.0),
            .flags = 0
                | MRF_OVERLAY_SCROLL_V
                | MRF_OVERLAY_SCROLL_H
                | MRF_OVERLAY_POST
                | MRF_FIRST_PERSON
                | MRF_RIGHT_HAND,
            .transform = mmat,
            .data = pmodel,
            .index_group = "base",
        };

    // update for particle sim
    {
        // TODO: single hand model
        typeof(g->psim->hand_models[0]) *psim_model =
            &g->psim->hand_models[1];

        psim_model->transform = mmat;
        strncpy(
            psim_model->name, models[model].name, sizeof(psim_model->name));
        strncpy(
            psim_model->index_group, "base", sizeof(psim_model->index_group));
    }

    // draw fingers
    for (finger_e f = FINGER_BASE_FIRST;
         f <= FINGER_BASE_LAST;
         f++) {
        if (!fingers[f].present) {
            continue;
        }

        finger_render(f, model, &mmat);
    }
}

void fingers_render(level_t *level, entity_t *ent) {
    if (g->mode != GAMEMODE_MAIN_MENU && level->is_hub) { return; }

    state->hand_overlay_tex = vtext_get_or_create("hand_text", g->hand_text);

    if (state->mode == FINGERS_MODE_MENU) {
        for (finger_e f = FINGER_BASE_FIRST; f <= FINGER_BASE_LAST; f++) {
            state->fingers[f].present = true;
        }

        for (finger_e f = FINGER_EX_FIRST; f <= FINGER_EX_LAST; f++) {
            state->fingers[f].present = false;
        }

        // show map editor finder
        state->fingers[FINGER_EX_LAST].present =
            g->editor_enabled
            && g_main_menu->screen == MAIN_MENU_SCREEN_DEFAULT
            && g_main_menu->last_play_transition_s == 0.0f;
    } else if (input_get(g->input, "l") & INPUT_PRESS) {
        for (finger_e f = FINGER_ALL_FIRST; f <= FINGER_ALL_LAST; f++) {
            state->fingers[f].present = false;
        }

        state->fingers[FINGER_POINTER].present = true;
        state->fingers[FINGER_THUMB].present = true;

        state->fingers[FINGER_EX0].present = true;
        // state->fingers[FINGER_EX1].present = true;
        state->fingers[FINGER_EX2].present = true;
        state->fingers[FINGER_EX3].present = true;
        state->fingers[FINGER_EX4].present = true;
    }

    update_on_render();

    fingers_render_model(
        level, ent,
        state->mode == FINGERS_MODE_SHOOT ?
            FINGERS_MODEL_SHOOT
            : FINGERS_MODEL_SHOW,
        state->fingers);

    // draw ex fingers
    if (state->mode == FINGERS_MODE_EDIT
        || state->mode == FINGERS_MODE_MENU) {
        for (finger_e idx = FINGER_EX_FIRST;
             idx <= FINGER_EX_LAST;
             idx++) {
            if (state->fingers[idx].present) {
                const m4 mmat = finger_ex_transform(idx);
                finger_render(
                    idx,
                    FINGERS_MODEL_SHOW,
                    &mmat);
            }
        }
    }
}
