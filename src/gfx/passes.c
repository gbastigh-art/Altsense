#include "gfx/passes.h"
#include "gfx/renderer.h"
#include "util/hooks.h"
#include "reloadhost.h"
#include "game.h"

passes_t g_passes;
RELOAD_STATIC_GLOBAL(g_passes)

static void make_passes() {
    const v2i size = g_passes.size;
    ASSERT(!v2i_eqv(size, v2i_of(0)));
    LOG("making passes at %d x %d", size.x, size.y);

    g_passes.deferred.color =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA8,
                .label = "deferred.color",
            });

    g_passes.deferred.pos_w_id =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA32F,
                .label = "deferred.pos_w_id",
            });

    g_passes.deferred.pos_v_index_type_flags =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA32F,
                .label = "deferred.pos_v_index_type_flags",
            });

    g_passes.deferred.normal_uv =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA16F,
                .label = "deferred.normal_uv",
            });

    g_passes.deferred.depth_stencil =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                .label = "deferred.depth_stencil",
            });

    g_passes.deferred.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.deferred.color,
                .colors[1].image = g_passes.deferred.pos_w_id,
                .colors[2].image = g_passes.deferred.pos_v_index_type_flags,
                .colors[3].image = g_passes.deferred.normal_uv,
                .depth_stencil.image = g_passes.deferred.depth_stencil,
                .label = "deferred.attach",
            });

    g_passes.composite.color =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA8,
                .label = "composite.color",
            });

    g_passes.composite.extra_id_pos =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA32F,
                .label = "composite.extra_id_pos",
            });

    g_passes.composite.light =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA8,
                .label = "composite.light",
            });

    g_passes.composite.bloom =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA16F,
                .label = "composite.bloom",
            });

    g_passes.composite.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.composite.color,
                .colors[1].image = g_passes.composite.extra_id_pos,
                .colors[2].image = g_passes.composite.light,
                .colors[3].image = g_passes.composite.bloom,
                .label = "composite.attach",
            });

    g_passes.phantom.color =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_RGBA8,
                .label = "phantom.color",
            });

    g_passes.phantom.depth =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_DEPTH,
                .label = "phantom.depth",
            });

    g_passes.phantom.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.phantom.color,
                .depth_stencil.image = g_passes.phantom.depth,
                .label = "phantom.attach",
            });

    g_passes.edge.color =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_R8,
                .label = "edge.color",
            });

    g_passes.edge.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.edge.color,
                .label = "edge.attach",
            });

    for (int i = 0; i < ARRLEN(g_passes.bloom.images); i++) {
        int d;
        if (i <= ARRLEN(g_passes.bloom.images) / 2) {
            d = max(i * 2, 1);
        } else {
            d = max(((ARRLEN(g_passes.bloom.images) - 1) - i) * 2, 1);
        }

        g_passes.bloom.images[i] =
            sg_make_image(
                &(sg_image_desc) {
                    .render_target = true,
                    .width = size.x / d,
                    .height = size.y / d,
                    .pixel_format = SG_PIXELFORMAT_RGBA16F,
                    .label = mem_strfmt(tlscratch(), "bloom.image%d", i),
                });

        g_passes.bloom.attaches[i] =
            sg_make_attachments(
                &(sg_attachments_desc) {
                    .colors[0].image = g_passes.bloom.images[i],
                    .label = mem_strfmt(tlscratch(), "bloom.attach%d", i),
                });
    }

    for (int i = 0; i < ARRLEN(g_passes.blur.images); i++) {
        int d;
        if (i <= ARRLEN(g_passes.blur.images) / 2) {
            d = max(i * 2, 1);
        } else {
            d = max(((ARRLEN(g_passes.blur.images) - 1) - i) * 2, 1);
        }

        g_passes.blur.images[i] =
            sg_make_image(
                &(sg_image_desc) {
                    .render_target = true,
                    .width = size.x / d,
                    .height = size.y / d,
                    .pixel_format = SG_PIXELFORMAT_RGBA16F,
                    .label = mem_strfmt(tlscratch(), "blur.image%d", i),
                });

        g_passes.blur.attaches[i] =
            sg_make_attachments(
                &(sg_attachments_desc) {
                    .colors[0].image = g_passes.blur.images[i],
                    .label = mem_strfmt(tlscratch(), "blur.attach%d", i),
                });
    }

    sg_image_desc c_color_desc =
        sg_query_image_desc(g_passes.composite.color);
    c_color_desc.label = "post0.color";
    g_passes.post0.color = sg_make_image(&c_color_desc);

    sg_image_desc c_light_desc =
        sg_query_image_desc(g_passes.composite.light);
    c_light_desc.label = "post0.light";
    g_passes.post0.light = sg_make_image(&c_light_desc);

    g_passes.post0.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.post0.color,
                .colors[1].image = g_passes.post0.light,
                .colors[2].image = g_passes.bloom.images[0],
                .label = "post0.attach",
            });

    g_passes.post1.color =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                // use RGBA16F to allow for color values >1
                .pixel_format = SG_PIXELFORMAT_RGBA16F,
                .label = "post1.color",
            });

    g_passes.post1.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.post1.color,
                .colors[1].image = g_passes.blur.images[0],
                .label = "post1.attach",
            });

    g_passes.post2.color =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format =
                    sg_query_desc().environment.defaults.color_format,
                .label = "post2.color",
            });

    g_passes.post2.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.post2.color,
                .label = "post2.attach",
            }); 

    g_passes.overlay.color =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format =
                    sg_query_desc().environment.defaults.color_format,
                .label = "overlay.color",
            });

    g_passes.overlay.depth =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = size.x,
                .height = size.y,
                .pixel_format = SG_PIXELFORMAT_DEPTH,
                .label = "overlay.depth",
            });

    g_passes.overlay.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.overlay.color,
                .depth_stencil.image = g_passes.overlay.depth,
                .label = "overlay.attach",
            });

    g_passes.editor.color =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = g->window_size.x,
                .height = g->window_size.y,
                .pixel_format =
                    sg_query_desc().environment.defaults.color_format,
                .label = "editor.color",
            });

    g_passes.editor.depth =
        sg_make_image(
            &(sg_image_desc) {
                .render_target = true,
                .width = g->window_size.x,
                .height = g->window_size.y,
                .pixel_format = SG_PIXELFORMAT_DEPTH,
                .label = "editor.depth",
            });

    g_passes.editor.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.editor.color,
                .depth_stencil.image = g_passes.editor.depth,
                .label = "editor.attach",
            });

    g_passes.shadow.image =
        sg_make_image(
            &(sg_image_desc) {
                .label = "shadow.image",
                .render_target = true,
                .type = SG_IMAGETYPE_2D,
                .width = SHADOW_MAP_SIZE,
                .height = SHADOW_MAP_SIZE,
                .pixel_format = SG_PIXELFORMAT_RGBA32F,
            });

    g_passes.shadow.attach =
        sg_make_attachments(
            &(sg_attachments_desc) {
                .colors[0].image = g_passes.shadow.image,
                .label = "shadow.attach",
            });

    // bit of a hack, reset lights if present
    if (g_renderer && map_valid(&g_renderer->light.info)) {
        map_clear(&g_renderer->light.info);
    }
}

