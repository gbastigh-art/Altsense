#include "gfx/renderer.h"
#include "editor/editor.h"
#include "gfx/scale_blur.h"
#include "gfx/edge.h"
#include "gfx/tex_atlas.h"
#include "gfx/debug_draw.h"
#include "gfx/palette.h"
#include "gfx/passes.h"
#include "gfx/model.h"
#include "gfx/screenquad.h"
#include "main_menu.h"
#include "platform.h"
#include "gfx/shaders.h"
#include "level/block.h"
#include "level/decal.h"
#include "level/level.h"
#include "level/particle.h"
#include "level/portal.h"
#include "level/sector.h"
#include "level/side.h"
#include "level/wall.h"
#include "level/entity.h"
#include "ext/cimgui.h"
#include "particle_sim.h"
#include "vtext.h"
#include "game.h"
#include "util/cube.h"
#include "util/time.h"
#include "util/fixlist.h"
#include "reloadhost.h"

// renderer instance
static renderer_t *r = NULL;
RELOAD_STATIC_GLOBAL(r)

renderer_t *g_renderer = NULL;
RELOAD_STATIC_GLOBAL(g_renderer)

static bool show_debug_ui = false;
RELOAD_STATIC_GLOBAL(show_debug_ui)

// forward decls
static void do_lights_pass();

#define LEVEL_VBUF_SIZE             (32 * 1024 * 1024)
#define LEVEL_IBUF_SIZE             (16 * 1024 * 1024)
#define MODEL_VBUF_SIZE             (32 * 1024 * 1024)
#define MODEL_IBUF_SIZE             (16 * 1024 * 1024)
#define SPRITE_INSTBUF_SIZE         16384
#define PARTICLE_INSTBUF_SIZE       32768
#define FANCY_PARTICLE_INSTBUF_SIZE 32768
#define MODEL_INSTBUF_SIZE          32768

#include "shader/shadow_map.glsl.h"
#include "shader/level.glsl.h"
#include "shader/sprite.glsl.h"
#include "shader/particle.glsl.h"
#include "shader/fancy_particle.glsl.h"
#include "shader/model.glsl.h"
#include "shader/composite.glsl.h"
#include "shader/post0.glsl.h"
#include "shader/post1.glsl.h"
#include "shader/post2.glsl.h"

// vertex for level pipeline (see level.glsl)
typedef struct level_vertex {
	v3 pos;
    v3 n_w;
	v2 uv;
	f32 buffer_index; // i32_bits_to_f32
	f32 type_flags;   // i32_bits_to_f32
} level_vertex_t;

// vertex for sprite pipeline (see sprite.glsl)
typedef struct {
	v3 pos;
	v2 uv;
} sprite_vertex_t;

// instance for sprite pipeline (see sprite.glsl)
// optimization opportunity: pack this
typedef struct sprite_inst {
	v3 pos;
	v2 size;
    f32 color;        // i32_bits_to_f32 / v4_to_u8x4
    f32 hsva;         // i32_bits_to_f32 / v4_to_u8x4
    f32 id;           // i32_bits_to_f32
    f32 buffer_index; // i32_bits_to_f32
    f32 type_flags;   // i32_bits_to_f32
    f32 flags;        // i32_bits_to_f32
    f32 tex_id;       // i32_bits_to_f32
    f32 rotation;
} sprite_inst_t;

// sprite instance buffer
static FIXLIST(sprite_inst_t, SPRITE_INSTBUF_SIZE) sprite_inst_buf;

// see particle.glsl
typedef sprite_vertex_t particle_vertex_t;

typedef struct particle_inst {
    v3 pos;
    v3 vel;
    v3 dir;
    v2 size;
    f32 id;     // i32_bits_to_f32
    f32 type;   // i32_bits_to_f32
    f32 tex_id; // i32_bits_to_f32
    f32 flags;  // i32_bits_to_f32
    f32 start;  // i32_bits_to_f32
    f32 color;  // v4_to_f32
    f32 hsva;   // v4_to_f32
} particle_inst_t;

// fancy particle instance buffer
static FIXLIST(particle_inst_t, PARTICLE_INSTBUF_SIZE) particle_inst_buf;

// see fancy_particle.glsl
typedef struct {
    v3 pos;
    v3 normal;
    v2 uv;
    f32 vertex_index;
} fancy_particle_vertex_t;

typedef struct fancy_particle_inst {
    f32 type; // i32_bits_to_f32
    f32 id;   // i32_bits_to_f32
    v3 pos;
    v3 scale;
    v3 vel;
} fancy_particle_inst_t;

// fancy particle instance buffer
static FIXLIST(fancy_particle_inst_t, FANCY_PARTICLE_INSTBUF_SIZE)
    fancy_particle_inst_buf;

// see model.glsl
typedef struct {
    f32 id;               // i32_bits_to_f32
    f32 buffer_index;     // i32_bits_to_f32
    f32 model_flags;      // i32_bits_to_f32
    f32 model_type_flags; // i32_bits_to_f32
    v4 t0, t1, t2, t3;
} model_inst_t;

static FIXLIST(model_inst_t, MODEL_INSTBUF_SIZE) model_inst_buf;

typedef struct prepared_model {
    model_t model;
    int buffer_index;
    const model_index_group_t *index_group;
} prepared_model_t;

// apply bindings for @include_block level_buffers for a pipeline into the
// specified bindings block
static void apply_level_data_bindings(
        sg_pipeline pip,
        sg_bindings *bindings) {
    const shader_refl_t *refl = shader_reflect(shader_for_pipeline(pip));

    for (int i = 0; i < 2; i++) {
        sg_shader_stage stage;
        sg_stage_bindings *stage_bindings;

        if (i == 0) {
             stage = SG_SHADERSTAGE_VS;
             stage_bindings = &bindings->vs;
        } else {
             stage = SG_SHADERSTAGE_FS;
             stage_bindings = &bindings->fs;
        }

        for (int j = 0; j < ARRLEN(r->render_data.arrays); j++) {
            const renderer_data_array_t *arr = &r->render_data.arrays[j];

            const int slot =
                refl->storagebuffer_slot_fn(
                    stage,
                    mem_strfmt(
                        &g->frame_arena,
                        "%s_render_data_buffer",
                        arr->name));

            if (slot != -1) {
                stage_bindings->storage_buffers[slot] = arr->buffer;
            }
        }
    }
}

// apply bindings for @include block light
static void apply_light_data_bindings(
    sg_pipeline pip,
    sg_bindings *bindings) {
    const shader_refl_t *refl = shader_reflect(shader_for_pipeline(pip));

    for (int i = 0; i < 2; i++) {
        sg_shader_stage stage;
        sg_stage_bindings *stage_bindings;

        if (i == 0) {
             stage = SG_SHADERSTAGE_VS;
             stage_bindings = &bindings->vs;
        } else {
             stage = SG_SHADERSTAGE_FS;
             stage_bindings = &bindings->fs;
        }

        int slot;

        slot = refl->storagebuffer_slot_fn(stage, "light_buffer");
        if (slot != -1) {
            stage_bindings->storage_buffers[slot] = r->light.buffer;
        }

        slot = refl->storagebuffer_slot_fn(stage, "block_walls_buffer");
        if (slot != -1) {
            stage_bindings->storage_buffers[slot] = r->blocks.data_buffer;
        }

        slot = refl->storagebuffer_slot_fn(stage, "block_walls_indices_buffer");
        if (slot != -1) {
            stage_bindings->storage_buffers[slot] = r->blocks.indices_buffer;
        }
    }
}

// marks an index dirty in a data array, potentially expanding the current dirty
// range
static void mark_data_array_dirty(renderer_data_array_t *arr, int index) {
    arr->max_index_for_level = max(arr->max_index_for_level, index);

    if (arr->dirty.min_index == -1 || arr->dirty.max_index == -1) {
        ASSERT_DEBUG(arr->dirty.min_index == -1 && arr->dirty.max_index == -1);
        arr->dirty.min_index = index;
        arr->dirty.max_index = index;
    } else {
        arr->dirty.min_index = min(arr->dirty.min_index, index);
        arr->dirty.max_index = max(arr->dirty.max_index, index);
    }
}

// (re)create all renderer pipelines
static void make_pipelines() {
    // reset existing pipelines
    for (uint i = 0; i < ARRLEN(r->pipelines.arr); i++) {
        if (r->pipelines.arr[i].id) {
            shaders_unregister_pipeline(&r->pipelines.arr[i]);
            sg_destroy_pipeline(r->pipelines.arr[i]);
            r->pipelines.arr[i] = (sg_pipeline) { 0 };
        }
    }

    for (int stencil = 0; stencil < ARRLEN(r->pipelines.stencil); stencil++) {
        for (int i = 0; i < ARRLEN(r->pipelines.stencil[0].arr); i++) {
            sg_pipeline *pip = &r->pipelines.stencil[stencil].arr[i];

            if (pip->id) {
                shaders_unregister_pipeline(pip);
                sg_destroy_pipeline(*pip);
                *pip = (sg_pipeline) { 0 };
            }
        }
    }

    // base level pipeline description
    const sg_pipeline_desc desc_level_base = {
        .shader = shaders_get(SHADER_LEVEL),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .buffers[0] = {
                .stride = sizeof(level_vertex_t),
            },
            .attrs = {
                [ATTR_level_vs_a_position] = {
                    .offset = offsetof(level_vertex_t, pos),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_level_vs_a_n_w] = {
                    .offset = offsetof(level_vertex_t, n_w),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_level_vs_a_texcoord0] = {
                    .offset = offsetof(level_vertex_t, uv),
                    .format = SG_VERTEXFORMAT_FLOAT2,
                },
                [ATTR_level_vs_a_buffer_index] = {
                    .offset = offsetof(level_vertex_t, buffer_index),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_level_vs_a_type_flags] = {
                    .offset = offsetof(level_vertex_t, type_flags),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
            }
        },
        .colors = {
            [0] = {
                .pixel_format =
                    sg_query_image_desc(
                        g_passes.deferred.color).pixel_format,
            },
            [1] = {
                .pixel_format =
                    sg_query_image_desc(
                        g_passes.deferred.pos_w_id).pixel_format,
            },
            [2] = {
                .pixel_format =
                    sg_query_image_desc(
                        g_passes.deferred.pos_v_index_type_flags).pixel_format,
            },
            [3] = {
                .pixel_format =
                    sg_query_image_desc(
                        g_passes.deferred.normal_uv).pixel_format,
            },
        },
        .color_count = 4,
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS,
            .pixel_format =
                sg_query_image_desc(
                    g_passes.deferred.depth_stencil).pixel_format,
        },
        .stencil = {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_GREATER_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .back = {
                .compare = SG_COMPAREFUNC_NEVER,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .ref = 0,
            .write_mask = 0xFF,
            .read_mask = 0xFF
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    };

    // base sprite pipeline description
    const sg_pipeline_desc desc_sprite_base = {
        .shader = shaders_get(SHADER_SPRITE),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .buffers = {
                [0] = {
                    .stride = sizeof(sprite_vertex_t),
                    .step_rate = SG_VERTEXSTEP_PER_VERTEX,
                },
                [1] = {
                    .stride = sizeof(sprite_inst_t),
                    .step_rate = 1,
                    .step_func = SG_VERTEXSTEP_PER_INSTANCE,
                }
            },
            .attrs = {
                [ATTR_sprite_vs_a_position] = {
                    .buffer_index = 0,
                    .offset = offsetof(sprite_vertex_t, pos),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_sprite_vs_a_texcoord0] = {
                    .buffer_index = 0,
                    .offset = offsetof(sprite_vertex_t, uv),
                    .format = SG_VERTEXFORMAT_FLOAT2,
                },
                [ATTR_sprite_vs_a_color] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, color),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_sprite_vs_a_hsva] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, hsva),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_sprite_vs_a_pos] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, pos),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_sprite_vs_a_size] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, size),
                    .format = SG_VERTEXFORMAT_FLOAT2,
                },
                [ATTR_sprite_vs_a_id] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, id),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_sprite_vs_a_buffer_index] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, buffer_index),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_sprite_vs_a_type_flags] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, type_flags),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_sprite_vs_a_flags] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, flags),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_sprite_vs_a_tex_id] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, tex_id),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_sprite_vs_a_rotation] = {
                    .buffer_index = 1,
                    .offset = offsetof(sprite_inst_t, rotation),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
            }
        },
        .colors = {
            [0] = desc_level_base.colors[0],
            [1] = desc_level_base.colors[1],
            [2] = desc_level_base.colors[2],
            [3] = desc_level_base.colors[3],
        },
        .color_count = desc_level_base.color_count,
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .pixel_format = desc_level_base.depth.pixel_format,
        },
        .stencil = {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_LESS_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .ref = 0,
            .write_mask = 0xFF,
            .read_mask = 0xFF
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    };

    // base particle pipeline description
    const sg_pipeline_desc desc_particle_base = {
        .shader = shaders_get(SHADER_PARTICLE),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .buffers = {
                [0] = {
                    .stride = sizeof(particle_vertex_t),
                    .step_rate = SG_VERTEXSTEP_PER_VERTEX,
                },
                [1] = {
                    .stride = sizeof(particle_inst_t),
                    .step_rate = 1,
                    .step_func = SG_VERTEXSTEP_PER_INSTANCE,
                },
            },
            .attrs = {
                [ATTR_particle_vs_a_pos] = {
                    .buffer_index = 0,
                    .offset = offsetof(particle_vertex_t, pos),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_particle_vs_a_uv] = {
                    .buffer_index = 0,
                    .offset = offsetof(particle_vertex_t, uv),
                    .format = SG_VERTEXFORMAT_FLOAT2,
                },
                [ATTR_particle_vs_i_pos] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, pos),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_particle_vs_i_vel] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, vel),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_particle_vs_i_dir] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, dir),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_particle_vs_i_size] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, size),
                    .format = SG_VERTEXFORMAT_FLOAT2,
                },
                [ATTR_particle_vs_i_id] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, id),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_particle_vs_i_type] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, type),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_particle_vs_i_tex_id] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, tex_id),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_particle_vs_i_flags] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, flags),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_particle_vs_i_start] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, start),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_particle_vs_i_color] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, color),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_particle_vs_i_hsva] = {
                    .buffer_index = 1,
                    .offset = offsetof(particle_inst_t, hsva),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
            }
        },
        .colors = {
            [0] = desc_level_base.colors[0],
            [1] = desc_level_base.colors[1],
            [2] = desc_level_base.colors[2],
            [3] = desc_level_base.colors[3],
        },
        .color_count = desc_level_base.color_count,
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .pixel_format = desc_level_base.depth.pixel_format,
        },
        .stencil = {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_LESS_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .ref = 0,
            .write_mask = 0xFF,
            .read_mask = 0xFF
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    };

    const sg_pipeline_desc desc_model_base = {
        .shader = shaders_get(SHADER_MODEL),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .buffers = {
                [0] = {
                    .stride = sizeof(model_vertex_t),
                    .step_rate = SG_VERTEXSTEP_PER_VERTEX,
                },
                [1] = {
                    .stride = sizeof(model_inst_t),
                    .step_rate = 1,
                    .step_func = SG_VERTEXSTEP_PER_INSTANCE,
                },
            },
            .attrs = {
                [ATTR_model_vs_a_position] = {
                    .buffer_index = 0,
                    .offset = offsetof(model_vertex_t, pos),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_model_vs_a_normal] = {
                    .buffer_index = 0,
                    .offset = offsetof(model_vertex_t, normal),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_model_vs_a_texcoord0] = {
                    .buffer_index = 0,
                    .offset = offsetof(model_vertex_t, uv),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_model_vs_a_id] = {
                    .buffer_index = 1,
                    .offset = offsetof(model_inst_t, id),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_model_vs_a_buffer_index] = {
                    .buffer_index = 1,
                    .offset = offsetof(model_inst_t, buffer_index),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_model_vs_a_model_flags] = {
                    .buffer_index = 1,
                    .offset = offsetof(model_inst_t, model_flags),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_model_vs_a_model_type_flags] = {
                    .buffer_index = 1,
                    .offset = offsetof(model_inst_t, model_type_flags),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_model_vs_a_t0] = {
                    .buffer_index = 1,
                    .offset = offsetof(model_inst_t, t0),
                    .format = SG_VERTEXFORMAT_FLOAT4,
                },
                [ATTR_model_vs_a_t1] = {
                    .buffer_index = 1,
                    .offset = offsetof(model_inst_t, t1),
                    .format = SG_VERTEXFORMAT_FLOAT4,
                },
                [ATTR_model_vs_a_t2] = {
                    .buffer_index = 1,
                    .offset = offsetof(model_inst_t, t2),
                    .format = SG_VERTEXFORMAT_FLOAT4,
                },
                [ATTR_model_vs_a_t3] = {
                    .buffer_index = 1,
                    .offset = offsetof(model_inst_t, t3),
                    .format = SG_VERTEXFORMAT_FLOAT4,
                },
            }
        },
        .colors = {
            [0] = desc_level_base.colors[0],
            [1] = desc_level_base.colors[1],
            [2] = desc_level_base.colors[2],
            [3] = desc_level_base.colors[3],
        },
        .color_count = desc_level_base.color_count,
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .pixel_format = desc_level_base.depth.pixel_format,
        },
        .stencil = {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_LESS_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .back = {
                .compare = SG_COMPAREFUNC_LESS_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .ref = 0,
            .write_mask = 0xFF,
            .read_mask = 0xFF,
        },
        .face_winding = SG_FACEWINDING_CCW,
        // TODO: undo this at some point, I just want to be able to see the
        // inside of bullets while they fly
        .cull_mode = SG_CULLMODE_NONE,
    };

    // base fancy particle pipeline description
    const sg_pipeline_desc desc_fancy_particle_base = {
        .shader = shaders_get(SHADER_FANCY_PARTICLE),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .buffers = {
                [0] = {
                    .stride = sizeof(fancy_particle_vertex_t),
                    .step_rate = SG_VERTEXSTEP_PER_VERTEX,
                },
                [1] = {
                    .stride = sizeof(fancy_particle_inst_t),
                    .step_rate = 1,
                    .step_func = SG_VERTEXSTEP_PER_INSTANCE,
                }
            },
            .attrs = {
                [ATTR_fancy_particle_vs_a_pos] = {
                    .buffer_index = 0,
                    .offset = offsetof(fancy_particle_vertex_t, pos),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_fancy_particle_vs_a_normal] = {
                    .buffer_index = 0,
                    .offset = offsetof(fancy_particle_vertex_t, normal),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_fancy_particle_vs_a_uv] = {
                    .buffer_index = 0,
                    .offset = offsetof(fancy_particle_vertex_t, uv),
                    .format = SG_VERTEXFORMAT_FLOAT2,
                },
                [ATTR_fancy_particle_vs_a_vertex_index] = {
                    .buffer_index = 0,
                    .offset = offsetof(fancy_particle_vertex_t, vertex_index),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_fancy_particle_vs_i_type] = {
                    .buffer_index = 1,
                    .offset = offsetof(fancy_particle_inst_t, type),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_fancy_particle_vs_i_id] = {
                    .buffer_index = 1,
                    .offset = offsetof(fancy_particle_inst_t, id),
                    .format = SG_VERTEXFORMAT_FLOAT,
                },
                [ATTR_fancy_particle_vs_i_pos] = {
                    .buffer_index = 1,
                    .offset = offsetof(fancy_particle_inst_t, pos),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_fancy_particle_vs_i_scale] = {
                    .buffer_index = 1,
                    .offset = offsetof(fancy_particle_inst_t, scale),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
                [ATTR_fancy_particle_vs_i_vel] = {
                    .buffer_index = 1,
                    .offset = offsetof(fancy_particle_inst_t, vel),
                    .format = SG_VERTEXFORMAT_FLOAT3,
                },
            }
        },
        .colors = {
            [0] = desc_level_base.colors[0],
            [1] = desc_level_base.colors[1],
            [2] = desc_level_base.colors[2],
            [3] = desc_level_base.colors[3],
        },
        .color_count = desc_level_base.color_count,
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .pixel_format = desc_level_base.depth.pixel_format,
        },
        .stencil = {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_LESS_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .ref = 0,
            .write_mask = 0xFF,
            .read_mask = 0xFF
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    };

    // create stencil pipelines
    for (int stencil = 0;
         stencil < ARRLEN(r->pipelines.stencil);
         stencil++) {
        // create temp desc which does not write to anything/has nothing enabled
        sg_pipeline_desc desc_level_no_write_base = desc_level_base;
        desc_level_no_write_base.depth.write_enabled = false;
        desc_level_no_write_base.stencil.enabled = false;
        for (int i = 0; i < 5; i++) {
            desc_level_no_write_base.colors[i].write_mask = SG_COLORMASK_NONE;
        }

        // desc incr/outline (first pass) draws where stencil is EQUAL to ref
        // (to only draw inside of current portals and INCR on depth pass
        // (increment VISIBLE portal area)
        sg_pipeline_desc desc_level_incr = desc_level_no_write_base;
        desc_level_incr.depth.write_enabled = true;
        desc_level_incr.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc_level_incr.stencil = (sg_stencil_state) {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_INCR_CLAMP,
            },
            .ref = stencil,
            .write_mask = 0xFF,
            .read_mask = 0xFF,
        };

        // desc depth (second pass) paints proper depth over the screen where
        // things have been drawn inside of the portal (at stencil + 1)
        sg_pipeline_desc desc_level_depth = desc_level_no_write_base;
        desc_level_depth = desc_level_no_write_base;
        desc_level_depth.depth.write_enabled = true;
        desc_level_depth.depth.compare = SG_COMPAREFUNC_ALWAYS;
        desc_level_depth.stencil = (sg_stencil_state) {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .ref = stencil + 1,
            .write_mask = 0xFF,
            .read_mask = 0xFF,
        };

        // desc decrement (third pass) decrements on failure to be equal to
        // current stencil ref (where things have been drawn inside the portal
        // to stencil ref + 1) to reset to the original stencil value
        sg_pipeline_desc desc_level_decr = desc_level_no_write_base;
        desc_level_decr.depth.write_enabled = false;
        desc_level_decr.depth.compare = SG_COMPAREFUNC_ALWAYS;
        desc_level_decr.stencil = (sg_stencil_state) {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_EQUAL,
                .fail_op = SG_STENCILOP_DECR_CLAMP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .ref = stencil,
            .write_mask = 0xFF,
            .read_mask = 0xFF,
        };

        // default desc will draw where LESS_EQUAL to this stencil, used for
        // drawing regular level geometry. also writes to color buffers!
        sg_pipeline_desc desc_level_reg = desc_level_base;
        desc_level_reg.stencil = (sg_stencil_state) {
            .enabled = true,
            .front = {
                .compare = SG_COMPAREFUNC_LESS_EQUAL,
                .fail_op = SG_STENCILOP_KEEP,
                .depth_fail_op = SG_STENCILOP_KEEP,
                .pass_op = SG_STENCILOP_KEEP,
            },
            .ref = stencil,
            .write_mask = 0xFF,
            .read_mask = 0xFF,
        };

        sg_pipeline_desc desc_model = desc_model_base;
        desc_model.stencil.ref = stencil;

        sg_pipeline_desc desc_sprite = desc_sprite_base;
        desc_sprite.stencil.ref = stencil;

        sg_pipeline_desc desc_particle = desc_particle_base;
        desc_particle.stencil.ref = stencil;

        sg_pipeline_desc desc_fancy_particle = desc_fancy_particle_base;
        desc_fancy_particle.stencil.ref = stencil;

        r->pipelines.stencil[stencil].level_incr =
            sg_make_pipeline(&desc_level_incr);
        shaders_register_pipeline(&r->pipelines.stencil[stencil].level_incr);

        r->pipelines.stencil[stencil].level_depth =
            sg_make_pipeline(&desc_level_depth);
        shaders_register_pipeline(&r->pipelines.stencil[stencil].level_depth);

        r->pipelines.stencil[stencil].level_decr =
            sg_make_pipeline(&desc_level_decr);
        shaders_register_pipeline(&r->pipelines.stencil[stencil].level_decr);

        r->pipelines.stencil[stencil].level_reg =
            sg_make_pipeline(&desc_level_reg);
        shaders_register_pipeline(&r->pipelines.stencil[stencil].level_reg);

        r->pipelines.stencil[stencil].model = sg_make_pipeline(&desc_model);
        shaders_register_pipeline(&r->pipelines.stencil[stencil].model);

        r->pipelines.stencil[stencil].sprite = sg_make_pipeline(&desc_sprite);
        shaders_register_pipeline(&r->pipelines.stencil[stencil].sprite);

        r->pipelines.stencil[stencil].particle =
            sg_make_pipeline(&desc_particle);
        shaders_register_pipeline(&r->pipelines.stencil[stencil].particle);

        r->pipelines.stencil[stencil].fancy_particle =
            sg_make_pipeline(&desc_fancy_particle);
        shaders_register_pipeline(
            &r->pipelines.stencil[stencil].fancy_particle);
    }

    // phantom models
    sg_pipeline_desc phantom_pip_desc = desc_model_base;
    phantom_pip_desc.shader = shaders_get(SHADER_MODEL_PHANTOM);
    phantom_pip_desc.cull_mode = SG_CULLMODE_BACK;
    phantom_pip_desc.stencil.enabled = false;
    phantom_pip_desc.color_count = 1;
    phantom_pip_desc.colors[0].pixel_format =
        sg_query_image_desc(g_passes.phantom.color).pixel_format;
    phantom_pip_desc.depth = (sg_depth_state) {
        .write_enabled = true,
        .compare = SG_COMPAREFUNC_LESS_EQUAL,
        .pixel_format =
            sg_query_image_desc(g_passes.phantom.depth).pixel_format,
    };
    r->pipelines.phantom = sg_make_pipeline(&phantom_pip_desc);
    shaders_register_pipeline(&r->pipelines.phantom);

    r->pipelines.composite = sg_make_pipeline(&(sg_pipeline_desc) {
        .shader = shaders_get(SHADER_COMPOSITE),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT2,
                [1].format = SG_VERTEXFORMAT_FLOAT2,
            },
        },
        .colors = {
            [0].pixel_format =
                sg_query_image_desc(g_passes.composite.color).pixel_format,
            [1].pixel_format =
                sg_query_image_desc(g_passes.composite.extra_id_pos).pixel_format,
            [2].pixel_format =
                sg_query_image_desc(g_passes.composite.light).pixel_format,
            [3].pixel_format =
                sg_query_image_desc(g_passes.composite.bloom).pixel_format,
        },
        .color_count = 4,
        .depth = {
            .pixel_format = SG_PIXELFORMAT_NONE,
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    });
    shaders_register_pipeline(&r->pipelines.composite);

    r->pipelines.downsample = sg_make_pipeline(&(sg_pipeline_desc) {
        .shader = shaders_get(SHADER_DOWNSAMPLE),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT2,
                [1].format = SG_VERTEXFORMAT_FLOAT2,
            }
        },
        .colors[0].pixel_format =
            sg_query_image_desc(g_passes.bloom.images[0]).pixel_format,
        .color_count = 1,
        .depth = { .pixel_format = SG_PIXELFORMAT_NONE, },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    });
    shaders_register_pipeline(&r->pipelines.downsample);

    r->pipelines.upsample = sg_make_pipeline(&(sg_pipeline_desc) {
        .shader = shaders_get(SHADER_UPSAMPLE),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT2,
                [1].format = SG_VERTEXFORMAT_FLOAT2,
            }
        },
        .colors[0].pixel_format =
            sg_query_image_desc(g_passes.bloom.images[0]).pixel_format,
        .color_count = 1,
        .depth = { .pixel_format = SG_PIXELFORMAT_NONE, },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    });
    shaders_register_pipeline(&r->pipelines.upsample);

    r->pipelines.edge = sg_make_pipeline(&(sg_pipeline_desc) {
        .shader = shaders_get(SHADER_EDGE),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT2,
                [1].format = SG_VERTEXFORMAT_FLOAT2,
            }
        },
        .colors[0].pixel_format =
            sg_query_image_desc(g_passes.edge.color).pixel_format,
        .color_count = 1,
        .depth = { .pixel_format = SG_PIXELFORMAT_NONE, },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    });
    shaders_register_pipeline(&r->pipelines.edge);

    sg_pipeline_desc
        post_pip_desc = {
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .index_type = SG_INDEXTYPE_UINT16,
            .layout = {
                .attrs = {
                    [0].format = SG_VERTEXFORMAT_FLOAT2,
                    [1].format = SG_VERTEXFORMAT_FLOAT2,
                }
            },
            // .colors = { see below }
            // .color_count = { see below }
            .depth = { .pixel_format = SG_PIXELFORMAT_NONE, },
            .face_winding = SG_FACEWINDING_CCW,
            .cull_mode = SG_CULLMODE_BACK,
        },
        post0_pip_desc = post_pip_desc,
        post1_pip_desc = post_pip_desc,
        post2_pip_desc = post_pip_desc;

    post0_pip_desc.shader = shaders_get(SHADER_POST0);

    sg_attachments_desc post0_attach_desc =
        sg_query_attachments_desc(g_passes.post0.attach);
    post0_pip_desc.colors[0] = (sg_color_target_state) {
        .pixel_format =
            sg_query_image_desc(post0_attach_desc.colors[0].image).pixel_format,
    };
    post0_pip_desc.colors[1] = (sg_color_target_state) {
        .pixel_format = sg_query_image_desc(g_passes.post0.light).pixel_format,
    };
    post0_pip_desc.colors[2] = (sg_color_target_state) {
        .pixel_format =
            sg_query_image_desc(g_passes.bloom.images[0]).pixel_format,
    };
    post0_pip_desc.color_count = 3;

    post1_pip_desc.shader = shaders_get(SHADER_POST1);
    post1_pip_desc.colors[0].pixel_format =
        sg_query_image_desc(g_passes.post1.color).pixel_format;
    post1_pip_desc.colors[1].pixel_format =
        sg_query_image_desc(g_passes.blur.images[0]).pixel_format;
    post1_pip_desc.color_count = 2;

    post2_pip_desc.shader = shaders_get(SHADER_POST2);
    post2_pip_desc.colors[0].pixel_format =
        sg_query_image_desc(g_passes.post2.color).pixel_format;
    post2_pip_desc.color_count = 1;

    r->pipelines.post0 = sg_make_pipeline(&post0_pip_desc);
    shaders_register_pipeline(&r->pipelines.post0);

    r->pipelines.post1 = sg_make_pipeline(&post1_pip_desc);
    shaders_register_pipeline(&r->pipelines.post1);

    r->pipelines.post2 = sg_make_pipeline(&post2_pip_desc);
    shaders_register_pipeline(&r->pipelines.post2);

    r->pipelines.shadow = sg_make_pipeline(&(sg_pipeline_desc) {
        .shader = shaders_get(SHADER_SHADOW_MAP),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT2,
                [1].format = SG_VERTEXFORMAT_FLOAT2,
            }
        },
        .colors = {
            [0] = {
                .pixel_format =
                    sg_query_image_desc(g_passes.shadow.image).pixel_format,
            },
        },
        .color_count = 1,
        .depth = {
            .pixel_format = SG_PIXELFORMAT_NONE,
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
    });
    shaders_register_pipeline(&r->pipelines.shadow);
}

