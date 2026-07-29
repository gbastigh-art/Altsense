#include "gfx/screenquad.h"

#include "gfx/shaders.h"
#include "game.h"

#include "shader/screenquad.glsl.h"

static struct {
    bool initialized;
    sg_buffer ibuf, vbuf;
    sg_pipeline *pipelines[SHADER_COUNT][_SG_PIXELFORMAT_NUM][2 /* depth? */];
} state;

RELOAD_STATIC_GLOBAL(state)

sg_pipeline screenquad_get_pipeline(
        shader_e shader,
        sg_pixel_format pixel_format,
        bool has_depth) {
    sg_pipeline **pslot =
        &state.pipelines[shader][pixel_format][has_depth ? 1 : 0];

    if (*pslot) { return **pslot; }

    // create pipeline
    *pslot = mem_alloc(&g->arena, sizeof(**pslot));
    **pslot = sg_make_pipeline(
        &(sg_pipeline_desc) {
            .shader = shaders_get(shader),
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .index_type = SG_INDEXTYPE_UINT16,
            .layout = {
                .attrs = {
                    [0].format = SG_VERTEXFORMAT_FLOAT2,
                    [1].format = SG_VERTEXFORMAT_FLOAT2,
                }
            },
            .depth = {
                .pixel_format =
                    has_depth ? SG_PIXELFORMAT_DEPTH : SG_PIXELFORMAT_NONE,
                .compare = SG_COMPAREFUNC_ALWAYS,
                .write_enabled = false,
            },
            .colors[0] = {
                .pixel_format = pixel_format,
                .blend = {
                    .enabled = true,
                    .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                    .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                    .op_rgb = SG_BLENDOP_ADD,
                    .src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA,
                    .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                    .op_alpha = SG_BLENDOP_ADD,
                },
            },
            .face_winding = SG_FACEWINDING_CCW,
            .cull_mode = SG_CULLMODE_BACK,
        });
    shaders_register_pipeline(*pslot);

    return **pslot;
}

static void screenquad_lazy_init() {
    if (state.initialized) { return; }
    state.initialized = true;

    static const u16 indices[] = { 0, 1, 2, 0, 2, 3 };

    static const f32 vertices[] = {
        // pos       // uv
        0.0f, 0.0f,  0.0f, 1.0f,
        1.0f, 0.0f,  1.0f, 1.0f,
        1.0f, 1.0f,  1.0f, 0.0f,
        0.0f, 1.0f,  0.0f, 0.0f,
    };

    state.ibuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_INDEXBUFFER,
                .data = SG_RANGE(indices)
            });

    state.vbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .data = SG_RANGE(vertices),
            });
}

void screenquad_render(
        sg_pixel_format dst_color_format,
        bool has_depth,
        sg_image src,
        sg_sampler sampler,
        const screenquad_params_t *params) {
    screenquad_lazy_init();

    screenquad_vs_params_t vs_params;
    screenquad_mats(&vs_params.model, &vs_params.view, &vs_params.proj);

    screenquad_fs_params_t fs_params = { 0 };
    fs_params.tint = params->tint;
    fs_params.transparency = params->transparency;

    screenquad_render_ex(
        screenquad_get_pipeline(SHADER_SCREENQUAD, dst_color_format, has_depth),
        &(sg_bindings) {
            .fs.images[SLOT_screenquad_tex] = src,
            .fs.samplers[SLOT_screenquad_smp] = sampler,
        },
        SLOT_screenquad_vs_params,
        SG_RANGE_REF(vs_params),
        SLOT_screenquad_fs_params,
        SG_RANGE_REF(fs_params));
}

void screenquad_mats(m4 *model, m4 *view, m4 *proj) {
    *model = m4_identity();
    *view = m4_identity();
    *proj = cam_ortho(0.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f);
}

void screenquad_render_ex(
        sg_pipeline pipeline,
        const sg_bindings *bindings,
        int vs_slot,
        const sg_range *vs_params,
        int fs_slot,
        const sg_range *fs_params) {
    screenquad_lazy_init();

    sg_apply_pipeline(pipeline);
    sg_bindings bind = *bindings;
    bind.index_buffer = state.ibuf;
    bind.vertex_buffers[0] = state.vbuf;
    sg_apply_bindings(&bind);
    if (vs_params) { sg_apply_uniforms(SG_SHADERSTAGE_VS, vs_slot, vs_params); }
    if (fs_params) { sg_apply_uniforms(SG_SHADERSTAGE_FS, fs_slot, fs_params); }
    sg_draw(0, 6, 1);
}
