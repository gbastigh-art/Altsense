#include "blood.h"
#include "game.h"
#include "gfx/screenquad.h"
#include "gfx/shaders.h"
#include "gfx/tex_atlas.h"

#include "shader/blood.glsl.h"

RELOAD_VISIBLE void blood_render(
        tex_atlas_entry_t *entry,
        DYNLIST(sprite_t)*,
        void*) {
    sg_pipeline pipeline =
        screenquad_get_pipeline(
            SHADER_BLOOD,
            sg_query_image_desc(g_tex_atlas->virtual_image).pixel_format,
            true);

    blood_vs_params_t vs_params = {
        .model = m4_identity(),
        .view = m4_identity(),
        .proj = cam_ortho(0.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f),
    };
    blood_fs_params_t fs_params = {
        .id = hash_add_str(0x12345, entry->name) % 1000,
        .size_px = entry->size_px,
    };
    screenquad_render_ex(
        pipeline,
        &(sg_bindings) { },
        SLOT_blood_vs_params,
        &SG_RANGE(vs_params),
        SLOT_blood_fs_params,
        &SG_RANGE(fs_params));
}

void blood_init() {
    for (int i = 0; i < 32; i++) {
        const bool large = i >= 16;

        const char *name =
            mem_strfmt(
                tlscratch(),
                "blood_%s_%d",
                large ? "lg" : "sm",
                i % 16);

        const tex_atlas_entry_t *entry =
            tex_atlas_alloc_virtual(
                name,
                &(tex_atlas_virtual_data_t) {
                    .size = large ? v2i_of(64) : v2i_of(32),
                    .func = blood_render,
                });

        if (!entry) {
            WARN("could not make blood texture %d", i);
        }

        if (large) {
            g->blood_textures.large[i % 16] = entry->id;
        } else {
            g->blood_textures.small[i % 16] = entry->id;
        }
    }
}