// resets all level-related renderer data
static void reset_for_level() {
    r->level = NULL;

    if (allocator_valid(&r->arena)) {
        heap_allocator_destroy(&r->arena);
    }

    heap_allocator_init(&r->arena, &g->arena, NULL);

    r->pixel_data = (typeof(r->pixel_data)) { 0 };

    dynbuf_init(&r->db_indices, &r->arena, LEVEL_IBUF_SIZE);
    dynbuf_init(&r->db_vertices, &r->arena, LEVEL_VBUF_SIZE);

    r->screenshakes          = NULL; dynlist_init(r->screenshakes,          &r->arena);
    r->tints                 = NULL; dynlist_init(r->tints,                 &r->arena);
    r->frame_lights          = NULL; dynlist_init(r->frame_lights,          &r->arena);
    r->env_lights            = NULL; dynlist_init(r->env_lights,            &r->arena);
    r->frame_models          = NULL; dynlist_init(r->frame_models,          &r->arena);
    r->frame_phantom_models  = NULL; dynlist_init(r->frame_phantom_models,  &r->arena);
    r->prepared_frame_models = NULL; dynlist_init(r->prepared_frame_models, &r->arena);
    r->frame_sprites         = NULL; dynlist_init(r->frame_sprites,         &r->arena);

    // lazy init in gfx/light.c
    r->light.info = (map_t) { 0 };

    bitmap_fill(&r->light.indices, 0);

    // reset info arrays up to max used index for each level
    struct { void *ptr; int t_size; int max_index; } info_resets[] = {
        {
            .ptr = &r->render_info.sides[0],
            .max_index = r->render_data.sides.max_index_for_level,
            .t_size = sizeof(r->render_info.sides[0]),
        },
        {
            .ptr = &r->render_info.sectors[0],
            .max_index = r->render_data.sectors.max_index_for_level,
            .t_size = sizeof(r->render_info.sectors[0]),
        },
        {
            .ptr = &r->render_info.decals[0],
            .max_index = r->render_data.decals.max_index_for_level,
            .t_size = sizeof(r->render_info.decals[0]),
        },
    };

    for (int i = 0; i < ARRLEN(info_resets); i++) {
        memset(
            info_resets[i].ptr,
            0,
            info_resets[i].t_size * (info_resets[i].max_index + 1));
    }

    for (int i = 0; i < ARRLEN(r->render_data.arrays); i++) {
        r->render_data.arrays[i].max_index_for_level = 0;
    }
}

void renderer_reset() {
    reset_for_level();
    make_pipelines();
}

void renderer_init() {
    r = mem_calloc(&g->arena, sizeof(*r));
    g_renderer = r;

    r->smp_nearest =
        sg_make_sampler(
            &(sg_sampler_desc) {
                .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
                .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
                .wrap_w = SG_WRAP_CLAMP_TO_EDGE,
                .min_filter = SG_FILTER_NEAREST,
                .mag_filter = SG_FILTER_NEAREST,
                .label = "sampler.nearest",
            });

    r->smp_linear =
        sg_make_sampler(
            &(sg_sampler_desc) {
                .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
                .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
                .wrap_w = SG_WRAP_CLAMP_TO_EDGE,
                .min_filter = SG_FILTER_LINEAR,
                .mag_filter = SG_FILTER_LINEAR,
                .label = "sampler.linear",
            });

    renderer_reset();

    r->level_ibuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_INDEXBUFFER,
                .size = LEVEL_IBUF_SIZE,
                .usage = SG_USAGE_STREAM
            });

    r->level_vbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .size = LEVEL_VBUF_SIZE,
                .usage = SG_USAGE_STREAM
            });

    static const u16 obj_indices[6] = { 0, 1, 2, 2, 3, 0 };

    static const sprite_vertex_t obj_vertices[4] = {
        [0] = {
            .pos = v3_of(0, 0, 0),
            .uv = v2_of(0, 0),
        },
        [1] = {
            .pos = v3_of(1, 0, 0),
            .uv = v2_of(1, 0),
        },
        [2] = {
            .pos = v3_of(1, 0, 1),
            .uv = v2_of(1, 1),
        },
        [3] = {
            .pos = v3_of(0, 0, 1),
            .uv = v2_of(0, 1),
        },
    };

    r->sprite_ibuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_INDEXBUFFER,
                .data = SG_RANGE(obj_indices)
            });

    r->sprite_vbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .data = SG_RANGE(obj_vertices)
            });

    r->sprite_instbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_STREAM,
                .size = SPRITE_INSTBUF_SIZE * sizeof(sprite_inst_t),
            });

    r->particle_instbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_STREAM,
                .size = PARTICLE_INSTBUF_SIZE * sizeof(particle_inst_t),
            });

    DYNLIST(u16) cube_indices = dynlist_create(u16, &g->frame_arena);
    DYNLIST(fancy_particle_vertex_t) cube_vertices =
        dynlist_create(fancy_particle_vertex_t, &g->frame_arena);

    for (int face = 0; face < 6; face++) {
        const int base = dynlist_size(cube_vertices);
        for (int i = 0; i < 4; i++) {
            *dynlist_push(cube_vertices) = (fancy_particle_vertex_t) {
                .pos =
                    CUBE_VERTICES[
                        CUBE_INDICES[face][CUBE_UNIQUE_INDICES[i]]],
                .normal = CUBE_NORMALS[face],
                .uv = CUBE_UVS[face][i],
                .vertex_index =
                    i32_bits_to_f32(
                        CUBE_INDICES[face][CUBE_UNIQUE_INDICES[i]]),
            };
        }

        for (int i = 0; i < 6; i++) {
            *dynlist_push(cube_indices) = base + CUBE_FACE_INDICES[i];
        }
    }

    r->fancy_particle_ibuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_INDEXBUFFER,
                .data = sg_range_from_dynlist(cube_indices),
            });

    r->fancy_particle_vbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .data = sg_range_from_dynlist(cube_vertices),
            });

    r->fancy_particle_instbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_STREAM,
                .size =
                    FANCY_PARTICLE_INSTBUF_SIZE
                        * sizeof(fancy_particle_inst_t),
            });

    r->model_instbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_STREAM,
                .size = MODEL_INSTBUF_SIZE * sizeof(model_inst_t),
            });

    r->blocks.data_buffer =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .label = "blocks.data.buf",
                .type = SG_BUFFERTYPE_STORAGEBUFFER,
                .size = sizeof(r->blocks.data),
                .usage = SG_USAGE_STREAM,
            });

    r->blocks.indices_buffer =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .label = "blocks.indices.buf",
                .type = SG_BUFFERTYPE_STORAGEBUFFER,
                .size = sizeof(r->blocks.indices),
                .usage = SG_USAGE_STREAM,
            });

    r->light.buffer =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .label = "lights.buf",
                .type = SG_BUFFERTYPE_STORAGEBUFFER,
                .size =  sizeof(r->light.arr),
                .usage = SG_USAGE_STREAM,
            });

    bitmap_init(
        &r->light.indices,
        NULL,
        SHADOW_MAP_SIZE_LIGHTS * SHADOW_MAP_SIZE_LIGHTS);

    r->render_data.sides = (renderer_data_array_t) {
        .name = "side",
        .render_type = RENDER_TYPE_SIDE,
        .level_type = LT_SIDE,
        .t_size = sizeof(r->render_data.data.side[0]),
        .data = &r->render_data.data.side[0],
        .buffer =
            sg_make_buffer(
                &(sg_buffer_desc) {
                    .label = "side.buf",
                    .type = SG_BUFFERTYPE_STORAGEBUFFER,
                    .size = sizeof(r->render_data.data.side),
                    .usage = SG_USAGE_STREAM,
                }),
    };

    r->render_data.sectors = (renderer_data_array_t) {
        .name = "sector",
        .render_type = RENDER_TYPE_SECTOR,
        .level_type = LT_SECTOR,
        .t_size = sizeof(r->render_data.data.sector[0]),
        .data = &r->render_data.data.sector[0],
        .buffer =
            sg_make_buffer(
                &(sg_buffer_desc) {
                    .label = "sector.buf",
                    .type = SG_BUFFERTYPE_STORAGEBUFFER,
                    .size = sizeof(r->render_data.data.sector),
                    .usage = SG_USAGE_STREAM,
                }),
    };

    r->render_data.decals = (renderer_data_array_t) {
        .name = "decal",
        .render_type = RENDER_TYPE_DECAL,
        .level_type = LT_DECAL,
        .t_size = sizeof(r->render_data.data.decal[0]),
        .data = &r->render_data.data.decal[0],
        .buffer =
            sg_make_buffer(
                &(sg_buffer_desc) {
                    .label = "decal.buf",
                    .type = SG_BUFFERTYPE_STORAGEBUFFER,
                    .size = sizeof(r->render_data.data.decal),
                    .usage = SG_USAGE_STREAM,
                }),
    };

    r->render_data.models = (renderer_data_array_t) {
        .name = "model",
        .render_type = RENDER_TYPE_MODEL,
        .t_size = sizeof(r->render_data.data.model[0]),
        .data = &r->render_data.data.model[0],
        .buffer =
            sg_make_buffer(
                &(sg_buffer_desc) {
                    .label = "model.buf",
                    .type = SG_BUFFERTYPE_STORAGEBUFFER,
                    .size = sizeof(r->render_data.data.model),
                    .usage = SG_USAGE_STREAM,
                }),
    };

    r->render_data.sprites = (renderer_data_array_t) {
        .name = "sprite",
        .render_type = RENDER_TYPE_SPRITE,
        .t_size = sizeof(r->render_data.data.sprite[0]),
        .data = &r->render_data.data.sprite[0],
        .buffer =
            sg_make_buffer(
                &(sg_buffer_desc) {
                    .label = "sprite.buf",
                    .type = SG_BUFFERTYPE_STORAGEBUFFER,
                    .size = sizeof(r->render_data.data.sprite),
                    .usage = SG_USAGE_STREAM,
                }),
    };

    r->psim.particles_buffer =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_STORAGEBUFFER,
                .usage = SG_USAGE_STREAM,
                .size = MAX_SIM_PARTICLES * sizeof(gpu_psim_particle_t),
            });

    r->psim.cells_buffer =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_STORAGEBUFFER,
                .usage = SG_USAGE_STREAM,
                .size = MAX_SIM_CELLS * sizeof(gpu_psim_cell_t),
            });

    // TODO: maybe not true for all platforms
    STATIC_ASSERT(sizeof(gpu_psim_cell_t) == sizeof(sim_cell_t));
}

void renderer_set_level(level_t *level) {
    reset_for_level();
    r->level = level;
    r->blocks.version = -1;
}

void renderer_destroy() {
    // no need to destroy sokol things since everything on the renderer is
    // program-lifetime :)
    if (allocator_valid(&r->arena)) {
        heap_allocator_destroy(&r->arena);
    }
}

