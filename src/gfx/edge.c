#include "gfx/passes.h"
#include "gfx/renderer.h"
#include "gfx/screenquad.h"

#include "shader/edge.glsl.h"

void edge_process() {
    edge_vs_params_t vs_params = { 0 };
    screenquad_mats(
        &vs_params.model,
        &vs_params.view,
        &vs_params.proj);

    sg_begin_pass(
        &(sg_pass) {
            .attachments = g_passes.edge.attach,
            .action.colors[0].load_action = SG_LOADACTION_CLEAR,
            .label = "edge.pass",
        });
    {
        screenquad_render_ex(
            g_renderer->pipelines.edge,
            &(sg_bindings) {
                .fs.images[SLOT_edge_image] = g_passes.composite.extra_id_pos,
                .fs.samplers[SLOT_edge_smp] = g_renderer->smp_nearest,
            },
            SLOT_edge_vs_params,
            SG_RANGE_REF(vs_params),
            0, NULL);
    }
    sg_end_pass();
}
