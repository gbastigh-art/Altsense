#pragma once

typedef struct sg_shader sg_shader;
typedef struct sg_pipeline sg_pipeline;

#include "util/enum.h"
#include "ext/sokol.h"

// all shader programs
#define ENUM_SHADER(F, ...)            \
    F(NONE,           0,  __VA_ARGS__) \
    F(SCREENQUAD,     1,  __VA_ARGS__) \
    F(BATCH,          2,  __VA_ARGS__) \
    F(LEVEL,          3,  __VA_ARGS__) \
    F(SPRITE,         4,  __VA_ARGS__) \
    F(MODEL,          5,  __VA_ARGS__) \
    F(COMPOSITE,      6,  __VA_ARGS__) \
    F(BLOOD,          7,  __VA_ARGS__) \
    F(SHADOW_MAP,     8,  __VA_ARGS__) \
    F(POST0,          9,  __VA_ARGS__) \
    F(POST1,          10, __VA_ARGS__) \
    F(POST2,          11, __VA_ARGS__) \
    F(PARTICLE,       12, __VA_ARGS__) \
    F(FANCY_PARTICLE, 13, __VA_ARGS__) \
    F(SGL,            14, __VA_ARGS__) \
    F(DOWNSAMPLE,     15, __VA_ARGS__) \
    F(UPSAMPLE,       16, __VA_ARGS__) \
    F(CLEAR,          17, __VA_ARGS__) \
    F(MODEL_PHANTOM,  18, __VA_ARGS__) \
    F(EDGE,           19, __VA_ARGS__) \
    F(SGP,            20, __VA_ARGS__) \

ENUM_DECL(shader, SHADER, ENUM_SHADER)

// init shaders
void shaders_init();

// reloads all shaders
void shaders_reload();

// retrieve shader
sg_shader shaders_get(shader_e sh);

// registers a pipeline to reload when its shader changes
void shaders_register_pipeline(sg_pipeline *pip);

// unregister a pipeline from hot-reload
void shaders_unregister_pipeline(sg_pipeline *pip);

// get shader_e from pipeline
shader_e shader_for_pipeline(sg_pipeline pip);

typedef struct {
    const char *label;
    const sg_shader_desc *(*desc_fn)(sg_backend);
    int (*attr_slot_fn)(const char*);
    int (*image_slot_fn)(sg_shader_stage, const char*);
    int (*sampler_slot_fn)(sg_shader_stage, const char*);
    int (*storagebuffer_slot_fn)(sg_shader_stage, const char*);
} shader_refl_t;

// get reflection info for shader
const shader_refl_t *shader_reflect(shader_e sh);