void renderer_free_sector(sector_t *sector) {
    // check render data
    sector_render_info_t *info = &r->render_info.sectors[sector->id];

    if (!genlist_handle_eq(sector->handle, info->handle)) {
        // not relevant
        return;
    }

    if (info->indices) {
        dynbuf_free(&r->db_indices, info->indices);
        info->indices = NULL;
    }

    if (info->vertices) {
        dynbuf_free(&r->db_vertices, info->vertices);
        info->vertices = NULL;
    }
}

// returns true on change
static bool prepare_decal(decal_t *decal) {
    decal_render_info_t *info = &r->render_info.decals[decal->id];
    decal_render_data_t *data = &r->render_data.data.decal[decal->id];

    if (info->version == decal->version
        && info->tex.index == decal->tex.index
        && genlist_handle_eq(info->handle, decal->handle)) {
        return false;
    }

    info->version = decal->version;
    info->handle = decal->handle;
    info->tex = decal->tex;

    mark_data_array_dirty(&r->render_data.decals, decal->id);

    const tex_atlas_entry_t *entry = tex_atlas_entry_by_id(decal->tex);

    *data = (decal_render_data_t) {
        .tex = decal->tex.index,
        .type = decal->type,
        .rotation = decal->rotation,
        .tint = decal->tint,
        .pos = decal->sector.ptr ? decal->sector.pos : decal->side.offsets,
        .size = v2_divs(box2f_size(box2f_from(entry->box_px)), PX_PER_UNIT),
    };

    if (decal->is_on_side) {
        data->sector_index = decal->side.ptr->sector->id;
        data->side_index = decal->side.ptr->id;
        data->plane_type = 0;
    } else {
        data->sector_index = decal->sector.ptr->id;
        data->side_index = -1;
        data->plane_type = decal->sector.plane;
    }

    return true;
}

static void mesh_decal(
        decal_t *decal,
        DYNLIST(u16) *indices,
        DYNLIST(level_vertex_t) *vertices) {
    const u16 base_index = dynlist_size(*vertices);

    const tex_atlas_entry_t *entry =
        tex_atlas_entry_by_id(decal->tex);

    const v2 size =
        v2_divs(box2f_size(box2f_from(entry->box_px)), PX_PER_UNIT);

    const bool clamped = DECAL_TYPES[decal->type].clamped_to_surface;

    level_vertex_t verts[4] = { 0 };
    v3 normal;
    bool wind_cw = false;

    // used for calculating uv coordinates
    v3 v_bl_true, v_tl_true, v_br_true;

    if (decal->is_on_side) {
        normal = v3_of(side_normal(decal->side.ptr), 0.0f);

        vertex_t *vs[2];
        side_get_vertices(decal->side.ptr, vs);

        // quantize to nearest side pixel
        const f32 base_x =
            floor_to_multf(decal->side.offsets.x - (size.x / 2.0f), PX_PER_UNIT);

        f32 t_v0_true = base_x / decal->side.ptr->wall->len;
        f32 t_v1_true = (base_x + size.x) / decal->side.ptr->wall->len;

        f32 t_v0 = t_v0_true;
        f32 t_v1 = t_v1_true;

        if (clamped) {
            t_v0 = satf(t_v0);
            t_v1 = satf(t_v1);
        }

        const v2 v0 = v2_combine(vs[0]->pos, vs[1]->pos, t_v0);
        const v2 v1 = v2_combine(vs[0]->pos, vs[1]->pos, t_v1);

        const v2 v0_true = v2_combine(vs[0]->pos, vs[1]->pos, t_v0_true);
        const v2 v1_true = v2_combine(vs[0]->pos, vs[1]->pos, t_v1_true);

        f32
            z0_true =
                floor_to_multf(
                    decal->side.ptr->sector->floor.z
                        + decal->side.offsets.y
                        - (size.y / 2.0f),
                    PX_PER_UNIT),
            z1_true = z0_true + size.y;

        f32 z0_l = z0_true, z0_r = z0_true, z1_l = z1_true, z1_r = z1_true;

        if (clamped) {
            // find segment of center
            side_segment_t seg;
            side_get_offset_segment(
                r->level, decal->side.ptr, decal->side.offsets, &seg);

            const rangef_t zs_l = {
                .z0 = lerp(seg.zbl, seg.zbr, t_v0),
                .z1 = lerp(seg.ztl, seg.ztr, t_v0),
            };
            const rangef_t zs_r = {
                .z0 = lerp(seg.zbl, seg.zbr, t_v1),
                .z1 = lerp(seg.ztl, seg.ztr, t_v1),
            };

            z0_l = rangef_clamp(zs_l, z0_l);
            z0_r = rangef_clamp(zs_r, z0_r);
            z1_l = rangef_clamp(zs_l, z1_l);
            z1_r = rangef_clamp(zs_r, z1_r);
        }

        verts[0].pos = v3_of(v0, z0_l);
        verts[1].pos = v3_of(v1, z0_r);
        verts[2].pos = v3_of(v0, z1_l);
        verts[3].pos = v3_of(v1, z1_r);

        if (clamped) {
            v_bl_true = v3_of(v0_true, z0_true);
            v_tl_true = v3_of(v0_true, z1_true);
            v_br_true = v3_of(v1_true, z0_true);
        } else {
            // standard UVs
            verts[0].uv = v2_of(0.0f, 0.0f);
            verts[1].uv = v2_of(1.0f, 0.0f);
            verts[2].uv = v2_of(0.0f, 1.0f);
            verts[3].uv = v2_of(1.0f, 1.0f);
        }
    } else {
        normal =
            sector_plane_normal(
                decal->sector.ptr,
                decal->sector.plane);

        wind_cw = decal->sector.plane == PLANE_TYPE_CEIL;

        // quantize/snap to pixel position
        const v2 v0 =
            v2_divs(
                v2_floor(
                    v2_scale(
                        v2_sub(decal->sector.pos, v2_divs(size, 2.0f)),
                        PX_PER_UNIT)),
                PX_PER_UNIT);

        const v2 v1 = v2_add(v0, size);

        verts[0].pos = v3_of(v0.x, v0.y, 0.0f);
        verts[1].pos = v3_of(v1.x, v0.y, 0.0f);
        verts[2].pos = v3_of(v0.x, v1.y, 0.0f);
        verts[3].pos = v3_of(v1.x, v1.y, 0.0f);

        for (int i = 0; i < 4; i++) {
            if (clamped) {
                verts[i].pos =
                    v3_of(
                        sector_clamp_point(
                            decal->sector.ptr,
                            v2_from(verts[i].pos)),
                        0.0f);
            }

            verts[i].pos.z =
                sector_point_zs(
                    decal->sector.ptr,
                    v2_from(verts[i].pos)).zs[decal->sector.plane];
        }

        if (clamped) {
            v_bl_true = v3_of(v0.x, v0.y, verts[0].pos.z);
            v_tl_true = v3_of(v0.x, v1.y, verts[2].pos.z);
            v_br_true = v3_of(v1.x, v0.y, verts[1].pos.z);
        } else {
            // standard UVs
            verts[0].uv = wind_cw ? v2_of(1.0f, 0.0f) : v2_of(0.0f, 0.0f);
            verts[1].uv = wind_cw ? v2_of(0.0f, 0.0f) : v2_of(1.0f, 0.0f);
            verts[2].uv = wind_cw ? v2_of(1.0f, 1.0f) : v2_of(0.0f, 1.0f);
            verts[3].uv = wind_cw ? v2_of(0.0f, 1.0f) : v2_of(1.0f, 1.0f);
        }
    }

    // all vertices are a linear combination of bottom left and top right,
    // use this to solve for UVs
    // (v_bl * t) + (v_tr * u) = verts[i]
    if (clamped) {
        for (int i = 0; i < ARRLEN(verts); i++) {
            const v3 a = v3_sub(v_br_true, v_bl_true);
            const v3 b = v3_sub(v_tl_true, v_bl_true);
            m3 mat;
            mat.col[0] = a;
            mat.col[1] = b;
            mat.col[2] = v3_cross(a, b);

            const m3 mat_inv = m3_inv(mat);
            const v3 res = m3_mulv(mat_inv, v3_sub(verts[i].pos, v_bl_true));
            verts[i].uv = v2_from(res);
        }

        if (decal->sector.ptr && decal->sector.plane == PLANE_TYPE_CEIL) {
            // winding
            swap(verts[0].uv.x, verts[1].uv.x);
            swap(verts[2].uv.x, verts[3].uv.x);
        }
    }

    for (int i = 0; i < ARRLEN(verts); i++) {
        level_vertex_t lv = verts[i];
        lv.n_w = normal;
        lv.buffer_index = i32_bits_to_f32(decal->id);
        lv.type_flags = i32_bits_to_f32(RENDER_TYPE_DECAL);
        *dynlist_push(*vertices) = lv;
    }

    *dynlist_push(*indices) = base_index + (wind_cw ? 2 : 0);
    *dynlist_push(*indices) = base_index + (wind_cw ? 1 : 1);
    *dynlist_push(*indices) = base_index + (wind_cw ? 0 : 2);
    *dynlist_push(*indices) = base_index + (wind_cw ? 2 : 1);
    *dynlist_push(*indices) = base_index + (wind_cw ? 3 : 3);
    *dynlist_push(*indices) = base_index + (wind_cw ? 1 : 2);
}

// returns true on change
static bool prepare_side(side_t *side) {
    side_render_info_t *info = &r->render_info.sides[side->id];

    if (info->version == side->version
        && genlist_handle_eq(info->handle, side->handle)) {
        return false;
    }

    info->version = side->version;
    info->handle = side->handle;
    // don't reset info->middle_indices, that's handled on sector remesh

    side_render_data_t *data = &r->render_data.data.side[side->id];
    mark_data_array_dirty(&r->render_data.sides, side->id);

    vertex_t *vs[2];
    side_get_vertices(side, vs);

    const sidemat_data_t mat = side_get_mat(side);
    const side_segments_t segs = side_get_segments(r->level, side);
    const side_segment_t *mid = segs.middle.present ? &segs.middle : NULL;

    const bool has_neighbor = side->portal && side->portal->sector;
    *data = (side_render_data_t) {
        .tex_low = mat.tex_low.index,
        .tex_mid = mat.tex_mid.index,
        .tex_high = mat.tex_high.index,
        .split_bottom = mat.split_bottom,
        .split_top = mat.split_top,
        .sidemat_flags = mat.flags,
        .flags = side->flags,
        .z_floor = side->sector->floor.z,
        .z_ceil = side->sector->ceil.z,
        .nz_floor = has_neighbor ? side->portal->sector->floor.z : 0.0f,
        .nz_ceil = has_neighbor ? side->portal->sector->ceil.z : 0.0f,
        .offsets = v2_from_i(mat.offsets),
        .sector_index = side->sector->id,
        .tex_overlay = mat.tex_overlay.index,
        .overlay_alpha = mat.overlay_alpha,
        .hsva = mat.hsva,
        .mzbl = mid ? mid->zbl : 0.0f,
        .mzbr = mid ? mid->zbr : 0.0f,
        .mztl = mid ? mid->ztl : 0.0f,
        .mztr = mid ? mid->ztr : 0.0f,
        .a = vs[0]->pos,
        .b = vs[1]->pos,
    };

    return true;
}

static void mesh_side_segment(
        sector_t *sector,
        side_t *side,
        const sidemat_data_t *mat,
        const side_segment_t *seg,
        DYNLIST(u16) *indices,
        DYNLIST(level_vertex_t) *vertices) {
    vertex_t *vs[2];
    side_get_vertices(side, vs);

    i32 type_flags = RENDER_TYPE_SIDE;

    if (side->portal && side->is_disconnect) {
        type_flags |= RENDER_FLAG_PORTAL;
    }

    if (mat->flags & SDMF_SKY) {
        type_flags |= RENDER_FLAG_SKY;
    }

    if (mat->flags & SDMF_TRUE_COLOR) {
        type_flags |= RENDER_FLAG_TRUE_COLOR;
    }

    switch (seg->index) {
    case SIDE_SEGMENT_WALL:   type_flags |= RENDER_FLAG_SEG_MIDDLE; break;
    case SIDE_SEGMENT_MIDDLE: type_flags |= RENDER_FLAG_SEG_MIDDLE; break;
    case SIDE_SEGMENT_TOP:    type_flags |= RENDER_FLAG_SEG_TOP;    break;
    case SIDE_SEGMENT_BOTTOM: type_flags |= RENDER_FLAG_SEG_BOTTOM; break;
    }

    if (seg->index == SIDE_SEGMENT_MIDDLE) {
        r->render_info.sides[side->id].middle_indices =
            dynlist_size(*indices);
    } else if (!side->portal || !side->is_disconnect) {
        r->render_info.sides[side->id].middle_indices = -1;
    }

    const u16 base_index = dynlist_size(*vertices);

    const level_vertex_t verts[4] = {
        // bottom left
        {
            .pos = v3_of(vs[0]->pos.x, vs[0]->pos.y, seg->zbl),
            .uv = v2_of(0.0f, 0.0f),
        },
        // bottom right
        {
            .pos = v3_of(vs[1]->pos.x, vs[1]->pos.y, seg->zbr),
            .uv = v2_of(1.0f, 0.0f),
        },
        // top left
        {
            .pos = v3_of(vs[0]->pos.x, vs[0]->pos.y, seg->ztl),
            .uv = v2_of(0.0f, 1.0f),
        },
        // top right
        {
            .pos = v3_of(vs[1]->pos.x, vs[1]->pos.y, seg->ztr),
            .uv = v2_of(1.0f, 1.0f),
        },
    };

    const v3 normal = v3_of(side_normal(side), 0.0f);

    for (int i = 0; i < ARRLEN(verts); i++) {
        level_vertex_t lv = verts[i];
        lv.n_w = normal;
        lv.buffer_index = i32_bits_to_f32(side->id);
        lv.type_flags = i32_bits_to_f32(type_flags);
        *dynlist_push(*vertices) = lv;
    }

    *dynlist_push(*indices) = base_index + 0;
    *dynlist_push(*indices) = base_index + 1;
    *dynlist_push(*indices) = base_index + 2;
    *dynlist_push(*indices) = base_index + 1;
    *dynlist_push(*indices) = base_index + 3;
    *dynlist_push(*indices) = base_index + 2;
}

// compute a hash of all elements used to make sector geometry
static hash_t compute_sector_mesh_hash(sector_t *sector) {
    hash_t hash = sector->tess_hash;

    // decals
    llist_each(node, &sector->decals, it) {
        hash = hash_add_v3(hash, decal_worldpos(it.el));
        hash = hash_add_int(hash, it.el->tex.index);
    }

    llist_each(sector_sides, &sector->sides, it) {
        llist_each(node, &it.el->decals, it) {
            hash = hash_add_v3(hash, decal_worldpos(it.el));
            hash = hash_add_int(hash, it.el->tex.index);
        }
    }

    return hash;
}

// returns true on data change
static bool prepare_sector(sector_t *sect) {
    sector_render_info_t *info = &r->render_info.sectors[sect->id];

    if (info->prepared_frame == g->time.frame.count) {
        // no changes, already prepared this frame
        return false;
    }

    info->prepared_frame = g->time.frame.count;

    bool any_change = false;

    // prepare sides, their decals
    llist_each(sector_sides, &sect->sides, it_s) {
        any_change |= prepare_side(it_s.el);

        llist_each(node, &it_s.el->decals, it_d) {
            any_change |= prepare_decal(it_d.el);
        }
    }

    // prepare sector decals
    llist_each(node, &sect->decals, it_d) {
        any_change |= prepare_decal(it_d.el);
    }

    if (!any_change
        && info->version == sect->version
        && genlist_handle_eq(info->handle, sect->handle)) {
        return false;
    }

    info->version = sect->version;
    info->handle = sect->handle;

    sector_render_data_t *data = &r->render_data.data.sector[sect->id];
    mark_data_array_dirty(&r->render_data.sectors, sect->id);

    // remesh if tess hash changed
    const hash_t mesh_hash = compute_sector_mesh_hash(sect);
    if (mesh_hash != info->mesh_hash) {
        *dynlist_push(r->mesh_sectors) = sect;
        info->mesh_hash = mesh_hash;
    }

    const sectmat_data_t mat = sector_get_mat(sect);

    *data = (sector_render_data_t) {
        .tex_floor = mat.tex_floor.index,
        .tex_ceil = mat.tex_ceil.index,
        .flags = sect->flags,
        .sectmat_flags = mat.flags,
        .floor_offsets = v2_from_i(mat.offsets_floor),
        .ceil_offsets = v2_from_i(mat.offsets_ceil),
        .floor_hsva = mat.hsva_floor,
        .ceil_hsva = mat.hsva_ceil,
        .tex_overlay_floor = mat.overlay_floor.index,
        .tex_overlay_ceil = mat.overlay_ceil.index,
        .overlay_alpha_floor = mat.overlay_alpha_floor,
        .overlay_alpha_ceil = mat.overlay_alpha_ceil,
    };

    const sector_type_t *ptype = sector_type(sect);
    if (ptype->is_liquid) {
        // optimization opportunity: cache this
        data->tex_liquid = tex_atlas_lookup(ptype->liquid.tex).index;
        data->liquid_hsv =
            ptype->liquid.use_custom_hsv ?
                sect->liquid_hsv
                : ptype->liquid.hsv;
        data->liquid_extra_bloom = ptype->liquid.extra_bloom;
    } else {
        data->tex_liquid = 0;
    }

    return true;
}

static void mesh_sector(sector_t *sect) {
    sector_render_info_t *info = &r->render_info.sectors[sect->id];

    // deallocate existing render data, if present
    if (info->indices) {
        dynbuf_free(&r->db_indices, info->indices);
        info->indices = NULL;
    }

    if (info->vertices) {
        dynbuf_free(&r->db_vertices, info->vertices);
        info->vertices = NULL;
    }

    DYNLIST(u16) indices =
        dynlist_create(u16, &g->frame_arena);

    DYNLIST(level_vertex_t) vertices =
        dynlist_create(level_vertex_t, &g->frame_arena);

    const sectmat_data_t mat = sector_get_mat(sect);

    // floor plane
    {
        const vec3s floor_normal =
            sector_plane_normal(sect, PLANE_TYPE_FLOOR);

        const int n = sector_type(sect)->is_liquid ? 2 : 1;

        for (int i = 0; i < n; i++) {
            const f32 offset = i == 1 ? sect->liquid_offset : 0.0f;

            int type_flags = RENDER_TYPE_SECTOR;

            if (i == 1) {
                type_flags |= RENDER_FLAG_LIQUID;
            }

            dynlist_each(sect->tris, it) {
                for (int j = 2; j >= 0; j--) {
                    const f32 floor =
                        sector_vertex_zs(sect, it.el->vs[j]).z0;

                    *dynlist_push(indices) = dynlist_size(vertices);
                    *dynlist_push(vertices) = (level_vertex_t) {
                        .pos =
                            v3_of(
                                it.el->vs[j]->pos,
                                offset + floor),
                        .n_w = floor_normal,
                        .uv = v2_of(0.0f),
                        .buffer_index = i32_bits_to_f32(sect->id),
                        .type_flags = i32_bits_to_f32(type_flags),
                    };
                }
            }
        }
    }

    // ceiling plane
    {
        const vec3s ceil_normal =
            sector_plane_normal(sect, PLANE_TYPE_CEIL);

        int type_flags = RENDER_TYPE_SECTOR | RENDER_FLAG_IS_CEIL;

        if (mat.flags & SCMF_SKY) {
            type_flags |= RENDER_FLAG_SKY;
        }

        dynlist_each(sect->tris, it) {
            for (int j = 2; j >= 0; j--) {
                const f32 ceil =
                    sector_vertex_zs(sect, it.el->vs[2 - j]).z1;

                *dynlist_push(indices) = dynlist_size(vertices);
                *dynlist_push(vertices) = (level_vertex_t) {
                    .pos =
                        v3_of(
                            it.el->vs[2 - j]->pos,
                            ceil),
                    .n_w = ceil_normal,
                    .uv = v2_of(0.0f),
                    .buffer_index = i32_bits_to_f32(sect->id),
                    .type_flags = i32_bits_to_f32(type_flags),
                };
            }
        }
    }

    // sides
    llist_each(sector_sides, &sect->sides, it_s) {
        side_t *s = it_s.el;

        sidemat_data_t mat;
        bool have_mat = false;

        const side_segments_t segs = side_get_segments(r->level, s);

        for (int i = 0; i < SIDE_SEGMENT_COUNT; i++) {
            if (!segs.arr[i].present || !segs.arr[i].mesh) {
                continue;
            }

            if (!have_mat) {
                have_mat = true;
                mat = side_get_mat(s);
            }

            mesh_side_segment(sect, s, &mat, &segs.arr[i], &indices, &vertices);
        }

        llist_each(node, &s->decals, it_d) {
            mesh_decal(it_d.el, &indices, &vertices);
        }
    }

    // decals
    llist_each(node, &sect->decals, it_d) {
        mesh_decal(it_d.el, &indices, &vertices);
    }

    if (dynlist_size(indices) == 0 && dynlist_size(vertices) == 0) {
        // nothing to render, don't try to allocate
        info->n_indices = 0;
        info->n_vertices = 0;
        ASSERT(!info->indices);
        ASSERT(!info->vertices);
        return;
    }

    info->indices =
        dynbuf_alloc(&r->db_indices, dynlist_size_bytes(indices));
    memcpy(info->indices, indices, dynlist_size_bytes(indices));
    info->n_indices = dynlist_size(indices);

    info->vertices =
        dynbuf_alloc(&r->db_vertices, dynlist_size_bytes(vertices));
    memcpy(info->vertices, vertices, dynlist_size_bytes(vertices));
    info->n_vertices = dynlist_size(vertices);
}

