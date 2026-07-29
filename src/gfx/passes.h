#pragma once

#include "ext/sokol.h"
#include "util/math/linalg.h"

typedef struct {
    // size for which passes were created
    v2i size;

    struct {
        // RGB color, A -> ignore
        sg_image color;

        // (pos_w, i32_bits_to_f32(id))
        sg_image pos_w_id;

        // (pos_v, i32_bits_to_f32(buffer_index << 16 | type_flags))
        sg_image pos_v_index_type_flags;

        // (normal_encode(n_v), uv)
        sg_image normal_uv;

        sg_image depth_stencil;

        sg_attachments attach;
    } deferred;

    struct {
        // RGB color, A ignored
        sg_image color;

        // x component depends on what's rendered (see composite.glsl)
        // (?, i32_bits_to_f32(id), pos)
        sg_image extra_id_pos;

        // RGB light, A ignored
        sg_image light;

        // initial bloom image
        sg_image bloom;

        sg_attachments attach;
    } composite;

    struct {
        sg_image color, depth;
        sg_attachments attach;
    } phantom;

    struct {
        sg_image color;
        sg_attachments attach;
    } edge;

    struct {
        sg_image color, depth;
        sg_attachments attach;
    } overlay;

    struct {
        sg_image color, depth;
        sg_attachments attach;
    } editor;

    struct {
        // TODO: consider RG11B10F?
        // RGBA32F - (z0, z1, power, <unused>)
        sg_image image;
        sg_attachments attach;
    } shadow;

    struct {
        sg_image images[NUM_BLOOM_PASSES + 1];

        // attaches[i] has dst of images[i]
        sg_attachments attaches[NUM_BLOOM_PASSES + 1];

        // num passes must be even, fx. with 4 passes:
        // * images[0] gets information from render (res x1.00)
        // * images[1] is images[0] downsampled     (res x0.50)
        // * images[2] is images[1] downsampled     (res x0.25)
        // * images[3] is images[2] upsampled       (res x0.50)
        // * images[4] is images[3] upsampled       (res x1.00)
        STATIC_ASSERT(NUM_BLOOM_PASSES % 2 == 0);
    } bloom;

    // same as bloom
    struct {
        sg_image images[NUM_BLUR_PASSES + 1];
        sg_attachments attaches[NUM_BLUR_PASSES + 1];
        STATIC_ASSERT(NUM_BLUR_PASSES % 2 == 0);
    } blur;

    struct {
        sg_image color;
        sg_image light;
        // ABSENT: sg_image bloom; - use bloom.images[0]
        sg_attachments attach;
    } post0;

    struct {
        sg_image color;
        sg_attachments attach;
    } post1;

    struct {
        sg_image color;
        sg_attachments attach;
    } post2;
} passes_t;

// global passes
extern passes_t g_passes;

// init g_passes
void passes_init();

// must be called each frame
void passes_update();
