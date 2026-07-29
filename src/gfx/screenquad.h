#pragma once

#include "ext/sokol.h"
#include "util/math.h"
#include "gfx/shaders.h"

void screenquad_mats(m4 *model, m4 *view, m4 *proj);

sg_pipeline screenquad_get_pipeline(
        shader_e shader,
        sg_pixel_format pixel_format,
        bool has_depth);

void screenquad_render_ex(
    sg_pipeline pipeline,
    const sg_bindings *bindings,
    int vs_slot,
    const sg_range *vs_params,
    int fs_slot,
    const sg_range *fs_params);

typedef struct {
    v4 tint;
    f32 transparency;
} screenquad_params_t;

void screenquad_render(
        sg_pixel_format dst_color_format,
        bool has_depth,
        sg_image src,
        sg_sampler sampler,
        const screenquad_params_t *params);