// always requires a data update - use per-model per-frame (!)
// returns assigned model index
static int prepare_model(prepared_model_t *pm) {
    renderer_data_array_t *arr = &r->render_data.models;

    if (arr->next_index >= MAX_PER_FRAME_MODELS) {
        WARN(
            "too many models, cannot assign index to model @ %p (max is %d)",
            pm,
            MAX_PER_FRAME_MODELS);
        return 0;
    }

    const int index = arr->next_index;
    arr->next_index++;

    const model_t *model = &pm->model;
    pm->index_group = NULL;

    if (!str_is_empty(model->index_group)) {
        pm->index_group = NULL;

        if (map_valid(&model->data->index_groups)) {
            model_index_group_t **pgroup =
                map_get(
                    model_index_group_t*,
                    &model->data->index_groups,
                    model->index_group);

            if (pgroup) {
                pm->index_group = *pgroup;
            }
        }
    }

    // draw whole model if index group could not be found
    if (!pm->index_group) {
        pm->index_group = &model->data->default_group;
    }

    r->render_data.data.model[index] = (model_render_data_t) {
        .id =
            (model->render_flags & RENDER_FLAG_SPECIAL_ID) ?
                model->special_id
                : lptr_to_nogen(model->id).raw,
        .tex = model->tex.index,
        .tex_overlay = model->tex_overlay.index,
        .overlay_alpha = model->overlay_alpha,
        .hsv = model->hsv,
        .tint = model->tint,
        .extra_light = model->extra_light,
        .extra_bloom = model->extra_bloom,
        .flags = model->flags,
        .spawn_time = model->spawn_time,
        .corpse_tick = model->corpse_tick,
        .m_centroid = pm->index_group->centroid,
    };
    mark_data_array_dirty(&r->render_data.models, index);

    pm->buffer_index = index;
    return index;
}

static int push_sprite_inst(const sprite_inst_desc_t *desc) {
    if (fixlist_full(sprite_inst_buf)) {
        WARN("out of space in sprite instance buffer");
        return 0;
    }

    renderer_data_array_t *arr = &r->render_data.sprites;

    if (arr->next_index >= MAX_PER_FRAME_SPRITES) {
        WARN(
            "too many sprites, cannot assign index to sprite (max is %d)",
            MAX_PER_FRAME_SPRITES);
        return 0;
    }

    const int index = arr->next_index;
    arr->next_index++;

    r->render_data.data.sprite[index] = (sprite_render_data_t) {
        .id = lptr_to_nogen(desc->id).raw,
        .tex = desc->tex_id.index,
        .extra_light = desc->extra_light,
        .extra_bloom = desc->extra_bloom,
    };
    mark_data_array_dirty(&r->render_data.sprites, index);

    const sprite_inst_t inst = {
        .color = v4_to_f32(desc->color),
        .hsva = v4_to_f32(desc->hsva),
        .pos = desc->pos,
        .size = desc->size,
        .id = i32_bits_to_f32(lptr_to_nogen(desc->id).raw),
        .buffer_index = i32_bits_to_f32(index),
        .type_flags =
            i32_bits_to_f32(desc->render_flags | RENDER_TYPE_SPRITE),
        .flags = i32_bits_to_f32(desc->flags),
        .tex_id = i32_bits_to_f32(desc->tex_id.index),
        .rotation = desc->rotation,
    };

    *fixlist_push(sprite_inst_buf) = inst;
    return 1;
}

static void deconstruct_view_matrix(
        m4 view,
        v3 *pdir,
        v3 *ppos,
        f32 *pyaw,
        f32 *ppitch) {
    v3 dir, pos;
    f32 yaw, pitch;

    const m4 view_inv = m4_inv(view);
    pos = v3_of(view_inv.m30, view_inv.m31, view_inv.m32);
    dir = v3_of(-view_inv.m20, -view_inv.m21, -view_inv.m22);
    yaw = atan2f(dir.y, dir.x);
    pitch = fast_asin(dir.z);

    if (pdir) { *pdir = dir; }
    if (ppos) { *ppos = pos; }
    if (pyaw) { *pyaw = yaw; }
    if (ppitch) { *ppitch = pitch; }
}

// clips "side" against specified view/proj/view_proj matrices + scissor rect,
// returns true if side is potentially visible + the side's clipped scissor rect
static bool side_clip(
        const side_t *side,
        const m4 *proj,
        const m4 *view,
        const m4 *view_proj,
        box2i_t scissor,
        box2i_t *scissor_out) {
    vertex_t *vs[2];
    side_get_vertices(side, vs);

    const frustum_3d_t frustum = cam_view_proj_to_frustum(view_proj);

    const side_segments_t segs = side_get_segments(r->level, side);
    if (!segs.middle.present) {
        // TODO: on-screen warning?
        // WARN(
        //     "trying to clip side %d which doesn't even have a middle seg?",
        //     side->id);
        return false;
    }

    const f32
        z0 = min(segs.middle.zbl, segs.middle.zbr),
        z1 = min(segs.middle.ztl, segs.middle.ztr);

    // optimization opportunity: use indices, don't check same vertices twice
    // check that vertices are on screen and CCW oriented
    const v3 ps[6] = {
        v3_of(vs[0]->pos, z0),
        v3_of(vs[1]->pos, z0),
        v3_of(vs[1]->pos, z1),

        v3_of(vs[0]->pos, z1),
        v3_of(vs[1]->pos, z1),
        v3_of(vs[0]->pos, z0),
    };

    // points clamped to view frustum
    v3 ps_clamp[6];

    int outside[6] = { 0 }, hits[6] = { 0 };

    for (int i = 0; i < ARRLEN(ps); i++) {
        v3 p = ps[i];
        for (int j = 0; j < 6; j++) {
            if (plane_classify(frustum.planes[j], ps[i]) < 0) {
                hits[i] |= 1 << j;
            }

            if (plane_classify(frustum.planes[j], p) < 0) {
                outside[j]++;

                // clamp to plane
                p = plane_project(frustum.planes[j], p);
            }
        }
        ps_clamp[i] = p;
    }

    // throw away if all points are outside one plane
    for (int i = 0; i < 6; i++) {
        if (outside[i] == 6) {
            if (show_debug_ui) {
                igText("rejecting %d, all points entirely outside", side->id);
            }

            return false;
        }
    }

    // convert clamped -> view
    v3 ps_view[6];
    for (int i = 0; i < ARRLEN(ps); i++) {
        ps_view[i] = v3_from(m4_mulv(*view, v4_of(ps_clamp[i], 1.0f)));
    }

    // TODO
    // check for CCW winding on both clamped tris
    int not_ccw = 0;
    for (int i = 0; i < 1; i++) {
        const m3 m = (m3) {
            .col = {
                v3_from(m4_mulv(*view, v4_of(ps[2], 1.0f))),
                v3_from(m4_mulv(*view, v4_of(ps[1], 1.0f))),
                v3_from(m4_mulv(*view, v4_of(ps[0], 1.0f))),
            }
        };

        if (m3_det(m) < 0) {
            not_ccw++;
        }
    }

    if (not_ccw == 1) {
        if (show_debug_ui) {
            igText("rejecting %d, not CCW", side->id);
        }

        return false;
    }

    // screen (pixel) positions
    bool force_full = false;
    v2i ps_px[6];

    for (int i = 0; i < ARRLEN(ps); i++) {
        v4 p_clip =
            m4_mulv(
                *view_proj,
                v4_of(ps[i], 1.0f));

        p_clip.w = ifnaninf(p_clip.w, 1.0f, 1.0f);

        v3 p_ndc = v3_divs(v3_from(p_clip), p_clip.w);
        ps_px[i] =
            v2i_from_v(
                v2_mul(
                    v2_of(
                        0.5f * (p_ndc.x + 1.0f),
                        0.5f * (p_ndc.y + 1.0f)),
                    v2_of(g->target_size.x - 1, g->target_size.y - 1)));
        ps_px[i] =
            v2i_clampv(
                ps_px[i],
                v2i_of(0),
                v2i_of(g->target_size.x - 1, g->target_size.y - 1));

        if (hits[i] & (1 << F3D_PLANE_LEFT)) {
            ps_px[i].x = 0;
        }

        if (hits[i] & (1 << F3D_PLANE_RIGHT)) {
            ps_px[i].x = g->target_size.x - 1;
        }

        if (hits[i] & (1 << F3D_PLANE_BOTTOM)) {
            ps_px[i].y = 0;
        }

        if (hits[i] & (1 << F3D_PLANE_TOP)) {
            ps_px[i].y = g->target_size.y - 1;
        }

        if (hits[i] & (1 << F3D_PLANE_NEAR)) {
            force_full = true;
            break;
        }
    }

    if (force_full) {
        *scissor_out = box2i_mm(v2i_of(0), g->target_size);
    } else {
        // find min/max on screen
        v2i ps_min = ps_px[0], ps_max = ps_px[0];
        for (int i = 0; i < ARRLEN(ps_px); i++) {
            ps_min.x = min(ps_min.x, ps_px[i].x);
            ps_min.y = min(ps_min.y, ps_px[i].y);
            ps_max.x = max(ps_max.x, ps_px[i].x);
            ps_max.y = max(ps_max.y, ps_px[i].y);
        }

        *scissor_out = box2i_mm(ps_min, ps_max);
    }

    if (show_debug_ui) {
        igText(
            "%d scissored to %" PRIbox2i,
            side->id,
            FMTbox2i(*scissor_out));
    }

    // clip against existing scissor rect
    if (!box2i_collides(*scissor_out, scissor)) {
        return false;
    }

    *scissor_out = box2i_intersect(*scissor_out, scissor);
    return true;
}

// from Eric Lengyel's "Oblique View Frustum Depth Projection and Clipping"
// terathon.com/code/oblique.html
//
// see also:
// https://aras-p.info/texts/obliqueortho.html
//
// takes a projection matrix, an "out" view matrix, and a world space position
// and normal - returns the projection matrix with its near pline adjusted such
// that it aligns with the plane defined by position/normal
//
// our version takes in a position and normal and converts that to the camera
// space plane
//
// NOTE: algorithm has been modified for DirectX/Metal/WGPU style clip space
// of [0,1] instead of [-1,1] (OpenGL style)
// (see gamedev.net/tutorials/programming/graphics/perspective-projections-in-lh-and-rh-systems-r3598/)
static m4 compute_oblique_proj_mat(
    const side_t *out,
    m4 proj,
    m4 view) {
    v3 view_pos;
    deconstruct_view_matrix(view, NULL, &view_pos, NULL, NULL);

    const v3 pos = v3_of(wall_midpoint(out->wall), 0.0f);
    const v3 normal = v3_of(side_normal(out), 0.0f);
    const f32 sgn = sign(v3_dot(normal, v3_sub(pos, view_pos)));

    const v3
        pos_v = v3_from(m4_mulv(view, v4_of(pos, 1.0))),
        normal_v =
            v3_scale(
                v3_from(m4_mulv(view, v4_of(normal, 0.0))),
                sgn);

#ifdef SOKOL_METAL
    const f32 offset = 0.0f;
#else
    const f32 offset = 0.05f;
#endif // ifdef SOKOL_METAL

    const f32 dist_v = -v3_dot(pos_v, normal_v) + offset;

    // when close, don't modify - this saves us some z-fighting issues
    if (fabsf(dist_v) <= 2.0f * NEAR_PLANE) {
        return proj;
    }

    const v4 clip_plane = v4_of(normal_v, dist_v);

    const v4 q =
        m4_mulv(
            m4_inv(proj),
            v4_of(
                sign(clip_plane.x),
                sign(clip_plane.y),
                1.0f,
                1.0f));

    v4 c; // scaled plane vector
    f32 a; // scaling factor

#ifdef SOKOL_METAL
    // depth is 0..1
    c = v4_scale(clip_plane, 1.0f / v4_dot(clip_plane, q));
    c.z -= 1.0f;
    // TODO: consider rediriving A for metal
    // but this is probably OK because metal depth buffer is 32-bit?
    a = 1.0f;
#else
    // calculate scaled plane vector c
    c = v4_scale(clip_plane, 2.0f / v4_dot(clip_plane, q));
    a = (2.0f * v4_dot(proj.col[3], q)) / v4_dot(c, q);
#endif // ifdef SOKOL_METAL

    // row 2 = c - row 3
    f32 *raw = (f32*) proj.raw;
    raw[2]  = (a * c.x) - raw[3];
    raw[6]  = (a * c.y) - raw[7];
    raw[10] = (a * c.z) - raw[11];
    raw[14] = (a * c.w) - raw[15];

	return proj;
}

// get view/proj matrices for portal from in -> out based on current proj/view
// matrices
static void compute_portal_view_proj(
        const side_t *in,
        const side_t *out,
        m4 proj,
        m4 view,
        m4 *proj_out,
        m4 *view_out) {
    vertex_t *vs_out[2], *vs_in[2];
    side_get_vertices(in, vs_in);
    side_get_vertices(out, vs_out);

    const v2
        n_in = side_normal(in),
        n_out = side_normal(out),
        mid_in = v2_lerp(vs_in[0]->pos, vs_in[1]->pos, 0.5f),
        mid_out = v2_lerp(vs_out[0]->pos, vs_out[1]->pos, 0.5f);

    const m4
        model_in =
            m4_mul(
                m4_translate_make(v3_of(mid_in, in->sector->floor.z)),
                m4_rotate(
                    m4_identity(),
                    atan2f(n_in.y, n_in.x),
                    v3_of(0, 0, 1))),
        model_out =
            m4_mul(
                m4_translate_make(v3_of(mid_out, out->sector->floor.z)),
                m4_rotate(
                    m4_identity(),
                    atan2f(n_out.y, n_out.x),
                    v3_of(0, 0, 1)));
    *view_out =
        m4_mul(
            m4_mul(
                m4_mul(view, model_in),
                m4_rotate(m4_identity(), PI, v3_of(0, 0, 1))),
            m4_inv(model_out));

    // clip on the OUT side. that is where (in view space) we want to oblique
    // near clipping plane to be placed.
    *proj_out =
        compute_oblique_proj_mat(
            out,
            proj,
            *view_out);
}

typedef struct render_pass {
#ifdef TARGET_DEBUG
    const char *name;
#endif // ifdef TARGET_DEBUG

    m4 view, proj, view_proj;
    frustum_3d_t frustum;
    f32 yaw;
    box2i_t scissor;
    u8 stencil_ref;
    int depth;
    side_t *entry_side, *exit_side;
    sector_t *sector;

    // pass previous to this pass
    const struct render_pass *from;

    // models for this pass
    DYNLIST(prepared_model_t) models;

    // sectors to be drawn with this pass
    DYNLIST(sector_t*) sectors;

    // children of this render pass, they are portaled to via their entry_side
    DYNLIST(struct render_pass*) children;

    // instance buffer offsets/counts
    int offset_sprites, n_sprites;
    int offset_particles, n_particles;
    int offset_fancy_particles, n_fancy_particles;
} render_pass_t;

typedef struct { box2i_t scissor; side_t *side; } portal_side_t;

static int portal_side_cmp(
        const void *a,
        const void *b,
        void *userdata) {
    const portal_side_t *ps_a = a, *ps_b = b;
    const v2 ref_pos = v2_from(*(v3*) userdata);

    const f32
        d_a =
            point_to_segment(
                ref_pos,
                ps_a->side->wall->v0->pos,
                ps_a->side->wall->v1->pos),
        d_b =
            point_to_segment(
                ref_pos,
                ps_b->side->wall->v0->pos,
                ps_b->side->wall->v1->pos);

    // NOTE: INVERTED, such that far sides are first to be rendered.
    return d_b - d_a;
}

static bool is_light_visible_for_pass(
        const render_pass_t *pass,
        const light_desc_t *desc) {
    sector_t *sector = NULL;
    side_t *side = NULL;
    f32 span;

    if ((sector = lptr_sector(r->level, desc->sector))) {
        span = v2_distance(sector->min, sector->max);
    } else if ((side = lptr_side(r->level, desc->side))) {
        span = side->wall->len;
    } else {
        span = 0.0f;
    }

    const f32 z_att =
        desc->params.z_attenuation == 0.0f ?
            desc->params.attenuation
            : desc->params.z_attenuation;

    const f32 radius = (span / 2.0f) + desc->params.attenuation;

    return
        frustum_3d_contains_or_intersects_box3f(
            &pass->frustum,
            (box3f_t) {
                .min = v3_sub(desc->pos, v3_of(radius, radius, z_att)),
                .max = v3_add(desc->pos, v3_of(radius, radius, z_att))
            });
}

// gather lights fromt his sector which could be relevant to the specified pass
static void gather_sector_lights_for_pass(
        const render_pass_t *pass,
        sector_t *sect,
        map_t *ids_to_descs) {
    sector_render_info_t *info = &r->render_info.sectors[sect->id];

    if (info->lights_gather_frame != g->time.frame.count) {
        // gathered this frame
        info->lights_gather_frame = g->time.frame.count;

        // reset lights list
        info->lights = NULL;
        dynlist_init(info->lights, &g->frame_arena);

        // add sector to lights list
        for (int i = 0; i < 2; i++) {
            if (v3_eqv_eps(sect->planes[i].light.color, v3_of(0))) {
                continue;
            }

            light_desc_t desc = {
                .id =
                    LIGHT_ID_FROM(
                        i == 0 ? LIGHT_TYPE_FLOOR : LIGHT_TYPE_CEIL,
                        sect->id),
                .pos =
                    v3_of(
                        box2f_center(sect->bounds),
                        sect->planes[i].z + ((i == 0) ? 1 : -1) * 0.0001f),
                .params = sect->planes[i].light,
                .sector = lptr_from(sect),
            };
            desc.params.flags |= LIGHT_FLAG_SECTOR;
            desc.params.flags |= i == 0 ? 0 : LIGHT_FLAG_CEIL;
            *dynlist_push(info->lights) = desc;
        }

        const int liquid_light_id =
            LIGHT_ID_FROM(LIGHT_TYPE_LIQUID, sect->id);

        const sector_type_t *sect_type = sector_type(sect);
        if (sect_type->is_liquid && sect_type->liquid.has_light) {
            const sector_type_t *ptype = sector_type(sect);

            light_desc_t desc = {
                .id = liquid_light_id,
                .pos =
                    v3_of(
                        v2_lerp(sect->min, sect->max, 0.5f),
                        sect->floor.z
                            + sect->liquid_offset
                            + 0.001f),
                .params = ptype->liquid.light,
                .sector = lptr_from(sect),
            };

            if (sect_type->liquid.use_custom_hsv) {
                desc.params.color =
                    color_offset_with_hsv(v3_of(1, 0, 1), sect->liquid_hsv);
            }

            desc.params.flags |= LIGHT_FLAG_SECTOR;

            *dynlist_push(info->lights) = desc;
        }

        // add sides to lights list
        llist_each(sector_sides, &sect->sides, it) {
            const side_t *side = it.el;

            // and decals
            llist_each(node, &side->decals, it) {
                if (!DECAL_TYPES[it.el->type].has_light) {
                    continue;
                }

                *dynlist_push(info->lights) = decal_light(r->level, it.el);
            }

            light_params_t params;

            const side_t *like = side_get_like(side);
            if (like) {
                params = like->light;
            } else {
                params = side->light;
            }

            if (v3_eqv_eps(params.color, v3_of(0))) {
                continue;
            }

            light_desc_t desc = {
                .id = LIGHT_ID_FROM(LIGHT_TYPE_SIDE, side->id),
                .pos =
                    v3_of(
                        v2_add(
                            wall_midpoint(side->wall),
                            v2_scale(side_normal(side), 0.00001)),
                        (sect->ceil.z + sect->floor.z) / 2.0),
                .params = params,
                .side = lptr_from(side),
            };
            desc.params.flags |= LIGHT_FLAG_SIDE;
            *dynlist_push(info->lights) = desc;
        }

        // add entities to lights list
        dlist_each(sector_node, &sect->entities, it) {
            if (!it.el->ptype->has_light) { continue; }

            const entity_t *ent = it.el;
            if (!ent->ptype->light_fn) {
                WARN("entity has light flag but no light fn?");
                continue;
            }

            light_desc_t desc = ent->ptype->light_fn(r->level, ent);

            if (!desc.id
                || v3_eqv_eps(desc.params.color, v3_of(0))) {
                continue;
            }

            desc.params.flags |= LIGHT_FLAG_ENTITY;
            *dynlist_push(info->lights) = desc;
        }

        // add particles to lights list
        dynlist_each(sect->particles, it) {
            const particle_t *p = it.el;

            if (!(PARTICLE_TYPES[p->type].has_light)) { continue; }

            light_desc_t desc = particle_light(r->level, p);
            if (!desc.id || v3_eqv_eps(desc.params.color, v3_of(0))) {
                continue;
            }

            desc.params.flags |= LIGHT_FLAG_PARTICLE;
            *dynlist_push(info->lights) = desc;
        }

        // and decals
        llist_each(node, &sect->decals, it) {
            if (!DECAL_TYPES[it.el->type].has_light) {
                continue;
            }

            const light_desc_t desc = decal_light(r->level, it.el);

            if (!desc.id || v3_eqv_eps(desc.params.color, v3_of(0))) {
                continue;
            }

            *dynlist_push(info->lights) = desc;
        }
    }

    // get lights
    dynlist_each(info->lights, it_l) {
        if (map_contains(ids_to_descs, it_l.el->id)) {
            continue;
        }

        if (is_light_visible_for_pass(pass, it_l.el)) {
            map_insert(ids_to_descs, it_l.el->id, *it_l.el);
        }
    }
}

