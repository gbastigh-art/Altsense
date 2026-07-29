#include "gfx/scale_blur.h"
#include "gfx/renderer.h"
#include "gfx/screenquad.h"

#include "shader/downsample.glsl.h"
#include "shader/upsample.glsl.h"

void scale_blur_process(
        sg_image *images,
        sg_attachments *attachments,
        int n) {
    ASSERT(n % 2 == 1, "need odd number of images, got %d", n);

    const sg_pass_action action = {
        .colors[0].load_action = SG_LOADACTION_CLEAR,
    };

    downsample_vs_params_t ds_vs_params = { 0 };
    screenquad_mats(
        &ds_vs_params.model,
        &ds_vs_params.view,
        &ds_vs_params.proj);

    upsample_vs_params_t us_vs_params = { 0 };
    screenquad_mats(
        &us_vs_params.model,
        &us_vs_params.view,
        &us_vs_params.proj);

    // downsample
    for (int i = 0; i < (n / 2); i++) {
        sg_begin_pass(
            &(sg_pass) {
                .attachments = attachments[i + 1], // dst is always next image
                .action = action,
                .label = mem_strfmt(tlscratch(), "scale_blur.ds.%d", i),
            });
        {
            screenquad_render_ex(
                g_renderer->pipelines.downsample,
                &(sg_bindings) {
                    .fs.images[SLOT_downsample_src] = images[i],
                    .fs.samplers[SLOT_downsample_smp] = g_renderer->smp_linear,
                },
                SLOT_downsample_vs_params, SG_RANGE_REF(ds_vs_params),
                0, NULL);
        }
        sg_end_pass();
    }

    // upsample
    for (int i = n / 2; i < n - 1; i++) {
        sg_begin_pass(
            &(sg_pass) {
                .attachments = attachments[i + 1], // dst is always next image
                .action = action,
                .label = mem_strfmt(tlscratch(), "scale_blur.us.%d", i),
            });
        {
            screenquad_render_ex(
                g_renderer->pipelines.upsample,
                &(sg_bindings) {
                    .fs.images[SLOT_upsample_src] = images[i],
                    .fs.samplers[SLOT_upsample_smp] = g_renderer->smp_linear,
                },
                SLOT_upsample_vs_params, SG_RANGE_REF(us_vs_params),
                0, NULL);
        }
        sg_end_pass();
    }
}
