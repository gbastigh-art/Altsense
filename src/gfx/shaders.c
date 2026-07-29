#include "gfx/shaders.h"

#include "ext/sokol.h"
#include "game.h"
#include "util/hooks.h"
#include "util/math.h"
#include "util/assert.h"
#include "reloadhost.h"

ENUM_DEFINE(shader, SHADER, ENUM_SHADER)

// NOTE: include all shaders! so they don't have to be impl'd elsewhere
#define SOKOL_SHDC_IMPL
#include "shader/composite.glsl.h"
#include "shader/batch.glsl.h"
#include "shader/level.glsl.h"
#include "shader/model.glsl.h"
#include "shader/post0.glsl.h"
#include "shader/post1.glsl.h"
#include "shader/post2.glsl.h"
#include "shader/screenquad.glsl.h"
#include "shader/particle.glsl.h"
#include "shader/fancy_particle.glsl.h"
#include "shader/sgl.glsl.h"
#include "shader/sgp.glsl.h"
#include "shader/shadow_map.glsl.h"
#include "shader/sprite.glsl.h"
#include "shader/clear.glsl.h"
#include "shader/upsample.glsl.h"
#include "shader/downsample.glsl.h"
#include "shader/blood.glsl.h"
#include "shader/edge.glsl.h"

static const shader_refl_t shader_refl[SHADER_COUNT] = {
#define DO_SHADER_REFL(upper, lower, prog, ...)                      \
    [SHADER_##upper] = {                                             \
        .label = #lower "_" #prog "_shader",                         \
        .desc_fn = lower##_##prog##_shader_desc,                     \
        .attr_slot_fn = lower##_##prog##_attr_slot,                  \
        .image_slot_fn = lower##_##prog##_image_slot,                \
        .sampler_slot_fn = lower##_##prog##_sampler_slot,            \
        .storagebuffer_slot_fn = lower##_##prog##_storagebuffer_slot,\
    },                                                               \

    DO_SHADER_REFL(SCREENQUAD,     screenquad, program)
    DO_SHADER_REFL(BATCH,          batch, program)
    DO_SHADER_REFL(LEVEL,          level, program)
    DO_SHADER_REFL(SPRITE,         sprite, program)
    DO_SHADER_REFL(MODEL,          model, program_normal)
    DO_SHADER_REFL(MODEL_PHANTOM,  model, program_phantom)
    DO_SHADER_REFL(COMPOSITE,      composite, program)
    DO_SHADER_REFL(SHADOW_MAP,     shadow_map, program)
    DO_SHADER_REFL(POST0,          post0, program)
    DO_SHADER_REFL(POST1,          post1, program)
    DO_SHADER_REFL(POST2,          post2, program)
    DO_SHADER_REFL(PARTICLE,       particle, program)
    DO_SHADER_REFL(FANCY_PARTICLE, fancy_particle, program)
    DO_SHADER_REFL(SGL,            sgl, program)
    DO_SHADER_REFL(DOWNSAMPLE,     downsample, program)
    DO_SHADER_REFL(UPSAMPLE,       upsample, program)
    DO_SHADER_REFL(CLEAR,          clear, program)
    DO_SHADER_REFL(BLOOD,          blood, program)
    DO_SHADER_REFL(EDGE,           edge, program)
    DO_SHADER_REFL(SGP,            sgp, program)

#undef DO_SHADER_REFL
};

static sg_shader shaders[SHADER_COUNT];
RELOAD_STATIC_GLOBAL(shaders)

// list of pipelines registered to each shader
static DYNLIST(sg_pipeline*) pipelines[SHADER_COUNT];
RELOAD_STATIC_GLOBAL(pipelines)

RELOAD_VISIBLE void shaders_deinit(void*) {
    for (shader_e sh = SHADER_NONE + 1; sh <= SHADER_LAST; sh++) {
        sg_destroy_shader(shaders[sh]);
    }
}