// prepares render passes starting from a base pass and creating a tree of
// render passes via its children
// also gathers lights (which will affect all passes)
// returns pointer to base render pass (in draw tree)
static render_pass_t *prepare_render_passes(
        const render_pass_t *first_pass,
        map_t *light_ids_to_descs) {

    DYNLIST(render_pass_t*) queue =
        dynlist_create(render_pass_t*, &g->frame_arena);

    render_pass_t *base_pass =
        mem_alloc_inplace(
            &g->frame_arena,
            sizeof(render_pass_t),
            first_pass);
    *dynlist_push(queue) = base_pass;

    while (dynlist_size(queue) > 0) {
        // pop front of queue
        render_pass_t *pass = dynlist_remove(queue, 0);

        if (pass->depth > MAX_PORTAL_DEPTH) {
             continue;
        }

        // add this pass as a portal from its parent
        if (pass->from) {
            *dynlist_push(pass->from->children) = pass;
        }

        // reference (camera/view) level position for render pass
        v3 ref_pos =
            pass->exit_side ?
                v3_of(wall_midpoint(pass->exit_side->wall), 1.0f)
                : g->cam.pos;

        const char *sides = "";
        {
            const render_pass_t *p = pass;
            while (p) {
                sides =
                    mem_strfcat(
                        tlscratch(),
                        sides,
                        "%d <- %d%s",
                        p->exit_side ? p->exit_side->id : -1,
                        p->entry_side ? p->entry_side->id : -1,
                        p->from ? " .. " : "");
                p = p->from;
            }
        }
        pass->name =
            mem_strfmt(
                tlscratch(),
                "PASS (%s/st: %d/dp: %d)",
                sides,
                pass->stencil_ref,
                pass->depth);

        const bool ui =
            show_debug_ui
            && igTreeNodeEx_Str(pass->name, ImGuiTreeNodeFlags_DefaultOpen);

        if (ui) {
            igTreeNodeSetOpen(igGetItemID(), true);
            igText("SCISSOR: %" PRIbox2i, FMTbox2i(pass->scissor));
            igText("VIEW:\n %" PRIm4, FMTm4(pass->view));
        }

        // accumulate visible
        pass->sectors = dynlist_create(sector_t*, &g->frame_arena);

        if (pass->sector) {
            // gather from sector matrix
            sector_matrix_each(
                    r->level,
                    &r->level->matrices.pvs,
                    pass->sector,
                    it) {
                if (!it.el
                    || !it.el->sides.head
                    || !frustum_3d_contains_or_intersects_box3f(
                            &pass->frustum,
                            it.el->bounds_3d)) {
                    if (show_debug_ui) {
                        igText("frustum culled sector %d", it.el->id);
                    }
                }

                *dynlist_push(pass->sectors) = it.el;
            }
        } else {
            // all sectors in level
            level_each(sector_t, &r->level->sectors, it) {
                *dynlist_push(pass->sectors) = it.el;
            }
        }

        // accumualate lights
        if (pass->sector) {
            sector_matrix_each(
                    r->level,
                    &r->level->matrices.near,
                    pass->sector,
                    it) {
                if (!it.el) { continue; }
                gather_sector_lights_for_pass(pass, it.el, light_ids_to_descs);
            }
        } else {
            DYNLIST(sector_t*) light_sectors =
                dynlist_create(sector_t*, &g->frame_arena);
            level_sectors_in_radius(
                r->level,
                v2_from(ref_pos),
                32.0f,
                &light_sectors);
            dynlist_each(light_sectors, it) {
                gather_sector_lights_for_pass(pass, *it.el, light_ids_to_descs);
            }
        }

        // begin accumulating instanced things
        int offset_pass_sprites = sprite_inst_buf.n;
        int n_pass_sprites = 0;
        int offset_pass_particles = particle_inst_buf.n;
        int n_pass_particles = 0;
        int offset_pass_fancy_particles = fancy_particle_inst_buf.n;
        int n_pass_fancy_particles = 0;

        // list of blocks to traverse
        LLIST(block_t) blocks;
        llist_init(&blocks);

        // accumulate models for this pass
        pass->models = dynlist_create(prepared_model_t, &g->frame_arena);

        // sides which are portals from this pass
        typedef struct { box2i_t scissor; side_t *side; } portal_side_t;
        DYNLIST(portal_side_t) portal_sides =
            dynlist_create(portal_side_t, &g->frame_arena);

        // push frame sprites
        // add frame sprites -> pass sprites
        dynlist_each(r->frame_sprites, it) {
            n_pass_sprites += push_sprite_inst(it.el);
        }

        // * cull sectors outside of view frustum
        // * accumulate list of distance-sorted portals
        // * prepare sectors for drawing
        dynlist_each(pass->sectors, it) {
            if (!*it.el) {
                WARN("empty sector?");
                dynlist_remove_it(pass->sectors, it);
                continue;
            }

            sector_t *sect = *it.el;

            if (show_debug_ui) {
                igText("rendering sector %d", (*it.el)->id);
            }

            prepare_sector(sect);

            // accumulate sector blocks into list
            const v2i b_min = level_pos_to_block_clamped(r->level, sect->min);
            const v2i b_max = level_pos_to_block_clamped(r->level, sect->max);

            for (int by = b_min.y; by <= b_max.y; by++) {
                for (int bx = b_min.x; bx <= b_max.x; bx++) {
                    block_t *block =
                        level_get_block_unsafe(r->level, v2i_of(bx, by));

                    if (!block->node.next && blocks.head != block) {
                        llist_prepend(node, &blocks, block);
                    }
                }
            }

            // push sides with disconnected portals
            dynlist_each(sect->disconnected_portals, it) {
                side_t *side = *it.el;
                if (side == pass->exit_side) {
                    continue;
                }

                if (pass->exit_side) {
                    const v2
                        exit_normal = side_normal(pass->exit_side),
                        normal = side_normal(side);

                    if (v2_dot(exit_normal, normal) > 0.0f) {
                        continue;
                    }
                }

                box2i_t scissor;
                if (!side_clip(
                        side,
                        &pass->proj,
                        &pass->view,
                        &pass->view_proj,
                        pass->scissor,
                        &scissor)) {
                    if (ui) {
                        igText("side %d is not visible from %d, skipping",
                            it.el ? side->id : -1,
                            pass->exit_side ? pass->exit_side->id : -1);
                    }
                    continue;
                }

                *dynlist_push(portal_sides) = (portal_side_t) {
                    .scissor = scissor,
                    .side = side,
                };
            }

            // get particles from this sector
            dynlist_each(sect->particles, it_p) {
                if (PARTICLE_TYPES[it_p.el->type].is_fancy) {
                    *fixlist_push(fancy_particle_inst_buf) =
                        (fancy_particle_inst_t) {
                            .type = i32_bits_to_f32(it_p.el->type),
                            .id = i32_bits_to_f32(it_p.el->id),
                            .pos = it_p.el->pos_xyz,
                            .scale = v3_of(1), // TODO(fancy particles)
                            .vel = it_p.el->vel_xyz,
                        };
                    n_pass_fancy_particles++;
                } else {
                    typeof(r->particle_render_info[0]) *info =
                        &r->particle_render_info[it_p.el->type];

                    particle_inst_desc_t desc = {
                        .pos = it_p.el->pos_xyz,
                        .vel = it_p.el->vel_xyz,
                        .size = info->size,
                        .id = it_p.el->id,
                        .type = it_p.el->type,
                        .tex_id = info->tex_id,
                        .flags = 0, // TODO(particles)
                        .color = v4_of(1),
                        .hsva = v4_of(0),
                    };

                    particle_inst_desc(r->level, it_p.el, &desc);

                    *fixlist_push(particle_inst_buf) =
                        (particle_inst_t) {
                            .pos = desc.pos,
                            .vel = desc.vel,
                            .dir = it_p.el->dir,
                            .size = desc.size,
                            .id = i32_bits_to_f32(desc.id),
                            .type = i32_bits_to_f32(desc.type),
                            .tex_id = i32_bits_to_f32(desc.tex_id.index),
                            .flags = i32_bits_to_f32(desc.flags),
                            .start = i32_bits_to_f32(it_p.el->start),
                            .color = v4_to_f32(desc.color),
                            .hsva = v4_to_f32(desc.hsva),
                        };
                    n_pass_particles++;
                }
            }
        }

        // add prepared pass frame models (note: already prepared!)
        dynlist_each(r->prepared_frame_models, it) {
            *dynlist_push(pass->models) = *it.el;
        }

        // entities to render
        LLIST(entity_t) entities;
        llist_init(&entities);

        // traverse blocks, drop from list, accumulate entities
        while (!llist_empty(&blocks)) {
            block_t *block = llist_pop_front(node, &blocks);

            dynlist_each(block->entities, it) {
                entity_t *ent = *it.el;
                if (ent->sector
                    && !ent->destroy
                    && !ent->ptype->is_invisible
                    && !ent->node.next
                    && ent != entities.head
                    && (!ent->ptype->is_edit_only
                        || g->mode == GAMEMODE_EDITOR)) {
                    llist_prepend(node, &entities, ent);
                }
            }
        }

        DYNLIST(model_t) tmp_models =
            dynlist_create(model_t, &g->frame_arena, 8);

        // gather entities
        while (!llist_empty(&entities)) {
            entity_t *ent = llist_pop_front(node, &entities);

            if (ent->ptype->has_model) {
                if (!ent->ptype->models_fn) {
                    WARN("entity is model but has no models_fn?");
                    continue;
                }

                entity_render_info_t *info = &r->render_info.entities[ent->id];

                // get models if not up to date
                if (info->frame != g->time.frame.count) {
                    info->frame = g->time.frame.count;

                    dynlist_resize_no_contract(tmp_models, 0);
                    ent->ptype->models_fn(r->level, ent, &tmp_models);

                    // push onto info->models
                    info->models = NULL;
                    dynlist_init(info->models, &g->frame_arena);

                    dynlist_each(tmp_models, it) {
                        prepared_model_t *pm = dynlist_push(info->models);
                        pm->model = *it.el;
                        pm->model.id = lptr_from(ent);
                        prepare_model(pm);
                    }
                }

                // get prepared models
                dynlist_push_all(pass->models, info->models);
            } else {
                const tex_atlas_entry_t *entry =
                    tex_atlas_entry_by_name(ent->ptype->sprite);

                sprite_inst_desc_t sprite = {
                    .pos = ent->pos_xyz,
                    .size =
                        box2f_size(
                            box2f_scale_min(
                                box2f_from(entry->box_px),
                                v2_of(1.0f / PX_PER_UNIT))),
                    .color = v4_of(1),
                    .hsva = v4_of(0),
                    .id = lptr_from(ent),
                    .render_flags = 0,
                    .tex_id = entry->id,
                    .rotation = 0.0f,
                };

                if (ent->ptype->sprite_fn) {
                    ent->ptype->sprite_fn(r->level, ent, &sprite);
                }

                n_pass_sprites += push_sprite_inst(&sprite);
            }
        }

        pass->offset_sprites = offset_pass_sprites;
        pass->n_sprites = n_pass_sprites;
        pass->offset_particles = offset_pass_particles;
        pass->n_particles = n_pass_particles;
        pass->offset_fancy_particles = offset_pass_fancy_particles;
        pass->n_fancy_particles = n_pass_fancy_particles;

        // sort portals according to distance from "camera" / ref_pos
        // NOTE: SORTING IS BACKWARDS, far away sides are rendered first so they
        // don't overlap with nearer sides
        dynlist_sort(portal_sides, portal_side_cmp, &ref_pos);

        // optimization opportunity (potentially):
        // check if sorting sectors according to distance (closest to ref_pos
        // first) might help with culling performance

        // accumulate portals
        pass->children = dynlist_create(render_pass_t*, &g->frame_arena);

        dynlist_each(portal_sides, it) {
            side_t *side = it.el->side;

            m4 view_portal, proj_portal;
            compute_portal_view_proj(
                side,
                side->portal,
                pass->proj,
                pass->view,
                &proj_portal,
                &view_portal);

            // recursively prepare inside of portal
            if (ui) {
                igText(
                    "DOING PORTAL %d (from %d)",
                    side->id,
                    pass->exit_side ? pass->exit_side->id : -1);
                igText("  SCISSOR: %" PRIbox2i, FMTbox2i(it.el->scissor));
            }

            const m4 view_proj = m4_mul(proj_portal, view_portal);

            // enqueue next pass
            render_pass_t *next_pass =
                mem_alloc_inplace(
                    &g->frame_arena,
                    sizeof(render_pass_t),
                    (&(render_pass_t) {
                        .proj = proj_portal,
                        .view = view_portal,
                        .view_proj = view_proj,
                        .frustum = cam_view_proj_to_frustum(&view_proj),
                        .yaw =
                            pass->yaw
                                + portal_angle(r->level, side, side->portal),
                        .scissor = it.el->scissor,
                        .stencil_ref = pass->stencil_ref + 1,
                        .depth = pass->depth + 1,
                        .entry_side = side,
                        .exit_side = side->portal,
                        .sector = side->portal->sector,
                        .from = pass,
                    }));

            // enqueue next pass to be prepared
            *dynlist_push(queue) = next_pass;
        }

        if (ui) { igTreePop(); }
    }

    // NOTE for instance buffers:
    // append, but should be only thing done this frame so offset ought to
    // be 0 unless something is badly wrong. if that's the case, change this
    // to a partial update

    // TODO: do this all at once before rendering??

    // upload all sprite, particle, fancy particle data
    if (sprite_inst_buf.n > 0) {
        const int offset =
            sg_append_buffer(
                r->sprite_instbuf,
                &(sg_range) {
                    .ptr = sprite_inst_buf.arr,
                    .size =
                        sprite_inst_buf.n
                            * sizeof(sprite_inst_t),
                });

        ASSERT(offset == 0);
    }

    if (particle_inst_buf.n > 0) {
        const int offset =
            sg_append_buffer(
                r->particle_instbuf,
                &(sg_range) {
                    .ptr = particle_inst_buf.arr,
                    .size =
                        particle_inst_buf.n
                            * sizeof(particle_inst_t),
                });

        ASSERT(offset == 0);
    }

    if (fancy_particle_inst_buf.n > 0) {
        const int offset =
            sg_append_buffer(
                r->fancy_particle_instbuf,
                &(sg_range) {
                    .ptr = fancy_particle_inst_buf.arr,
                    .size =
                        fancy_particle_inst_buf.n
                            * sizeof(fancy_particle_inst_t),
                });

        ASSERT(offset == 0);
    }

    return base_pass;
}

static int prepared_model_index_group_cmp(const void *a, const void *b, void*) {
    const prepared_model_t *pa = a, *pb = b;
    return ((intptr_t) pa->index_group) - ((intptr_t) pb->index_group);
}

