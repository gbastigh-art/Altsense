#include "gfx/blit.h"
#include "gfx/screenquad.h"

#include "shader/screenquad.glsl.h"

void blit_process(sg_attachments attach, sg_image src, sg_sampler smp) {
    sg_begin_pass(
        &(sg_pass) {
            .attachments = attach,
            .action.colors[0].load_action = SG_LOADACTION_CLEAR,
            .label = "blit.pass",
        });
    {
        screenquad_render(
            sg_query_image_desc(
                sg_query_attachments_desc(attach).colors[0].image).pixel_format,
            SG_PIXELFORMAT_NONE,
            src,
            smp,
            &(screenquad_params_t) { 0 });
    }
    sg_end_pass();
}
