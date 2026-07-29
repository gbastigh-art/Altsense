#include "gfx/sprite.h"
#include "gfx/renderer.h"
#include "gfx/tex_atlas.h"
#include "gfx/shaders.h"
#include "game.h"

#include "shader/batch.glsl.h"

typedef struct {
    v4 color;
    v2 offset;
    v2 scale;
    v2 uv_min;
    v2 uv_max;
    f32 z;
    f32 flags; // i32_bits_to_f32
    f32 tex;   // i32_bits_to_f32
} batch_instance_t;

static struct {
    bool initialized;
    sg_buffer ibuf, vbuf, instbuf;
    sg_pipeline *pipelines[_SG_PIXELFORMAT_NUM][2 /* depth? */];
} state;
RELOAD_STATIC_GLOBAL(state)

static void sprite_lazy_init() {
    if (state.initialized) { return; }
    state.initialized = true;

    state.instbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .size = 4 * 1024 * 1024,
                .usage = SG_USAGE_STREAM
            });

    static const u16 indices[] = { 0, 1, 3, 1, 2, 3 };

    static const f32 vertices[] = {
        // pos       // uv
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f,
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

static sg_pipeline sprite_get_pipeline(
        sg_pixel_format pixel_format,
        bool has_depth) {

    sg_pipeline **pslot = &state.pipelines[pixel_format][has_depth ? 1 : 0];

    if (*pslot) { return **pslot; }

    // create pipeline
    *pslot = mem_alloc(&g->arena, sizeof(**pslot));
    **pslot =
        sg_make_pipeline(
            &(sg_pipeline_desc) {
                .shader = shaders_get(SHADER_BATCH),
                .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
                .index_type = SG_INDEXTYPE_UINT16,
                .layout = {
                    .buffers[1] = {
                        .stride = sizeof(batch_instance_t),
                        .step_func = SG_VERTEXSTEP_PER_INSTANCE,
                    },
                    .attrs = {
                        [ATTR_batch_vs_a_position] = {
                            .format = SG_VERTEXFORMAT_FLOAT2,
                            .buffer_index = 0,
                        },
                        [ATTR_batch_vs_a_texcoord0] = {
                            .format = SG_VERTEXFORMAT_FLOAT2,
                            .buffer_index = 0,
                        },
                        [ATTR_batch_vs_a_offset] = {
                            .format = SG_VERTEXFORMAT_FLOAT2,
                            .offset = offsetof(batch_instance_t, offset),
                            .buffer_index = 1,
                        },
                        [ATTR_batch_vs_a_scale] = {
                            .format = SG_VERTEXFORMAT_FLOAT2,
                            .offset = offsetof(batch_instance_t, scale),
                            .buffer_index = 1,
                        },
                        [ATTR_batch_vs_a_uv_min] = {
                            .format = SG_VERTEXFORMAT_FLOAT2,
                            .offset = offsetof(batch_instance_t, uv_min),
                            .buffer_index = 1,
                        },
                        [ATTR_batch_vs_a_uv_max] = {
                            .format = SG_VERTEXFORMAT_FLOAT2,
                            .offset = offsetof(batch_instance_t, uv_max),
                            .buffer_index = 1,
                        },
                        [ATTR_batch_vs_a_color] = {
                            .format = SG_VERTEXFORMAT_FLOAT4,
                            .offset = offsetof(batch_instance_t, color),
                            .buffer_index = 1,
                        },
                        [ATTR_batch_vs_a_z] = {
                            .format = SG_VERTEXFORMAT_FLOAT,
                            .offset = offsetof(batch_instance_t, z),
                            .buffer_index = 1,
                        },
                        [ATTR_batch_vs_a_flags] = {
                            .format = SG_VERTEXFORMAT_FLOAT,
                            .offset = offsetof(batch_instance_t, flags),
                            .buffer_index = 1,
                        },
                        [ATTR_batch_vs_a_tex] = {
                            .format = SG_VERTEXFORMAT_FLOAT,
                            .offset = offsetof(batch_instance_t, tex),
                            .buffer_index = 1,
                        },
                    }
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
                .depth = {
                    .pixel_format =
                        has_depth ?
                            SG_PIXELFORMAT_DEPTH
                            : SG_PIXELFORMAT_NONE,
                    .compare = SG_COMPAREFUNC_LESS_EQUAL,
                    .write_enabled = true,
                },
                .face_winding = SG_FACEWINDING_CCW,
                // TODO: bit of a hack, otherwise virtual textures break on
                // metal - but necessary so that the textures can be flipped
                // like all uv coords expect...
                .cull_mode = SG_CULLMODE_NONE,
            });
    shaders_register_pipeline(*pslot);

    return **pslot;
}

void sprite_batch_render(
        sg_attachments target,
        const sprite_t *sprites,
        int count,
        const m4 *proj,
        const m4 *view) {
    if (count <= 0) { return; }

    sprite_lazy_init();

    // convert sprites -> batch_instance_t
    DYNLIST(batch_instance_t) instances =
        dynlist_create(batch_instance_t, &g->frame_arena, count);

    for (int i = 0; i < count; i++) {
        const sprite_t *sprite = &sprites[i];
        const tex_atlas_entry_t *entry =
            tex_atlas_entry_by_name(sprite->tex);

        box2f_t box_uv = entry->box_uv;

        if (v2_eqv(sprite->box.min, v2_of(0))
            && v2_eqv(sprite->box.max, v2_of(0))) {
            box_uv = entry->box_uv;
        } else {
            // sprite->box is specified in sprite-local UV units - scale to
            // apply to sprite box by interpolation in sprite box
            const v2 mi = box_uv.min, ma = box_uv.max;
            box_uv =
                box2f_mm(
                    v2_of(
                        lerp(mi.x, ma.x, sprite->box.min.x),
                        lerp(mi.y, ma.y, sprite->box.min.y)),
                    v2_of(
                        lerp(mi.x, ma.x, sprite->box.max.x),
                        lerp(mi.y, ma.y, sprite->box.max.y)));

        }

        *dynlist_push(instances) =
            (batch_instance_t) {
                .offset = sprite->pos,
                .scale =
                    v2_mul(
                        v2_div(
                            box2f_size(box_uv),
                            v2_of(1.0f / TEX_ATLAS_SIZE)),
                        sprite->scale),
                .uv_min = box_uv.min,
                .uv_max = box_uv.max,
                .color = sprite->color,
                .z = sprite->z,
                .flags = i32_bits_to_f32(sprite->flags),
                .tex = i32_bits_to_f32(entry->id.index),
            };
    }

    const int offset =
        sg_append_buffer(
            state.instbuf,
            &(sg_range) {
                .ptr = instances,
                .size = dynlist_size_bytes(instances)
            });
    ASSERT(!sg_query_buffer_overflow(state.instbuf));

    const sg_attachments_desc att_desc = sg_query_attachments_desc(target);
    const sg_pipeline pipeline =
        sprite_get_pipeline(
            sg_query_image_desc(att_desc.colors[0].image).pixel_format,
            att_desc.depth_stencil.image.id != 0);

    sg_apply_pipeline(pipeline);
        
    batch_vs_params_t vs_params;
    vs_params.model = m4_identity();
    vs_params.view = *view;
    vs_params.proj = *proj;

    batch_fs_params_t fs_params;
    fs_params.tick = g->tick;

    sg_bindings bindings = {
        .fs = {
            .images = {
                // [SLOT_batch_palette] = g_palette.map_image,
            },
            .samplers[SLOT_batch_smp_nearest] = g_renderer->smp_nearest,
        },
        .index_buffer = state.ibuf,
        .vertex_buffers[0] = state.vbuf,
        .vertex_buffers[1] = state.instbuf,
        .vertex_buffer_offsets[1] = offset,
    };
    tex_atlas_apply_bindings(pipeline, &bindings);
    sg_apply_bindings(&bindings);
    sg_apply_uniforms(
        SG_SHADERSTAGE_VS, SLOT_batch_vs_params, SG_RANGE_REF(vs_params));
    sg_apply_uniforms(
        SG_SHADERSTAGE_FS, SLOT_batch_fs_params, SG_RANGE_REF(fs_params));
    sg_draw(0, 6, count);
}