static void draw_pass(const render_pass_t *pass) {
    ASSERT(pass->stencil_ref <= MAX_PORTAL_DEPTH);

    // first - draw and stencil portals
    level_vs_params_t vs_params = { 0 };
    vs_params.view = pass->view;
    vs_params.proj = pass->proj;

    level_fs_params_t fs_params = { 0 };
    fs_params.is_portal_pass = true;
    fs_params.visopt = g->visopt;
    fs_params.tick = g->tick;
    fs_params.view_base = g->cam.view;

    // TODO: cleanup
    fs_params.fog_dist =
        r->level->fog.dist == 0.0f ? DEFAULT_FOG_DIST : r->level->fog.dist;

    // look for fade_point
    const entity_t *fade_origin =
        r->level->entities_by_type[ENTITY_TYPE_FADE_ORIGIN].head;
    if (fade_origin) {
        fs_params.fade_enabled = true;
        fs_params.fade_origin = fade_origin->pos;
    } else {
        fs_params.fade_enabled = false;
    }

    sg_bindings level_bindings =
        (sg_bindings) {
            .fs = { .samplers[SLOT_level_smp_nearest] = r->smp_nearest, },
            .index_buffer = r->level_ibuf,
            .vertex_buffers[0] = r->level_vbuf,
        };

    tex_atlas_apply_bindings(
        r->pipelines.stencil[0].level_reg, &level_bindings);
    apply_level_data_bindings(
        r->pipelines.stencil[0].level_reg, &level_bindings);

    dynlist_each(pass->children, it) {
        side_t *side = (*it.el)->entry_side;
        const side_render_info_t *side_info = &r->render_info.sides[side->id];

        if (side_info->version != side->version
            || !genlist_handle_eq(side_info->handle, side->handle)) {
            // if you're hitting this warning, something is not preparing
            // properly
            WARN(
                "skipping side %d due mismatched version or handle",
                 side->id);
            continue;
        } else if (side_info->middle_indices == -1) {
            // if you're hitting this warning, something is probably not
            // bumping the side version even though it should
            WARN("skipping side %d due to missing indices", side->id);
            continue;
        }

        const sector_t *sect = side->sector;
        const sector_render_info_t *sect_info =
            &r->render_info.sectors[sect->id];

        // bindings for rendering side middle indices are sector-relative
        level_bindings.index_buffer_offset =
            sect_info->indices - r->db_indices.ptr;
        level_bindings.vertex_buffer_offsets[0] =
            sect_info->vertices - r->db_vertices.ptr;

        sg_apply_scissor_rect(
            v2_spread(pass->scissor.min),
            v2_spread(box2i_size(pass->scissor)),
            false);

        // draw portal outline with EQUAL for stenciling, but INCR where pass
        sg_apply_pipeline(r->pipelines.stencil[pass->stencil_ref].level_incr);

        sg_apply_uniforms(
            SG_SHADERSTAGE_VS, SLOT_level_vs_params,
            &(sg_range) { &vs_params, sizeof(vs_params) });

        sg_apply_uniforms(
            SG_SHADERSTAGE_FS, SLOT_level_fs_params,
            &(sg_range) { &fs_params, sizeof(fs_params) });
        sg_apply_bindings(&level_bindings);

        sg_draw(side_info->middle_indices, 6, 1);

        // recursively draw inside of portal
        const bool do_tree_pop = show_debug_ui && igTreeNode_Str((*it.el)->name);
        draw_pass(*it.el);
        if (do_tree_pop) { igTreePop(); }

        sg_apply_scissor_rect(
            v2_spread(pass->scissor.min),
            v2_spread(box2i_size(pass->scissor)),
            false);

        // use depth pipeline to paint depth everywhere over portal
        sg_apply_pipeline(r->pipelines.stencil[pass->stencil_ref].level_depth);

        sg_apply_uniforms(
            SG_SHADERSTAGE_VS, SLOT_level_vs_params,
            &(sg_range) { &vs_params, sizeof(vs_params) });

        sg_apply_uniforms(
            SG_SHADERSTAGE_FS, SLOT_level_fs_params,
            &(sg_range) { &fs_params, sizeof(fs_params) });

        sg_apply_bindings(&level_bindings);

        // draw to paint proper depth over this part of the scene
        sg_draw(side_info->middle_indices, 6, 1);

        if (show_debug_ui) {
            igText("drawing into %d depth buffer", side->id);
        }

        // THIRD DRAW: decrement on failure to reset to original stencil
        // value
        sg_apply_pipeline(r->pipelines.stencil[pass->stencil_ref].level_decr);
        sg_apply_uniforms(
            SG_SHADERSTAGE_VS, SLOT_level_vs_params,
            &(sg_range) { &vs_params, sizeof(vs_params) });

        sg_apply_uniforms(
            SG_SHADERSTAGE_FS, SLOT_level_fs_params,
            &(sg_range) { &fs_params, sizeof(fs_params) });

        sg_apply_bindings(&level_bindings);

        sg_draw(side_info->middle_indices, 6, 1);
    }

    // draw regular level geometry
    sg_apply_pipeline(r->pipelines.stencil[pass->stencil_ref].level_reg);

    sg_apply_uniforms(
        SG_SHADERSTAGE_VS, SLOT_level_vs_params,
        &(sg_range) { &vs_params, sizeof(vs_params) });

    fs_params.is_portal_pass = false;
    sg_apply_uniforms(
        SG_SHADERSTAGE_FS, SLOT_level_fs_params,
        &(sg_range) { &fs_params, sizeof(fs_params) });

    level_bindings.index_buffer_offset = 0;
    level_bindings.vertex_buffer_offsets[0] = 0;
    sg_apply_bindings(&level_bindings);

    DYNLIST(sg_ext_indirect_command) commands =
        dynlist_create(
            sg_ext_indirect_command,
            &g->frame_arena,
            dynlist_size(pass->sectors));

    dynlist_each(pass->sectors, it) {
        sector_t *sect = *it.el;
        const sector_render_info_t *sect_info =
            &r->render_info.sectors[sect->id];

        if ((!sect_info->indices && !sect_info->vertices)
            || sect_info->n_indices == 0
            || sect_info->n_vertices == 0) {
            continue;
        }

        *dynlist_push(commands) = (sg_ext_indirect_command) {
            .base_instance = 0,
            .num_instances = 1,
            .base_vertex =
                (sect_info->vertices - r->db_vertices.ptr)
                    / sizeof(level_vertex_t),
            .base_element =
                (sect_info->indices - r->db_indices.ptr)
                    / sizeof(u16),
            .num_elements = sect_info->n_indices,
        };
    }

    if (dynlist_size(commands) != 0) {
        //dynlist_each(commands, it) {
        //    level_bindings.index_buffer_offset = it.el->base_element * sizeof(u16);
        //    level_bindings.vertex_buffer_offsets[0] = it.el->base_vertex * sizeof(level_vertex_t);
        //    sg_apply_bindings(&level_bindings);
        //    sg_draw(0, it.el->num_elements, 1);
        //}
        sg_ext_draw_multi(&sg_range_from_dynlist(commands));
    }

    // draw models
    sg_apply_pipeline(r->pipelines.stencil[pass->stencil_ref].model);

    model_vs_params_t model_vs_params = { 0 };
    model_vs_params.view = pass->view;
    model_vs_params.proj = pass->proj;

    model_fs_params_normal_t model_fs_params = { 0 };
    model_fs_params.tick = g->tick;
    model_fs_params.time_s = g->time.total_scaled_s;

    sg_bindings model_bindings_base = {
        .fs.samplers[SLOT_model_smp_nearest] = r->smp_nearest,
        .index_buffer = g_model_atlas->index_buffer.handle,
        .vertex_buffers[0] = g_model_atlas->vertex_buffer.handle,
        .vertex_buffers[1] = r->model_instbuf,
    };

    // NOTE: also ok for model_no_depth, bindings should be the exact same
    tex_atlas_apply_bindings(
        r->pipelines.stencil[0].model, &model_bindings_base);
    apply_level_data_bindings(
        r->pipelines.stencil[0].model, &model_bindings_base);

    sg_apply_uniforms(
        SG_SHADERSTAGE_VS,
        SLOT_model_vs_params,
        &SG_RANGE(model_vs_params));

    sg_apply_uniforms(
        SG_SHADERSTAGE_FS,
        SLOT_model_fs_params_normal,
        &SG_RANGE(model_fs_params));

    // bucket pass models according to index group by sorting
    dynlist_sort(pass->models, prepared_model_index_group_cmp, NULL);

    // accumulate all instances in order for this pass, grouped by index group
    // to be rendered together
    typedef struct {
        int offset;
        int count;
    } model_instance_group_t;

    // reset instance buffer
    model_inst_buf.n = 0;

    DYNLIST(model_instance_group_t) instance_groups =
        dynlist_create(model_instance_group_t, &g->frame_arena);

    for (int i = 0, n = dynlist_size(pass->models); i < n;) {
        const prepared_model_t *pm = &pass->models[i];

        // count instances
        int n_instances = 0;

        while (i + n_instances < n
               && pass->models[i + n_instances].index_group == pm->index_group) {
            n_instances++;
        }

        if (!pm->model.data) {
            WARN(
                "frame model for %s has no data?",
                lptr_to_str(r->level, pm->model.id, tlscratch()));
            goto done;
        } else if (!pm->model.data->uploaded) {
            WARN(
                "frame model for %s not uploaded yet, skipping render",
                lptr_to_str(r->level, pm->model.id, tlscratch()));
            goto done;
        }

        // push group onto list
        *dynlist_push(instance_groups) =
            (model_instance_group_t) { .offset = i, .count = n_instances };

        // upload n_instances worth of instance data
        // offsets will match group
        for (int j = i; j < i + n_instances; j++) {
            const prepared_model_t *pm_j = &pass->models[j];
            const model_t *m_j = &pass->models[j].model;

            *fixlist_push(model_inst_buf) = (model_inst_t) {
                .id =
                    i32_bits_to_f32(
                        (m_j->render_flags & RENDER_FLAG_SPECIAL_ID) ?
                            m_j->special_id
                            : lptr_to_nogen(m_j->id).raw),
                .buffer_index = i32_bits_to_f32(pm_j->buffer_index),
                .model_flags = i32_bits_to_f32(m_j->flags),
                .model_type_flags =
                    i32_bits_to_f32(m_j->render_flags | RENDER_TYPE_MODEL),
                .t0 = m_j->transform.col[0],
                .t1 = m_j->transform.col[1],
                .t2 = m_j->transform.col[2],
                .t3 = m_j->transform.col[3],
            };
        }

done:
        i += n_instances;
    }

    const int n_instances_total = dynlist_size(pass->models);
    const int base_instbuf_offset =
        sg_append_buffer(
            r->model_instbuf,
            &(sg_range) {
                .ptr = model_inst_buf.arr,
                .size = n_instances_total * sizeof(model_inst_buf.arr[0]),
            });

    dynlist_each(instance_groups, it) {
        const prepared_model_t *pm = &pass->models[it.el->offset];

        // point bindings to index group
        sg_bindings model_bindings = model_bindings_base;
        model_bindings.index_buffer_offset =
            pm->model.data->offset_indices;
        model_bindings.vertex_buffer_offsets[0] =
            pm->model.data->offset_vertices;
        model_bindings.vertex_buffer_offsets[1] =
            base_instbuf_offset
                + (it.el->offset * sizeof(model_inst_buf.arr[0]));
        sg_apply_bindings(&model_bindings);

        sg_draw(
            pm->index_group->offset_indices,
            pm->index_group->n_indices,
            it.el->count);
    }

    // draw accumulated sprite instances
    if (pass->n_sprites > 0) {
        sprite_vs_params_t vs_params;
        vs_params.yaw = pass->yaw;
        vs_params.pitch = g->cam.pitch;
        vs_params.cam_right = g->cam.right;
        vs_params.cam_up = g->cam.up;
        vs_params.view = pass->view;
        vs_params.proj = pass->proj;

        sprite_fs_params_t fs_params;
        fs_params.tick = g->tick;

        sg_apply_pipeline(r->pipelines.stencil[pass->stencil_ref].sprite);

        sg_apply_uniforms(
            SG_SHADERSTAGE_VS, SLOT_sprite_vs_params,
            &(sg_range) { &vs_params, sizeof(vs_params) });

        sg_apply_uniforms(
            SG_SHADERSTAGE_FS, SLOT_sprite_fs_params,
            &(sg_range) { &fs_params, sizeof(fs_params) });

        sg_bindings sprite_bindings = {
            .fs = {
                .samplers[SLOT_sprite_smp_nearest] = r->smp_nearest,
            },
            .index_buffer = r->sprite_ibuf,
            .vertex_buffers[0] = r->sprite_vbuf,
            .vertex_buffers[1] = r->sprite_instbuf,
            .vertex_buffer_offsets[1] =
                pass->offset_sprites * sizeof(sprite_inst_t),
        };

        tex_atlas_apply_bindings(
            r->pipelines.stencil[0].sprite, &sprite_bindings);

        sg_apply_bindings(&sprite_bindings);
        sg_draw(0, 6, pass->n_sprites);
    }

    // draw accumulated particle instances
    if (pass->n_particles > 0) {
        particle_vs_params_t vs_params;
        vs_params.yaw = pass->yaw;
        vs_params.pitch = g->cam.pitch;
        vs_params.cam_right = g->cam.right; // TODO
        vs_params.cam_dir  = g->cam.dir; // TODO(particles): breaks in portals?
        vs_params.cam_up = g->cam.up; // TODO
        vs_params.cam_pos = g->cam.pos; // TODO
        vs_params.view = pass->view;
        vs_params.proj = pass->proj;
        vs_params.view_no_pitch = g->cam.view_no_pitch; // TODO

        if (g->debug.pause_particles) {
            vs_params.tick = g->debug.pause_particle_tick;
            vs_params.time_s = g->debug.pause_particle_s;
        } else {
            vs_params.tick = g->tick;
            vs_params.time_s = g->time.total_scaled_s;
        }

        particle_fs_params_t fs_params;
        fs_params.view = pass->view;
        fs_params.tick = vs_params.tick;
        fs_params.time_s = fs_params.time_s;
        fs_params.cam_right = g->cam.right; // TODO
        fs_params.cam_dir  = g->cam.dir; // TODO(particles): breaks in portals?
        fs_params.cam_up = g->cam.up; // TODO

        sg_apply_pipeline(r->pipelines.stencil[pass->stencil_ref].particle);

        sg_apply_uniforms(
            SG_SHADERSTAGE_VS, SLOT_particle_vs_params,
            &(sg_range) { &vs_params, sizeof(vs_params) });

        sg_apply_uniforms(
            SG_SHADERSTAGE_FS, SLOT_particle_fs_params,
            &(sg_range) { &fs_params, sizeof(fs_params) });

        sg_bindings particle_bindings = {
            .fs = {
                .samplers[SLOT_particle_smp_nearest] = r->smp_nearest,
            },
            .index_buffer = r->sprite_ibuf,
            .vertex_buffers[0] = r->sprite_vbuf,
            .vertex_buffers[1] = r->particle_instbuf,
            .vertex_buffer_offsets[1] =
                pass->offset_particles * sizeof(particle_inst_t),
        };

        tex_atlas_apply_bindings(
            r->pipelines.stencil[0].particle, &particle_bindings);

        sg_apply_bindings(&particle_bindings);
        sg_draw(0, 6, pass->n_particles);
    }

    // draw accumulated fancy particles
    if (pass->n_fancy_particles > 0) {
        fancy_particle_vs_params_t vs_params;
        vs_params.view = pass->view;
        vs_params.proj = pass->proj;
        vs_params.tick = g->tick;
        vs_params.time_s = g->time.total_scaled_s;

        fancy_particle_fs_params_t fs_params;
        fs_params.tick = g->tick;
        fs_params.time_s = g->time.total_scaled_s;

        sg_apply_pipeline(
            r->pipelines.stencil[pass->stencil_ref].fancy_particle);

        sg_apply_uniforms(
            SG_SHADERSTAGE_VS, SLOT_fancy_particle_vs_params,
            &(sg_range) { &vs_params, sizeof(vs_params) });

        sg_apply_uniforms(
            SG_SHADERSTAGE_FS, SLOT_fancy_particle_fs_params,
            &(sg_range) { &fs_params, sizeof(fs_params) });

        sg_bindings fancy_particle_bindings = {
            .fs = { },
            .index_buffer = r->fancy_particle_ibuf,
            .vertex_buffers[0] = r->fancy_particle_vbuf,
            .vertex_buffers[1] = r->fancy_particle_instbuf,
            .vertex_buffer_offsets[1] =
                pass->offset_fancy_particles * sizeof(fancy_particle_inst_t),
        };

        sg_apply_bindings(&fancy_particle_bindings);
        sg_draw(0, 36, pass->n_fancy_particles);
    }
}

static void render_phantom_models() {
    sg_apply_pipeline(r->pipelines.phantom);

    model_vs_params_t model_vs_params = { 0 };

    model_vs_params.view = g->cam.phantom_view;
    model_vs_params.proj = g->cam.phantom_proj;

    sg_bindings model_bindings_base = {
        .index_buffer = g_model_atlas->index_buffer.handle,
        .vertex_buffers[0] = g_model_atlas->vertex_buffer.handle,
        .vertex_buffers[1] = r->model_instbuf,
    };

    sg_apply_uniforms(
        SG_SHADERSTAGE_VS,
        SLOT_model_vs_params,
        &SG_RANGE(model_vs_params));

    // reset instance buffer
    model_inst_buf.n = 0;

    const int n_models = dynlist_size(r->frame_phantom_models);

    // upload individual instances for each model
    for (int i = 0; i < n_models; i++) {
        const model_t *m = &r->frame_phantom_models[i];
        *fixlist_push(model_inst_buf) = (model_inst_t) {
            .id =
                i32_bits_to_f32(
                    (m->render_flags & RENDER_FLAG_SPECIAL_ID) ?
                        m->special_id
                        : lptr_to_nogen(m->id).raw),
            .buffer_index = i32_bits_to_f32(0),
            .model_flags = i32_bits_to_f32(m->flags),
            .model_type_flags =
                i32_bits_to_f32(m->render_flags | RENDER_TYPE_MODEL),
            .t0 = m->transform.col[0],
            .t1 = m->transform.col[1],
            .t2 = m->transform.col[2],
            .t3 = m->transform.col[3],
        };
    }

    const int base_instbuf_offset =
        sg_append_buffer(
            r->model_instbuf,
            &(sg_range) {
                .ptr = model_inst_buf.arr,
                .size = n_models * sizeof(model_inst_buf.arr[0]),
            });

    // render each instance individually
    for (int i = 0; i < n_models; i++) {
        const model_t *m = &r->frame_phantom_models[i];
        const model_index_group_t *index_group =
            m->index_group ?
                model_data_try_get_index_group(m->data, m->index_group)
                : &m->data->default_group;

        // point bindings to index group
        sg_bindings model_bindings = model_bindings_base;
        model_bindings.index_buffer_offset = m->data->offset_indices;
        model_bindings.vertex_buffer_offsets[0] = m->data->offset_vertices;

        // one instance for each model
        model_bindings.vertex_buffer_offsets[1] =
            base_instbuf_offset
                + (i * sizeof(model_inst_buf.arr[0]));

        sg_apply_bindings(&model_bindings);

        sg_draw(
            index_group->offset_indices,
            index_group->n_indices,
            1);
    }
}

void renderer_update_cam() {
    g->cam.pitch = clamp(g->cam.pitch, -PI_2 + 0.05f, PI_2 - 0.05f);
    g->cam.yaw = angle_wrap_tau(g->cam.yaw);

    const v3 shake = screenshake_sample_all(g->cam.pos);

    static f32
        delta_yaw,
        last_yaw,
        tilt,
        tilt_target,
        delta_yaw_tilt,
        fov_out,
        fov_out_target;

    RELOAD_STATIC_RANGE(RANGE(delta_yaw));
    RELOAD_STATIC_RANGE(RANGE(last_yaw));
    RELOAD_STATIC_RANGE(RANGE(tilt));
    RELOAD_STATIC_RANGE(RANGE(tilt_target));
    RELOAD_STATIC_RANGE(RANGE(delta_yaw_tilt));
    RELOAD_STATIC_RANGE(RANGE(fov_out));
    RELOAD_STATIC_RANGE(RANGE(fov_out_target));

    delta_yaw =
        g->player && ticks_since_tick(g->player->last_portal_tick) <= 5 ?
            0 : clamp(angle_min_diff(g->cam.yaw, last_yaw), -1.0f, 1.0f);

    last_yaw = g->cam.yaw;

    delta_yaw_tilt = dtlerp(delta_yaw_tilt, delta_yaw, 20.0f, g->time.frame.dt);
    if (isnan(delta_yaw_tilt)) { delta_yaw_tilt = 0.0f; }

    const f32
        pitch = g->cam.pitch + shake.x,
        yaw = g->cam.yaw + shake.y;

    // 0, 0, 0 should look down the positive x-axis (1, 0, 0)
    g->cam.dir =
        v3_normalize(
            v3_of(
                cosf(yaw) * cosf(pitch),
                sinf(yaw) * cosf(pitch),
                sinf(pitch)));

    g->cam.dir_xy = v3_normalize(v3_of(g->cam.dir.x, g->cam.dir.y, 0.0f));

    if (g->player) {
        // compute 2D velocity relative to current look direction
        // REMEMBER: yaw == 0 looks down +X
        const v2 vel_rel = v2_rotate(g->player->vel, -g->cam.yaw);

        const f32 tilt_max = 0.18f + (g->cam.slide * 0.03f);
        tilt_target = tilt_max * clamp(-vel_rel.y / 12.0f, -1.0f, 1.0f);

        if (g->player->sliding) {
            f32 factor = satf(g->player->slide_angle / PI_12);

            // ease_cubic_out to make angle increase faster at start
            factor = ease_cubic_out(factor);

            const f32 slide_duration_s =
                (g->player->last_slide_tick - g->player->last_slide_begin_tick)
                    * (1.0f / TICKS_PER_SECOND);

            // 0.25s to get full effet
            factor *= satf(slide_duration_s / 0.25f);

            tilt_target += -1.0f * 0.16f * factor;
        }

        if (g->player && ticks_since_tick(g->player->last_portal_tick) <= 3) {
            // tilt = tilt;
        } else {
            tilt = dtlerp(tilt, tilt_target, 50.0f, g->time.frame.dt_scaled);
        }
    } else {
        tilt = 0.0f;
    }

    // 2D right vector
    const v2 right_xy = v2_rotate(v2_from(g->cam.dir_xy), -PI_2);

    // TODO: fix
    // compute tilted up where "tilt" moves up point to right
    const v3 up_tilted =
        v3_normalize(
            v3_of(
                v2_scale(
                    right_xy,
                    tilt * invsatf(fabsf(g->cam.pitch) / PI_2)),
                1));

    g->cam.right = v3_normalize(v3_cross(g->cam.dir, up_tilted));
    g->cam.up = v3_normalize(v3_cross(g->cam.right, g->cam.dir));

    if (g->player) {
        fov_out_target =
            ease_quart_in(
                satf(
                    v2_norm(
                        v2_posproj(
                            g->player->vel,
                            v2_from(g->cam.dir)))
                        / (0.5f * PLAYER_MAX_SPEED)));

        if (g->player && ticks_since_tick(g->player->last_portal_tick) <= 1) {
            fov_out_target = fov_out;
        }

        fov_out_target = satf(fov_out_target);

        if (g->player->sliding) {
            fov_out_target *= 1.5f;
        }

        if (g->slow_mo_time > 0.0f) {
            fov_out_target += 0.25f;
        }

        fov_out =
            dtlerp(
                fov_out,
                fov_out_target,
                10.0f,
                g->time.frame.dt);
    } else {
        fov_out = 0.0f;
        fov_out_target = 0.0f;
    }

    f32 fov = 85.0f;
    fov += lerp(0.0f, 20.0f, fov_out);

    if (g->mode == GAMEMODE_MAIN_MENU
        && g_main_menu->last_play_transition_s != 0.0f) {
        const stime_t since = secs_since_s(g_main_menu->last_play_transition_s);

        static f32 transition_fov;
        if (since < (PLAY_TRANSITION_TIME_S / 2.0f)) {
            transition_fov =
                dtlerp(transition_fov, 15.0f, 8.0f, g->time.frame.dt_scaled);
        } else {
            transition_fov =
                dtlerp(transition_fov, 0.0f, 8.0f, g->time.frame.dt_scaled);
        }

        fov += transition_fov;
    }

    g->cam.fov = fov;

    g->cam.view =
        cam_lookat(
            g->cam.pos,
            v3_add(g->cam.dir, g->cam.pos),
            g->cam.up),
    g->cam.proj =
        cam_perspective(
            rads_from_degs(fov),
            g->target_size.x / (f32) g->target_size.y,
            NEAR_PLANE,
            FAR_PLANE);
    g->cam.view_proj = m4_mul(g->cam.proj, g->cam.view);
    g->cam.frustum = cam_view_proj_to_frustum(&g->cam.view_proj);

    g->cam.view_no_pitch =
        cam_lookat(
            g->cam.pos,
            v3_add(v3_normalize(g->cam.dir_xy), g->cam.pos),
            v3_of(0, 0, 1));

    g->cam.view_proj_no_pitch =
        m4_mul(g->cam.proj, g->cam.view_no_pitch);

    g->cam.inv_view = m4_inv(g->cam.view);
    g->cam.inv_proj = m4_inv(g->cam.proj);
    g->cam.inv_view_proj = m4_inv(g->cam.view_proj);
}