static void destroy_passes() {
    // a nice little compiler error as a reminder to update these arrays
    // whenever the number of bloom images changes :)
    STATIC_ASSERT(ARRLEN(g_passes.bloom.images) == 5);

    sg_image *images[] = {
        &g_passes.deferred.color,
        &g_passes.deferred.pos_w_id,
        &g_passes.deferred.pos_v_index_type_flags,
        &g_passes.deferred.normal_uv,
        &g_passes.deferred.depth_stencil,
        &g_passes.composite.color,
        &g_passes.composite.extra_id_pos,
        &g_passes.composite.light,
        &g_passes.composite.bloom,
        &g_passes.phantom.color,
        &g_passes.phantom.depth,
        &g_passes.edge.color,
        &g_passes.overlay.color,
        &g_passes.overlay.depth,
        &g_passes.shadow.image,
        &g_passes.editor.color,
        &g_passes.editor.depth,
        &g_passes.bloom.images[0],
        &g_passes.bloom.images[1],
        &g_passes.bloom.images[2],
        &g_passes.bloom.images[3],
        &g_passes.bloom.images[4],
        &g_passes.blur.images[0],
        &g_passes.blur.images[1],
        &g_passes.blur.images[2],
        &g_passes.blur.images[3],
        &g_passes.blur.images[4],
        &g_passes.post0.color,
        &g_passes.post0.light,
        &g_passes.post1.color,
        &g_passes.post2.color,
    };

    sg_attachments *attachments[] = {
        &g_passes.deferred.attach,
        &g_passes.composite.attach,
        &g_passes.phantom.attach,
        &g_passes.edge.attach,
        &g_passes.overlay.attach,
        &g_passes.editor.attach,
        &g_passes.shadow.attach,
        &g_passes.bloom.attaches[0],
        &g_passes.bloom.attaches[1],
        &g_passes.bloom.attaches[2],
        &g_passes.bloom.attaches[3],
        &g_passes.bloom.attaches[4],
        &g_passes.blur.attaches[0],
        &g_passes.blur.attaches[1],
        &g_passes.blur.attaches[2],
        &g_passes.blur.attaches[3],
        &g_passes.blur.attaches[4],
        &g_passes.post0.attach,
        &g_passes.post1.attach,
        &g_passes.post2.attach,
    };

    for (int i = 0; i < ARRLEN(images); i++) {
        if (images[i]->id) {
            sg_destroy_image(*images[i]);
            images[i]->id = 0;
        }
    }

    for (int i = 0; i < ARRLEN(attachments); i++) {
        if (attachments[i]->id) {
            sg_destroy_attachments(*attachments[i]);
            attachments[i]->id = 0;
        }
    }
}

RELOAD_VISIBLE void passes_deinit(void*) {
    destroy_passes(); 
}

void passes_init() {
    hook_register(HOOK_EXIT, passes_deinit, NULL); 
    g_passes.size = g->target_size;
    make_passes();
}

void passes_update() {
    if (v2i_eqv(g_passes.size, g->target_size)) { return; }

    LOG(
        "target resize, remaking passes: %" PRIv2i " -> %" PRIv2i,
        FMTv2i(g_passes.size),
        FMTv2i(g->target_size));

    g_passes.size = g->target_size;
    destroy_passes();
    make_passes();
}
