#include "gfx/debug_draw.h"
#include "gfx/renderer.h"
#include "gfx/shaders.h"
#include "reloadhost.h"
#include "util/dynlist.h"
#include "game.h"

static DYNLIST(debug_draw_line_t)  lines;
static DYNLIST(debug_draw_point_t) points;
static DYNLIST(debug_draw_box_t)  boxes;
static DYNLIST(debug_draw_cyl_t)   cyls;
static sgl_pipeline pipeline;

// debug primitives
static struct {
    int n_vertices;
    sshape_vertex_t vertices[1024];

    int n_indices;
    u16 indices[1024];
} prim_cyl, prim_box;

static void debug_draw_lazy_init() {
    RELOAD_STATIC_RANGE(RANGE(lines));
    RELOAD_STATIC_RANGE(RANGE(points));
    RELOAD_STATIC_RANGE(RANGE(boxes));
    RELOAD_STATIC_RANGE(RANGE(cyls));
    RELOAD_STATIC_RANGE(RANGE(pipeline));
    RELOAD_STATIC_RANGE(RANGE(prim_cyl));
    RELOAD_STATIC_RANGE(RANGE(prim_box));

    static bool initialized = false;
    RELOAD_STATIC_RANGE(RANGE(initialized));

    if (initialized) { return; }
    initialized = true;

    dynlist_init(lines, &g->arena);
    dynlist_init(points, &g->arena);
    dynlist_init(boxes, &g->arena);
    dynlist_init(cyls, &g->arena);

    // copy data from render deferred renderer info
    const sg_pipeline_desc deferred_desc =
        sg_query_pipeline_desc(g_renderer->pipelines.stencil[0].level_reg);

    sg_pipeline_desc pip_desc = {
        .shader = shaders_get(SHADER_SGL),
        .color_count = deferred_desc.color_count,
        .depth = deferred_desc.depth,
    };

    memcpy(
        &pip_desc.colors,
        &deferred_desc.colors,
        sizeof(deferred_desc.colors));

    pipeline = sgl_context_make_pipeline(g->sgl_ctx, &pip_desc);

    const sshape_buffer_t
        buf_cyl =
            sshape_build_cylinder(
                &(sshape_buffer_t) {
                    .indices = { .buffer = SSHAPE_RANGE(prim_cyl.indices) },
                    .vertices = { .buffer = SSHAPE_RANGE(prim_cyl.vertices) },
                },
                &(sshape_cylinder_t) {
                    .height = 1.0f,
                    .radius = 0.5f,
                    .slices = 8,
                    .stacks = 2,
                }),
        buf_box =
            sshape_build_box(
                &(sshape_buffer_t) {
                    .indices = { .buffer = SSHAPE_RANGE(prim_box.indices) },
                    .vertices = { .buffer = SSHAPE_RANGE(prim_box.vertices) },
                },
                &(sshape_box_t) {
                    .height = 1.0f,
                    .width = 1.0f,
                    .depth = 1.0f,
                });

    prim_cyl.n_indices =
        buf_cyl.indices.data_size / sizeof(u16);
    prim_cyl.n_vertices =
        buf_cyl.vertices.data_size / sizeof(sshape_vertex_t);

    prim_box.n_indices = buf_box.indices.data_size / sizeof(u16);
    prim_box.n_vertices = buf_box.vertices.data_size / sizeof(sshape_vertex_t);
}

void debug_draw_line(const debug_draw_line_t *line) {
    debug_draw_lazy_init();
    *dynlist_push(lines) = *line;
}

void debug_draw_point(const debug_draw_point_t *point) {
    debug_draw_lazy_init();
    *dynlist_push(points) = *point;
}

void debug_draw_box(const debug_draw_box_t *box) {
    debug_draw_lazy_init();
    *dynlist_push(boxes) = *box;
}

void debug_draw_cyl(const debug_draw_cyl_t *cyl) {
    debug_draw_lazy_init();
    *dynlist_push(cyls) = *cyl;
}

void debug_draw_render(const m4 *proj, const m4 *view) {
    debug_draw_lazy_init();
    sgl_set_context(g->sgl_ctx);

    sgl_defaults();
    sgl_load_pipeline(pipeline);
    sgl_point_size(4.0f);

    const m4 view_proj = m4_mul(*proj, *view);
    sgl_load_matrix((float*) view_proj.raw);

    sgl_begin_lines();
    dynlist_each(lines, it) {
        sgl_v3f_c3f(v3_spread(it.el->a), v3_spread(it.el->color));
        sgl_v3f_c3f(v3_spread(it.el->b), v3_spread(it.el->color));
    }
    sgl_end();

    dynlist_each(cyls, it) {
        sgl_push_matrix();
        sgl_translate(it.el->p.x, it.el->p.y, it.el->p.z + (it.el->h / 2.0f));
        sgl_scale(it.el->r, it.el->r, it.el->h);
        sgl_rotate(PI_2, 1.0f, 0.0f, 0.0f);
        sgl_begin_triangles();
        for (int i = 0; i < prim_cyl.n_indices; i++) {
            const sshape_vertex_t *v =
                &prim_cyl.vertices[prim_cyl.indices[i]];
            sgl_v3f_c3f(v->x, v->y, v->z, v3_spread(it.el->color));
        }
        sgl_end();
        sgl_pop_matrix();
    }

    dynlist_each(boxes, it) {
        const v3
            center = v3_lerp(it.el->box.min, it.el->box.max, 0.5f),
            d = v3_sub(it.el->box.max, it.el->box.min);

        sgl_push_matrix();
        sgl_translate(v3_spread(center));
        sgl_scale(v3_spread(d));
        sgl_begin_triangles();
        for (int i = 0; i < prim_box.n_indices; i++) {
            const sshape_vertex_t *v =
                &prim_box.vertices[prim_box.indices[i]];
            sgl_v3f_c3f(v->x, v->y, v->z, v3_spread(it.el->color));
        }
        sgl_end();
        sgl_pop_matrix();
    }

    sgl_begin_points();
    dynlist_each(points, it) {
        sgl_v3f_c3f(v3_spread(it.el->p), v3_spread(it.el->color));
    }
    sgl_end();

    if (g->visopt & VISOPT_SGL_WIRE) {
        sg_ext_set_wireframe(true);
    }

    sgl_context_draw(g->sgl_ctx);

    if (g->visopt & VISOPT_SGL_WIRE) {
        sg_ext_set_wireframe(false);
    }
}

void debug_draw_end_frame() {
    debug_draw_lazy_init();

#define DD_ITER(list)                                                       \
    dynlist_each(list, it) {                                                \
        if (!it.el->_internal.frame) {                                      \
            it.el->_internal.frame = g->time.frame.count;                   \
        } else if (                                                         \
            g->time.frame.count - it.el->_internal.frame > it.el->frames) { \
            dynlist_remove_it(list, it);                                    \
        }                                                                   \
    }                                                                       \

    DD_ITER(lines)
    DD_ITER(points)
    DD_ITER(boxes)
    DD_ITER(cyls)

#undef DD_ITER
}