void renderer_render() {
    const f32 dt = g->time.frame.dt_scaled;

    r->mesh_sectors = dynlist_create(sector_t*, &g->frame_arena);

    // reset per-frame buffers
    r->render_data.models.next_index = 0;
    r->render_data.sprites.next_index = 0;

    // reset instance buffers
    sprite_inst_buf.n = 0;
    particle_inst_buf.n = 0;
    fancy_particle_inst_buf.n = 0;

    // reset to unassigned/-1, sprite indices are reallocated per-frame
    for (int i = 0; i < ARRLEN(r->particle_render_info); i++) {
        const tex_id_t tex_id = tex_atlas_lookup(PARTICLE_TYPES[i].tex);

        r->particle_render_info[i] = (typeof(r->particle_render_info[i])) {
            .tex_id = tex_id,
            .size =
                v2_scale(
                    v2_from_i(tex_atlas_entry_by_id(tex_id)->size_px),
                    1.0f / PX_PER_UNIT),
        };
    }

    // fade origin, NULL if not present
    const entity_t *fade_origin =
        r->level->entities_by_type[ENTITY_TYPE_FADE_ORIGIN].head;

    // update tints
    dynlist_each(r->tints, it) {
        if ((ns_to_secs(g->time.total_ns - it.el->start_abs_ns)
                >= it.el->duration && !it.el->single_frame)
            || (it.el->single_frame
                && (g->time.frame.count != it.el->start_frame))) {
            dynlist_remove_it(r->tints, it);
        }
    }

    // generate block image
    if (r->blocks.dirty) {
        r->blocks.dirty = false;

        if (r->blocks.n_walls != 0) {
            sg_ext_update_buffer_partial(
                r->blocks.data_buffer,
                0,
                &(sg_range) {
                    .ptr = r->blocks.data,
                    .size = r->blocks.n_walls * sizeof(r->blocks.data[0]),
                });
        }

        if (r->blocks.n_blocks != 0) {
            sg_ext_update_buffer_partial(
                r->blocks.indices_buffer,
                0,
                &(sg_range) {
                    .ptr = r->blocks.indices,
                    .size = r->blocks.n_blocks * sizeof(r->blocks.indices[0])
                });
        }
    }

    show_debug_ui =
        g->debug.show_renderer_debug
        && igBegin("renderer debug", &g->debug.show_renderer_debug, 0);

    // prepare frame models - do it once here so we don't do it for every pass
    dynlist_each(r->frame_models, it) {
        prepared_model_t *pm = dynlist_push(r->prepared_frame_models);
        pm->model = *it.el;
        prepare_model(pm);
    }

    // (int) light id -> light_desc_t
    map_t light_ids_to_descs;
    map_init(
        &light_ids_to_descs,
        &g->frame_arena,
        sizeof(int),
        sizeof(light_desc_t),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    // recursively prepare from the base render pass
    render_pass_t *base_pass =
        prepare_render_passes(
            &(render_pass_t) {
                .depth = 0,
                .view = g->cam.view,
                .proj = g->cam.proj,
                .view_proj = g->cam.view_proj,
                .frustum = g->cam.frustum,
                .yaw = g->cam.yaw,
                .scissor = box2i_ps(v2i_of(0), g->target_size),
                .sector =
                    level_find_point_sector(
                        r->level,
                        v2_from(g->cam.pos),
                        NULL),
                .from = NULL,
            },
            &light_ids_to_descs);

    // gather lights into frame_lights
    dynlist_reserve(
        r->frame_lights,
        dynlist_size(r->frame_lights) + map_size(&light_ids_to_descs));

    map_each(int, light_desc_t, &light_ids_to_descs, it) {
        *dynlist_push(r->frame_lights) = *it.value;
    }

    // update shadow maps
    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.shadow.attach,
            .action.colors[0] = { .load_action = SG_LOADACTION_LOAD },
            .label = "shadow.pass",
        });
    sg_push_debug_group("LIGHTS");
    {
         do_lights_pass();
    }
    sg_pop_debug_group();
    sg_end_pass();

    if (r->light.n_lights != 0) {
        // optimization opportunity: diff lights buffer, range update
        sg_ext_update_buffer_partial(
            r->light.buffer,
            0,
            &(sg_range) {
                .ptr = r->light.arr,
                .size = r->light.n_lights * sizeof(r->light.arr[0]),
            });
    }

    // update dirty ranges for each data array
    for (int i = 0; i < ARRLEN(r->render_data.arrays); i++) {
        renderer_data_array_t *arr = &r->render_data.arrays[i];

        if (arr->dirty.min_index == -1 && arr->dirty.max_index == -1) {
            continue;
        }

        ASSERT(arr->dirty.min_index != -1 && arr->dirty.max_index != -1);

        sg_ext_update_buffer_partial(
            arr->buffer,
            arr->t_size * arr->dirty.min_index,
            &(sg_range) {
                .ptr = ((u8*) arr->data) + (arr->t_size * arr->dirty.min_index),
                .size =
                    ((arr->dirty.max_index - arr->dirty.min_index) + 1)
                        * arr->t_size,
            });

        arr->dirty.min_index = -1;
        arr->dirty.max_index = -1;
    }

    // mesh sectors which need updates
    dynlist_each(r->mesh_sectors, it) {
        mesh_sector(*it.el);
    }

    // update level geometry
    if (dynlist_size(r->mesh_sectors) != 0) {
        // TODO: partial updates
        sg_update_buffer(
            r->level_ibuf,
            &(sg_range) {
                .size = r->db_indices.used,
                .ptr = r->db_indices.ptr
            });

        sg_update_buffer(
            r->level_vbuf,
            &(sg_range) {
                .size = r->db_vertices.used,
                .ptr = r->db_vertices.ptr
            });
    }

    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.deferred.attach,
            .action = {
                .colors[0] = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = { 0.0, 0.0, 0.0, 1.0 },
                },
                .colors[1] = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = { 0.0, 0.0, 0.0, 1.0 },
                },
                .colors[2] = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = { 0.0, 0.0, 0.0, 1.0 },
                },
                .colors[3] = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = { 0.0, 0.0, 0.0, 1.0 },
                },
                .depth = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = 1000.0f
                },
                .stencil = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = 0
                },
            },
            .label = "deferred.pass",
        });
    sg_push_debug_group("3D");

    if (g->debug.wireframe) {
        sg_ext_set_wireframe(true);
    }

    // passes are drawn recursively, start from first pass
    draw_pass(base_pass);

    sg_ext_set_wireframe(false);

    debug_draw_render(&g->cam.proj, &g->cam.view);

    if (show_debug_ui) {
        igEnd();
    }

    // end deferred pass
    sg_pop_debug_group();
    sg_end_pass();

    static i64 last_particle_update_ns;
    if (ns_to_secs(g->time.total_ns - last_particle_update_ns) >= (1.0f / 20.0f)) {
        last_particle_update_ns = g->time.total_ns;

        DYNLIST(gpu_psim_particle_t) particles =
            dynlist_create(gpu_psim_particle_t, &g->frame_arena);

        dynlist_each(g->psim->particles, it) {
            *dynlist_push(particles) =
                (gpu_psim_particle_t) {
                    .pos = it.el->pos,
                    .density = it.el->density,
                    .is_right = it.el->is_right ? 1 : 0,
                };
        }

        r->psim.n_particles = dynlist_size(particles);
        if (r->psim.n_particles != 0) {
            sg_update_buffer(
                r->psim.particles_buffer,
                &sg_range_from_dynlist(particles));
        }

        r->psim.cell_dims = g->psim->cell_dims;
        if (dynlist_size(g->psim->cells) != 0) {
            sg_update_buffer(
                r->psim.cells_buffer,
                &sg_range_from_dynlist(g->psim->cells));
        }
    }

    // compute contrast effect
    {
        if (g->mode == GAMEMODE_MAIN_MENU
            && g_main_menu->last_play_transition_s != 0.0f) {
            const stime_t since = secs_since_s(g_main_menu->last_play_transition_s);

            static f32 t;
            if (since < (PLAY_TRANSITION_TIME_S / 4.0f)) {
                t =
                    dtlerp(t, 0.2f, 20.0f, g->time.frame.dt_scaled);
            } else {
                t =
                    dtlerp(t, 0.0f, 5.0f, g->time.frame.dt_scaled);
            }

            g->contrast_effect = t;
        } else if (g->player) {
            g->contrast_effect =
                0.05f * invsatf(ticks_since_tick(g->player->last_shot) / 6.0f);
        } else {
            g->contrast_effect = 0.0f;
        }
    }

    // composite
    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.composite.attach,
            .action = {
                .colors[0] = { .load_action = SG_LOADACTION_CLEAR, },
                .colors[1] = { .load_action = SG_LOADACTION_CLEAR, },
                .colors[2] = { .load_action = SG_LOADACTION_CLEAR, },
                .colors[3] = { .load_action = SG_LOADACTION_CLEAR, },
                .depth = {
                    .load_action = SG_LOADACTION_CLEAR,
                    .clear_value = 100.0f
                }
            },
            .label = "composite.pass",
        });
    sg_push_debug_group("composite");
    {
        composite_vs_params_t vs_params = { 0 };
        screenquad_mats(
            &vs_params.model,
            &vs_params.view,
            &vs_params.proj);

        composite_fs_params_t fs_params = { 0 };
        fs_params.proj = g->cam.proj;
        fs_params.view = g->cam.view;
        fs_params.inv_view_proj = g->cam.inv_view_proj;
        fs_params.viewport = v2_from_i(g->target_size);
        fs_params.depth_near = NEAR_PLANE;
        fs_params.depth_far = FAR_PLANE;

        // sky text vars
        {
            static f32 alpha, mix;
            mix =
                dtlerp(
                    mix,
                    g->sky_text.mix,
                    8.0f,
                    g->time.frame.dt_scaled);
            fs_params.sky_tex_mix = mix;

            alpha =
                dtlerp(
                    alpha,
                    g->sky_text.alpha,
                    8.0f,
                    g->time.frame.dt_scaled);
            fs_params.sky_tex_alpha = alpha;

            fs_params.sky_tex_id0 =
                vtext_get_or_create("sky_text0", g->sky_text.texts[0]).index;
            fs_params.sky_tex_id1 =
                vtext_get_or_create("sky_text1", g->sky_text.texts[1]).index;
        }

        // TODO: cleanup
        fs_params.fog_dist =
            r->level->fog.dist == 0.0f ? DEFAULT_FOG_DIST : r->level->fog.dist;

        if (fade_origin) {
            fs_params.fade_enabled = true;
            fs_params.fade_origin = fade_origin->pos;
        } else {
            fs_params.fade_enabled = false;
        }

        g->extra_bloom = 0.25f;

        if (g->player) {
            // bloom burst on damage
            const stime_t since_damage_s =
                secs_since_tick(g->player->last_damage_tick);
            g->extra_bloom = 0.25f + (3.5f * (invsatf(since_damage_s / 0.25f)));

            if (g->player->sector
                && sector_type(g->player->sector)->is_liquid) {
                const f32 liquid_height =
                    g->player->sector->floor.z
                        + g->player->sector->liquid_offset;

                if (g->cam.pos.z < liquid_height) {
                    // extra bloom under liquids
                    g->extra_bloom += 0.25f;
                }
            }
        }

        fs_params.extra_bloom = g->extra_bloom;
        fs_params.camera_pos = g->cam.pos;
        fs_params.liquid_fall_effect = g->liquid_fall.effect;

        fs_params.near_plane_no_pitch =
            cam_view_proj_to_frustum(&g->cam.view_proj_no_pitch)
                .planes[F3D_PLANE_NEAR];

        fs_params.visopt = g->visopt;

        if (g->mode != GAMEMODE_EDITOR) {
            fs_params.visopt &= ~VISOPT_HIGHLIGHT_3D;
        }

        fs_params.blocks_size = g->level->blocks.size;
        fs_params.blocks_offset = g->level->blocks.offset;
        fs_params.tick = g->tick;
        fs_params.time_s = g->time.total_scaled_s;

        sg_bindings composite_bindings = {
            .fs = {
                .images = {
                    [SLOT_composite_d_color] =
                        g_passes.deferred.color,
                    [SLOT_composite_d_pos_w_id] =
                        g_passes.deferred.pos_w_id,
                    [SLOT_composite_d_pos_v_index_type_flags] =
                        g_passes.deferred.pos_v_index_type_flags,
                    [SLOT_composite_d_normal_uv] =
                        g_passes.deferred.normal_uv,
                    [SLOT_composite_shadow_image] =
                        g_passes.shadow.image,
                },
                .samplers[SLOT_composite_smp_nearest] = r->smp_nearest,
            },
        };

        tex_atlas_apply_bindings(
            r->pipelines.composite, &composite_bindings);
        apply_level_data_bindings(r->pipelines.composite, &composite_bindings);
        apply_light_data_bindings(r->pipelines.composite, &composite_bindings);

        screenquad_render_ex(
            r->pipelines.composite,
            &composite_bindings,
            SLOT_composite_vs_params,
            SG_RANGE_REF(vs_params),
            SLOT_composite_fs_params,
            SG_RANGE_REF(fs_params));
    }
    sg_pop_debug_group();
    sg_end_pass();

    // maybe phantom if there are models to be drawn
    if (dynlist_size(r->frame_phantom_models) != 0) {
        // use +x as forward
        g->cam.phantom_view = cam_lookat(v3_of(0), v3_of(-1, 0, 0), v3_of(0, 0, 1));

        const f32 aspect = g->target_size.x / (f32) g->target_size.y;

        // NOTE: uses orthographic since perspective would distort in edges
        g->cam.phantom_proj =
            cam_ortho(0.0f, 1.0f * aspect, 0.0f, 1.0f, 1.0f, -1.0f);
        g->cam.phantom_view_proj =
            m4_mul(g->cam.phantom_proj, g->cam.phantom_view);
        g->cam.inv_phantom_view_proj = m4_inv(g->cam.phantom_view_proj);

        sg_begin_pass(
            &(sg_pass) {
                .attachments = g_passes.phantom.attach,
                .action = {
                    .colors[0] = { .load_action = SG_LOADACTION_CLEAR, },
                    .depth = {
                        .load_action = SG_LOADACTION_CLEAR,
                        .clear_value = 100.0f
                    }
                },
                .label = "phantom.pass",
            });
        sg_push_debug_group("phantom");
        {
            render_phantom_models();
        }
        sg_pop_debug_group();
        sg_end_pass();
    }

    // bloom blur
    scale_blur_process(
        &g_passes.bloom.images[0],
        &g_passes.bloom.attaches[0],
        ARRLEN(g_passes.bloom.images));

    // edges
    edge_process();

    if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        // TODO: fix for some weird out of order stuff happening?
        sg_commit();
    }

    // post pass (0)
    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.post0.attach,
            .action = {
                .colors[0] = { .load_action = SG_LOADACTION_CLEAR },
                .colors[1] = { .load_action = SG_LOADACTION_CLEAR },
                .colors[2] = { .load_action = SG_LOADACTION_CLEAR },
            },
            .label = "post0.pass",
        });
    sg_push_debug_group("post0");
    {
        post0_vs_params_t vs_params = { 0 };
        screenquad_mats(
            &vs_params.model,
            &vs_params.view,
            &vs_params.proj);

        post0_fs_params_t fs_params = { 0 };
        fs_params.tick = g->tick;
        fs_params.time_s = g->time.total_scaled_s;
        fs_params.visopt = g->visopt;

        // particle params
        fs_params.n_particles = r->psim.n_particles;
        fs_params.particle_kern_radius = g->psim->desc.kern_radius;
        fs_params.particle_disp_scale = 100.0f;
        fs_params.particle_disp_radius = 0.30f; // NOTE NOTE NOTE: must be <= kern radius or things will be fucked up!
        fs_params.particle_bounds = g->psim->desc.bounds;
        fs_params.particle_render_offset = g->psim->desc.render_offset;
        fs_params.particle_cell_dims = r->psim.cell_dims;
        fs_params.particle_color_left = compute_left_hand_particle_color();
        fs_params.particle_color_right = compute_right_hand_particle_color();

        fs_params.override_color = g->hand_override_color;

        sg_bindings bindings = {
            .fs = {
                .images = {
                    [SLOT_post0_d_pos_v_index_type_flags] =
                        g_passes.deferred.pos_v_index_type_flags,
                    [SLOT_post0_c_color] = g_passes.composite.color,
                    [SLOT_post0_c_extra_id_pos] =
                        g_passes.composite.extra_id_pos,
                    [SLOT_post0_c_bloom] = g_passes.composite.bloom,
                    [SLOT_post0_c_light] = g_passes.composite.light,
                },
                .samplers[SLOT_post0_smp_nearest] = r->smp_nearest,
                .storage_buffers[SLOT_post0_particle_buffer] =
                    r->psim.particles_buffer,
                .storage_buffers[SLOT_post0_cell_buffer] =
                    r->psim.cells_buffer,
            },
        };

        screenquad_render_ex(
            r->pipelines.post0,
            &bindings,
            SLOT_post0_vs_params,
            SG_RANGE_REF(vs_params),
            SLOT_post0_fs_params,
            SG_RANGE_REF(fs_params));
    }
    sg_pop_debug_group();
    sg_end_pass();

    // post pass (1)
    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.post1.attach,
            .action = {
                .colors[0] = { .load_action = SG_LOADACTION_CLEAR },
                .colors[1] = { .load_action = SG_LOADACTION_CLEAR },
            },
            .label = "post1.pass",
        });
    sg_push_debug_group("post1");
    {
        post1_vs_params_t vs_params;
        screenquad_mats(
            &vs_params.model,
            &vs_params.view,
            &vs_params.proj);

        post1_fs_params_t fs_params = { 0 };
        fs_params.view = g->cam.view;
        fs_params.inv_view = g->cam.inv_view;
        fs_params.tick = g->tick;
        fs_params.time_s = g->time.total_scaled_s;
        fs_params.visopt = g->visopt;
        fs_params.gamma = g->gamma;
        fs_params.near_plane_no_pitch =
            cam_view_proj_to_frustum(&g->cam.view_proj_no_pitch)
                .planes[F3D_PLANE_NEAR];
        fs_params.cam_pos = g->cam.pos;
        fs_params.fog_enabled = fade_origin == NULL ? 1 : 0;
        fs_params.no_dither256 = g->debug.no_dither256;
        fs_params.no_dither8 = g->debug.no_dither8;
        fs_params.no_bloom = g->debug.no_bloom;

        sg_bindings bindings = {
            .fs = {
                .images = {
                    [SLOT_post1_c_color] = g_passes.post0.color,
                    [SLOT_post1_bloom_image] =
                        g_passes.bloom.images[
                            ARRLEN(g_passes.bloom.images) - 1],
                    [SLOT_post1_c_light] = g_passes.post0.light,
                    [SLOT_post1_c_extra_id_pos] =
                        g_passes.composite.extra_id_pos,
                    [SLOT_post1_d_pos_v_index_type_flags] =
                        g_passes.deferred.pos_v_index_type_flags,
                    [SLOT_post1_d_normal_uv] =
                        g_passes.deferred.normal_uv,
                    [SLOT_post1_palette_level] =
                        g->palettes.level->map_image,
                    [SLOT_post1_palette_generic] =
                        g->palettes.generic->map_image,
                },
                .samplers[SLOT_post1_smp_nearest] = r->smp_nearest,
                .samplers[SLOT_post1_smp_linear] = r->smp_linear,
            },
        };
        tex_atlas_apply_bindings(r->pipelines.post1, &bindings);
        apply_level_data_bindings(r->pipelines.post1, &bindings);

        screenquad_render_ex(
            r->pipelines.post1,
            &bindings,
            SLOT_post1_vs_params,
            SG_RANGE_REF(vs_params),
            SLOT_post1_fs_params,
            SG_RANGE_REF(fs_params));
    }
    sg_pop_debug_group();
    sg_end_pass();

    // TODO: cleanup
    // other blur
    if (g->liquid_fall.effect > 0.0f) {
        scale_blur_process(
            &g_passes.blur.images[0],
            &g_passes.blur.attaches[0],
            ARRLEN(g_passes.blur.images));
    }

    if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        // TODO: fix for some weird out of order stuff happening?
        sg_commit();
    }

    // post pass (2)
    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.post2.attach,
            .action = {
                .colors[0] = { .load_action = SG_LOADACTION_CLEAR }
            },
            .label = "post2.pass",
        });
    sg_push_debug_group("post2");
    {
        post2_vs_params_t vs_params = { 0 };
        screenquad_mats(
            &vs_params.model,
            &vs_params.view,
            &vs_params.proj);

        post2_fs_params_t fs_params = { 0 };
        fs_params.view = g->cam.view;
        fs_params.inv_view = g->cam.inv_view;
        fs_params.tick = g->tick;
        fs_params.time_s = g->time.total_scaled_s;
        fs_params.visopt = g->visopt;
        fs_params.cam_pos = g->cam.pos;
        fs_params.contrast_effect = g->contrast_effect;
        fs_params.phantom_color = g->phantom_color;
        if (g->debug.no_phantom) {
            fs_params.phantom_color.a = 0.0f;
        }
        fs_params.transition_effect = g->transition_effect.strength;
        fs_params.transition_tex =
            vtext_get_or_create(
                "transition_tex",
                g->transition_effect.text).index;
        fs_params.last_heartbeat_s = ns_to_secs(g->last_heartbeat_ns);
        fs_params.no_dither256 = g->debug.no_dither256;
        fs_params.no_bloom = g->debug.no_bloom;

        {
            fs_params.vignette_tex0 =
                vtext_get_or_create(
                    "vignette_tex0",
                    g->vignette.texts[0]).index;
            fs_params.vignette_tex1 =
                vtext_get_or_create(
                    "vignette_tex1",
                    g->vignette.texts[1]).index;

            // lerp vignette state
            static f32 tex_mix, strength;
            static v4 color, tex_color;

            tex_mix = dtlerp(tex_mix, g->vignette.tex_mix, 10.0f, dt);
            fs_params.vignette_tex_mix = tex_mix;

            strength = dtlerp(strength, g->vignette.strength, 10.0f, dt);
            fs_params.vignette_strength = strength;

            color =
                color_lerp_rgba(color, g->vignette.color, dtlerp_t(10.0f, dt));
            fs_params.vignette_color = color;

            tex_color =
                color_lerp_rgba(
                    tex_color,
                    g->vignette.tex_color,
                    dtlerp_t(10.0f, dt));
            fs_params.vignette_tex_color = tex_color;

            if (g->debug.no_vignette) {
                fs_params.vignette_strength = 0.0f;
                fs_params.vignette_color.a = 0.0f;
            }
        }

        if (g->player) {
            fs_params.hit_tick = g->player->last_damage_tick;
        }

        if (g->mode == GAMEMODE_EDITOR
            && g_editor
            && (g->visopt & VISOPT_HIGHLIGHT_3D)) {
            fs_params.highlight_id = lptr_to_nogen(g_editor->highlight.ptr).raw;
        }

        if (g->mode == GAMEMODE_EDITOR
            && g_editor) {
            nstime_t change_time;
            lptr_t change_ptr = LPTR_NULL;
            map_each(lptr_t, nstime_t, &g_editor->changed_ptr_to_time, it) {
                if (lptr_is_null(change_ptr) || *it.value > change_time) {
                    change_ptr = *it.key;
                    change_time = *it.value;
                }
            }

            fs_params.changed_id = lptr_to_nogen(change_ptr).raw;
        }

        if (g->player) {
            static f32 speed_effect = 0.0f;
            f32 speed_effect_target = 0.0f;

            if (g->player->dashing) {
                speed_effect_target = 1.0f;
            } else if (g->player->sliding) {
                const v2
                    d = v2_normalize(g->player->vel),
                    p =
                        v2_proj(
                            g->player->vel,
                            v2_from(g->cam.dir));

                const f32 power =
                    satf((v2_norm(p) - 5.0f) / 5.0f)
                        * max(v2_dot(d, v2_from(g->cam.dir)), 0.1f);

                speed_effect_target =
                    satf(
                        secs_since_tick(g->player->last_slide_begin_tick)
                            / 0.05f)
                    * power;
            }

            speed_effect =
                dtlerp(
                    speed_effect,
                    speed_effect_target,
                    10.0f,
                    g->time.frame.dt_scaled);
            fs_params.speed_effect = speed_effect;
        } else {
            fs_params.speed_effect = 0.0;
        }

        fs_params.liquid_fall_effect = g->liquid_fall.effect;

        if (g->liquid_fall.effect > 0.0f) {
            // lerp liquid fall vars
            static f32 alpha, mix;

            alpha =
                dtlerp(
                    alpha,
                    g->liquid_fall.alpha,
                    8.0f,
                    g->time.frame.dt_scaled);
            fs_params.liquid_fall_tex_alpha = alpha;

            mix =
                dtlerp(
                    mix,
                    g->liquid_fall.mix,
                    12.0f,
                    g->time.frame.dt_scaled);
            fs_params.liquid_fall_tex_mix = mix;

            fs_params.liquid_fall_tex0 =
                vtext_get_or_create(
                    "liquid_fall_tex0", g->liquid_fall.texts[0]).index;
            fs_params.liquid_fall_tex1 =
                vtext_get_or_create(
                    "liquid_fall_tex1", g->liquid_fall.texts[1]).index;
        }

        sg_bindings bindings = {
            .fs = {
                .images = {
                    [SLOT_post2_base_image] = g_passes.post1.color,
                    [SLOT_post2_light_image] = g_passes.post0.light,
                    [SLOT_post2_bloom_image] =
                        g_passes.bloom.images[
                            ARRLEN(g_passes.bloom.images) - 1],

                    [SLOT_post2_blur_image] =
                        g_passes.blur.images[
                            ARRLEN(g_passes.blur.images) - 1],
                    [SLOT_post2_c_extra_id_pos] =
                        g_passes.composite.extra_id_pos,
                    [SLOT_post2_d_pos_v_index_type_flags] =
                        g_passes.deferred.pos_v_index_type_flags,
                    [SLOT_post2_palette_generic] =
                        g->palettes.generic->map_image,
                    [SLOT_post2_phantom_image] =
                        g_passes.phantom.color,
                    [SLOT_post2_edge_image] =
                        g_passes.edge.color,
                },
                .samplers[SLOT_post2_smp_nearest] = r->smp_nearest,
                .samplers[SLOT_post2_smp_linear] = r->smp_linear,
            },
        };
        tex_atlas_apply_bindings(r->pipelines.post1, &bindings);

        screenquad_render_ex(
            r->pipelines.post2,
            &bindings,
            SLOT_post2_vs_params,
            SG_RANGE_REF(vs_params),
            SLOT_post2_fs_params,
            SG_RANGE_REF(fs_params));
    }
    sg_pop_debug_group();
    sg_end_pass();

    // reset models, sprites
    dynlist_resize(r->frame_models, 0);
    dynlist_resize(r->frame_phantom_models, 0);
    dynlist_resize(r->prepared_frame_models, 0);
    dynlist_resize(r->frame_sprites, 0);
}