void shaders_reload() {
    // fix for shaders being unloaded mid-program
    sg_reset_state_cache();

    for (shader_e s = SHADER_NONE + 1; s <= SHADER_LAST; s++) {
        // don't reload SGL, SGP shaders - too complicated to replace at runtime
        if (s == SHADER_SGL) { continue; }
        if (s == SHADER_SGP) { continue; }

        // recreate shader
        sg_destroy_shader(shaders[s]);
        LOG("making shader %s (%d)", shader_to_str(s), s);

        shaders[s] = sg_make_shader(shader_refl[s].desc_fn(sg_query_backend()));

        // recreate associated pipelines
        dynlist_each(pipelines[s], it) {
            sg_pipeline *pip = *it.el;

            if (!pip->id) {
                WARN("cannot reload bad pipeline %p", pip);
                continue;
            }

            sg_pipeline_desc desc = sg_query_pipeline_desc(*pip);
            LOG("  destroying 0x%08x...", pip->id);
            sg_destroy_pipeline(*pip);

            desc.shader = shaders[s];
            *pip = sg_make_pipeline(&desc);
            LOG("  recreated as 0x%08x", pip->id);
        }
    }
}

RELOAD_VISIBLE void shaders_post_reload(void*) {
    shaders_reload();
}

void shaders_init() {
    hook_register(HOOK_EXIT, shaders_deinit, NULL);
    hook_register(HOOK_POST_RELOAD, shaders_post_reload, NULL);

    for (shader_e s = SHADER_NONE + 1; s <= SHADER_LAST; s++) {
        LOG("making shader %s (%d)", shader_to_str(s), s);
        shaders[s] = sg_make_shader(shader_refl[s].desc_fn(sg_query_backend()));
        dynlist_init(pipelines[s], &g->arena);
    }
}

sg_shader shaders_get(shader_e sh) {
    ASSERT(sh && sh <= SHADER_LAST);
    return shaders[sh];
}

void shaders_register_pipeline(sg_pipeline *pip) {
    const sg_pipeline_desc desc = sg_query_pipeline_desc(*pip);
    ASSERT(desc.shader.id);

    shader_e sh;
    for (sh = SHADER_NONE + 1; sh <= SHADER_LAST; sh++) {
        if (shaders[sh].id == desc.shader.id) {
            break;
        }
    }
    ASSERT(sh != SHADER_COUNT);

    if (!pipelines[sh]) {
        dynlist_init(pipelines[sh], &g->arena);
    }

    *dynlist_push(pipelines[sh]) = pip;
}

void shaders_unregister_pipeline(sg_pipeline *pip) {
    const sg_pipeline_desc desc = sg_query_pipeline_desc(*pip);
    ASSERT(desc.shader.id);

    shader_e sh;
    for (sh = SHADER_NONE + 1; sh < SHADER_COUNT; sh++) {
        if (shaders[sh].id == desc.shader.id) {
            break;
        }
    }
    ASSERT(sh != SHADER_COUNT);
    ASSERT(pipelines[sh]);

    dynlist_each(pipelines[sh], it) {
        if (*it.el == pip) {
            dynlist_remove_it(pipelines[sh], it);
            return;
        }
    }

    ASSERT(false, "no pipeline %p for shader %s", pip, shader_to_str(sh));
}

const shader_refl_t *shader_reflect(shader_e sh) {
    ASSERT(sh > SHADER_NONE && sh < SHADER_COUNT);
    return &shader_refl[sh];
}

shader_e shader_for_pipeline(sg_pipeline pip) {
    const sg_pipeline_desc pip_desc = sg_query_pipeline_desc(pip);

    for (shader_e sh = SHADER_NONE + 1; sh < SHADER_COUNT; sh++) {
        if (shaders[sh].id == pip_desc.shader.id) {
            return sh;
        }
    }

    WARN("could not find SHADER_* for pipeline %s", pip_desc.label);
    return SHADER_NONE;
}