static void callback_pixel_data_query(
        sg_image,
        const void *data,
        int sz,
        void *userdata) {
    renderer_pixel_data_t *pdata = userdata;
    if (!pdata->data || !pdata->last_request_frame) {
        // can occur if pixels are queried -> renderer is reset -> pixels are
        // retrieved
        return;
    }

    ASSERT(pdata->data);
    ASSERT(
        sz ==
            pdata->size.x
                * pdata->size.y
                * sg_query_pixelformat(pdata->format).bytes_per_pixel);
    memcpy(pdata->data, data, sz);
}

void renderer_do_query_pixels() {
    struct {
        renderer_pixel_data_t *pdata;
        sg_image image;
    } queries[] = {
         {
            .pdata = &r->pixel_data.extra_id_pos,
            .image = g_passes.composite.extra_id_pos,
         },
    };

    for (int i = 0; i < ARRLEN(queries); i++) {
        typeof(queries[i]) *q = &queries[i];
        const sg_image_desc image_desc = sg_query_image_desc(q->image);

        if (q->pdata->format == 0) {
            q->pdata->format = image_desc.pixel_format;
        }

        const v2i image_size = v2i_of(image_desc.width, image_desc.height);
        if (!v2i_eqv(q->pdata->size, image_size)) {
            if (q->pdata->data) {
                mem_free(&r->arena, q->pdata->data);
                q->pdata->data = NULL;
            }

            q->pdata->size = image_size;
        }


        if (!q->pdata->data) {
            const int bytes_per_pixel =
                sg_query_pixelformat(image_desc.pixel_format).bytes_per_pixel;

            q->pdata->data =
                mem_alloc(
                    &r->arena,
                    image_size.x
                        * image_size.y
                        * bytes_per_pixel);
        }

        if (q->pdata->last_request_frame != g->time.frame.count) {
            q->pdata->last_request_frame = g->time.frame.count;

            // request pixels
            sg_ext_query_image_pixels(
                g_passes.composite.extra_id_pos,
                callback_pixel_data_query,
                q->pdata);
        }
    }
}

v4 renderer_info_at(v2i pos) {
    if (!g->allow_editor_picking) { return v4_of(0); }

    if (!r->pixel_data.extra_id_pos.data) {
        return v4_of(0);
    }

    const int index = (g->target_size.y - pos.y - 1) * g->target_size.x + pos.x;
    return ((v4*) r->pixel_data.extra_id_pos.data)[index];
}

void renderer_add_tint( const screen_tint_t *tint) {
    screen_tint_t *t = dynlist_push(r->tints);
    *t = *tint;
    t->start_frame = g->time.frame.count;
    t->start_abs_ns = g->time.total_ns;
}

// TODO: error/warn when there are too many lights? just prioritize according
// to distance

// render shadow map for specified light
static void update_shadow_map_for_light(light_info_t *l) {
    const int
        vx = l->shadow.index % SHADOW_MAP_SIZE_LIGHTS,
        vy = l->shadow.index / SHADOW_MAP_SIZE_LIGHTS;

    sg_apply_viewport(
        vx * SHADOW_MAP_PER_LIGHT,
#ifdef SOKOL_METAL
        (SHADOW_MAP_SIZE_LIGHTS - vy - 1) * SHADOW_MAP_PER_LIGHT,
#else
        vy,
#endif // ifdef SOKOL_METAL
        SHADOW_MAP_PER_LIGHT,
        SHADOW_MAP_PER_LIGHT,
        false);

    shadow_map_vs_params_t vs_params = { 0 };
    screenquad_mats(
        &vs_params.model,
        &vs_params.view,
        &vs_params.proj);

    shadow_map_fs_params_t fs_params = { 0 };
    fs_params.l_pos = l->desc.pos;
    fs_params.l_attenuation = l->desc.params.attenuation;
    fs_params.l_span = l->span;
    fs_params.l_sector = -1;
    fs_params.l_side = -1;

    sector_t *sector = NULL;
    side_t *side = NULL;

    if ((sector = lptr_sector(r->level, l->desc.sector))) {
        fs_params.l_sector = l->desc.sector.id;
        fs_params.l_plane =
            sector_plane_vec(
                sector,
                (l->desc.params.flags & LIGHT_FLAG_CEIL) ?
                    PLANE_TYPE_CEIL : PLANE_TYPE_FLOOR);
        fs_params.l_zs = v2_of(l->desc.pos.z);

        // combine continuous lines
        v2 a = v2_of(-1), b = v2_of(-1);

        int i = 0;
        llist_each(sector_sides, &sector->sides, it) {
            vertex_t *vs[2];
            side_get_vertices(it.el, vs);

            if (v2_eqv_eps(a, v2_of(-1))) {
                a = vs[0]->pos; b = vs[1]->pos;
            } else if (
                segments_are_colinear(a, b, vs[0]->pos, vs[1]->pos, 0.001f)) {
                b = vs[1]->pos;
            } else {
                // emit, reset
                fs_params.l_lines[i++] = v4_of(a.x, a.y, b.x, b.y);
                a = v2_of(-1);
                b = v2_of(-1);
            }

            if (i >= MAX_LIGHT_LINES) { break; }
        }

        // emit final line
        if (!v2_eqv_eps(a, v2_of(-1))) {
            fs_params.l_lines[i++] = v4_of(a.x, a.y, b.x, b.y);
        }

        fs_params.n_l_lines = i;
    } else if ((side = lptr_side(r->level, l->desc.side))) {
        fs_params.l_side = l->desc.side.id;

        vertex_t *vs[2];
        side_get_vertices(side, vs);

        fs_params.n_l_lines = 1;
        fs_params.l_lines[0] =
            v4_of(
                vs[0]->pos.x, vs[0]->pos.y,
                vs[1]->pos.x, vs[1]->pos.y);

        fs_params.l_plane = side_plane_vec(side);
        fs_params.l_zs = v2_of(side->sector->floor.z, side->sector->ceil.z);
    }

    fs_params.blocks_size = r->level->blocks.size;
    fs_params.blocks_offset = r->level->blocks.offset;

    sg_bindings bindings = { 0 };

    apply_light_data_bindings(r->pipelines.shadow, &bindings);

    screenquad_render_ex(
        r->pipelines.shadow,
        &bindings,
        SLOT_shadow_map_vs_params,
        SG_RANGE_REF(vs_params),
        SLOT_shadow_map_fs_params,
        SG_RANGE_REF(fs_params));
}

static bool traverse_blocks_hash(
    level_t *level, block_t *block, v2i, void *userdata) {
    hash_t *hash = userdata;
    *hash = hash_add_int(*hash, block->version);
    return true;
}

static hash_t light_blocks_hash(level_t *level, light_desc_t *desc) {
    hash_t hash = 0x12345;
    level_traverse_block_area(
        level,
        v2_sub(v2_from(desc->pos), v2_of(desc->params.attenuation)),
        v2_add(v2_from(desc->pos), v2_of(desc->params.attenuation)),
        traverse_blocks_hash,
        &hash,
        LTB_NONE);
    return hash;
}

static int light_info_shadow_priority_cmp(
    const void *a, const void *b, void*) {
    return
        (*(light_info_t**) a)->shadow.priority
            - (*(light_info_t**) b)->shadow.priority;
}

// gets shadow map index for light if it doesn't have one, might bump other
// lights off if this lights is a higher priority
static void light_get_or_allocate_shadow_map_index(light_info_t *l) {
    // still OK
    if (l->shadow.index != -1) { return; }

    const int i = bitmap_find(&r->light.indices, 0, false);

    // found a spot
    if (i != -1) {
        bitmap_set(&r->light.indices, i);
        l->shadow.index = i;
        return;
    }

    // bump a lower priority light out
    map_each(int, light_info_t, &r->light.info, it) {
        if (it.value->priority < l->priority
            && !(it.value->desc.params.flags & LIGHT_FLAG_NO_SHADOWS)
            && it.value->shadow.index != -1) {
            // steal index from this light
            l->shadow.index = it.value->shadow.index;
            it.value->shadow.index = -1;
        }
    }
}

static void do_lights_pass() {
    static int next_env_light_id = 1;
    RELOAD_STATIC_VAR(next_env_light_id);

    // remove old env lights, add to frame lights
    dynlist_each(r->env_lights, it) {
        if (!it.el->id) { it.el->id = next_env_light_id++; }

        if (it.el->start_secs == 0.0f) {
            it.el->start_secs = g->time.total_scaled_s;
        }

        const f32 alive_secs = g->time.total_scaled_s - it.el->start_secs;
        if (alive_secs >= it.el->duration_secs) {
            dynlist_remove_it(r->env_lights, it);
            continue;
        }

        light_desc_t desc = {
            .id = LIGHT_ID_FROM(LIGHT_TYPE_ENV, it.el->id),
            .pos = it.el->pos,
            .params = it.el->params
        };

        const f32 t = alive_secs / it.el->duration_secs;

        if (it.el->fade) {
            desc.params.attenuation = lerp(desc.params.attenuation, 0, t);

            if (desc.params.z_attenuation != 0.0f) {
                desc.params.z_attenuation =
                    lerp(desc.params.z_attenuation, 0, t);
            }

            desc.params.power = lerp(desc.params.power, 0, t);
            desc.params.color = v3_lerp(desc.params.color, v3_of(0), t);
        }

        *dynlist_push(r->frame_lights) = desc;
    }

    if (!map_valid(&r->light.info)) {
        map_init(
            &r->light.info,
            &r->arena,
            sizeof(int),
            sizeof(light_info_t),
            map_hash_bytes,
            map_cmp_bytes,
            NULL, NULL, NULL);
    }

    // pair r->light.info down to only lights which exist in current frame
    dynlist_each(r->frame_lights, it) {
        // ignore disabled lights
        if (it.el->params.flags & LIGHT_FLAG_DISABLE) { continue; }

        // ignore color-less lights
        if (v3_eqv_eps(it.el->params.color, v3_of(0))) { continue; }

        light_info_t *info = map_get(light_info_t, &r->light.info, it.el->id);

        if (!info) {
            info =
                map_insert(
                    &r->light.info,
                    it.el->id,
                    ((light_info_t) { .shadow.index = -1 }));

            // initialize priority only by distance
            info->priority = -v3_distance2(g->cam.pos, it.el->pos);
        }

        info->desc = *it.el;
        info->last_visible_frame = g->time.frame.count;

        sector_t *sector = NULL;
        side_t *side = NULL;

        if ((sector = lptr_sector(r->level, it.el->sector))) {
            info->span = v2_distance(sector->min, sector->max);
        } else if ((side = lptr_side(r->level, it.el->side))) {
            info->span = side->wall->len;
        } else {
            info->span = 0.0f;
        }
    }

    // remove lights which are not visible this frame
    map_each(int, light_info_t, &r->light.info, it) {
        if (it.value->last_visible_frame != g->time.frame.count) {
            if (it.value->shadow.index != -1) {
                bitmap_clr(&r->light.indices, it.value->shadow.index);
            }

            map_remove_it(&r->light.info, it);
        }
    }

    // find lights which need to have shadows updated
    DYNLIST(light_info_t*) to_update =
        dynlist_create(light_info_t*, &g->frame_arena);
    map_each(int, light_info_t, &r->light.info, it) {
        if (it.value->desc.params.flags & LIGHT_FLAG_NO_SHADOWS) { continue; }

        // check if update is needed at all
        it.value->blocks_hash =
            light_blocks_hash(r->level, &it.value->desc);

        if (it.value->shadow.blocks_hash == it.value->blocks_hash
            && v3_eqv_eps(it.value->shadow.pos, it.value->desc.pos)
            && it.value->shadow.attenuation == it.value->desc.params.attenuation
            && it.value->shadow.index != -1) {
            // nothing to update
            continue;
        }

        int priority = 0;

        // also works when last_update_frame is 0 (light is new), so as to
        // *really* prioritize lights which have never had shadow maps
        // generated
        priority += g->time.frame.count - it.value->shadow.last_update_frame;

        // deprioritize by distance
        priority -= v3_distance2(g->cam.pos, it.value->desc.pos);

        priority += it.value->desc.params.attenuation;

        it.value->shadow.priority = priority;

        *dynlist_push(to_update) = it.value;
    }

    // sort updateable lights by priority, lowest to highest
    dynlist_sort(to_update, light_info_shadow_priority_cmp, NULL);

    // update as many lights as possible until we run out of time
    f32 shadow_map_total_ms = 0;
    while (dynlist_size(to_update) != 0
           && shadow_map_total_ms < MAX_SHADOW_MAP_MS_PER_FRAME) {
        // pop off of end of list - this grabs highest priority first
        light_info_t *l = dynlist_pop(to_update);

        // does this light have an index? try to allocate one - and bump other
        // lights off if necessary
        light_get_or_allocate_shadow_map_index(l);

        // could not allocate shadow index, skip
        if (l->shadow.index == -1) { continue; }

        const i64 start = time_ns();
        update_shadow_map_for_light(l);
        const i64 end = time_ns();
        shadow_map_total_ms += (end - start) / 1000000.0f;

        l->last_visible_frame = g->time.frame.count;
        l->shadow.blocks_hash = l->blocks_hash;
        l->shadow.pos = l->desc.pos;
        l->shadow.attenuation = l->desc.params.attenuation;
        l->shadow.last_update_frame = g->time.frame.count;
    }

    // update light data buffer with all current valid lights
    int n_lights = 0;
    map_each(int, light_info_t, &r->light.info, it) {
        if (n_lights >= MAX_LIGHTS) { break; }

        light_t l = {
            .pos = it.value->desc.pos,
            .color = it.value->desc.params.color,
            .flags = it.value->desc.params.flags,
            .id = it.value->desc.id,
            .span = it.value->span,
            .ambient = it.value->desc.params.ambient,
            .c1 = it.value->desc.params.c1,
            .c2 = it.value->desc.params.c2,
            .power = it.value->desc.params.power,
            .z_attenuation = it.value->desc.params.z_attenuation,
        };

        if ((it.value->desc.params.flags & LIGHT_FLAG_NO_SHADOWS)
            || it.value->shadow.index == -1) {
            l.attenuation = it.value->desc.params.attenuation;
            l.shadow_index = -1;
        } else {
            l.attenuation = it.value->shadow.attenuation,
            l.shadow_index = it.value->shadow.index;
        }

        sector_t *sector = NULL;
        side_t *side = NULL;

        if ((sector = lptr_sector(r->level, it.value->desc.sector))) {
            l.plane =
                sector_plane_vec(
                    sector,
                    (it.value->desc.params.flags & LIGHT_FLAG_CEIL) ?
                        PLANE_TYPE_CEIL : PLANE_TYPE_FLOOR);
        } else if ((side = lptr_side(r->level, it.value->desc.side))) {
            l.plane = side_plane_vec(side);
        }

        r->light.arr[n_lights] = l;
        n_lights++;
    }

    r->light.arr[min(n_lights, MAX_LIGHTS - 1)] =
        (light_t) {
            .pos = v3_of(-1),
            .attenuation = -1,
            .color = v3_of(-1),
            .flags = -1,
            .shadow_index = -1,
        };

    // always include backstop light
    r->light.n_lights = min(n_lights + 1, MAX_LIGHTS);

    // resize for next frame
    dynlist_resize(r->frame_lights, 0);
}

void renderer_lights_for(lptr_t ptr, DYNLIST(light_info_t*) *out) {
    map_each(int, light_info_t, &r->light.info, it) {
        level_type_e type;

        switch (LIGHT_ID_TYPE(it.value->desc.id)) {
        case LIGHT_TYPE_CEIL:
        case LIGHT_TYPE_FLOOR:
        case LIGHT_TYPE_LIQUID:
            type = LT_SECTOR;
            break;
        case LIGHT_TYPE_SIDE:
            type = LT_SIDE;
            break;
        case LIGHT_TYPE_ENTITY:
            type = LT_ENTITY;
            break;
        case LIGHT_TYPE_DECAL:
            type = LT_DECAL;
            break;
        default:
            continue;
        }

        if (type == lptr_type(ptr)
            && LIGHT_ID_INDEX(it.value->desc.id)
                == lptr_to_index(r->level, ptr)) {
            *dynlist_push(*out) = it.value;
        }
    }
}
