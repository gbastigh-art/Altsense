#include "game.h"
#ifndef SOKOL_C_DO_NOT_INCLUDE_SOKOL_H
    #include "ext/sokol.h"
#endif // ifndef SOKOL_C_DO_NOT_INCLUDE_SOKOL_H
#include "util/log.h"
#include "util/assert.h"
#include "util/math.h"
#include "util/alloc.h"
#include "reloadhost.h"

static allocator_stats_t arena_stats;
RELOAD_STATIC_GLOBAL(arena_stats)

static allocator_t arena;
RELOAD_STATIC_GLOBAL(arena)

allocator_t *sokol_ext_arena() {
    // TODO: g->arena?
    if (!allocator_valid(&arena)) {
        heap_allocator_init(&arena, g_mallocator, &arena_stats);
    }

    return &arena;
}

void *sokol_ext_alloc(size_t n, void*) {
    return mem_alloc(sokol_ext_arena(), n);
}

void sokol_ext_free(void *p, void*) {
    mem_free(sokol_ext_arena(), p);
}

void sokol_ext_log(
    const char* tag,                // always "sg"
    uint32_t log_level,             // 0=panic, 1=error, 2=warning, 3=info
    uint32_t log_item_id,           // SG_LOGITEM_*
    const char* message_or_null,    // a message string, may be nullptr in release mode
    uint32_t line_nr,               // line number in sokol_gfx.h
    const char* filename_or_null,   // source filename, may be nullptr in release mode
    void* user_data) {
    _log(
        filename_or_null ? filename_or_null : "(sokol gfx)",
        line_nr,
        "",
        log_level == 3 ? "LOG" : (log_level == 2 ? "WRN" : "ERR"),
        "(TAG: %s / ID: %d) %s",
        tag,
        (int) log_item_id,
        message_or_null ? message_or_null : "(null)");
    
    if (log_level <= 2) {
        dumptrace(stderr);
        abort();
    }
}

static void get_thick_line_triangles(
        float ax,
        float ay,
        float bx,
        float by,
        float thickness,
        sgp_triangle tris[2]) {
    const v2
        normal = v2_normalize(v2_of(ay - by, bx - ax)),
        tnorm = v2_scale(normal, 0.5f * thickness),
        itnorm = v2_scale(tnorm, -1.0f);

    const v2 ps[4] = {
        v2_add(v2_of(ax, ay), tnorm),
        v2_add(v2_of(ax, ay), itnorm),
        v2_add(v2_of(bx, by), itnorm),
        v2_add(v2_of(bx, by), tnorm),
    };

    tris[0].a = (sgp_point) { v2_spread(ps[0]) };
    tris[0].b = (sgp_point) { v2_spread(ps[1]) };
    tris[0].c = (sgp_point) { v2_spread(ps[2]) };

    tris[1].a = (sgp_point) { v2_spread(ps[2]) };
    tris[1].b = (sgp_point) { v2_spread(ps[3]) };
    tris[1].c = (sgp_point) { v2_spread(ps[0]) };
}

void sgp_ext_draw_thick_line(
        float ax,
        float ay,
        float bx,
        float by,
        float thickness) {
    sgp_triangle tris[2];
    get_thick_line_triangles(ax, ay, bx, by, thickness, tris);
    sgp_draw_filled_triangles(tris, 2);
}

void sgp_ext_draw_thick_lines(
        const sgp_line *lines,
        usize count,
        float thickness) {
    DYNLIST(sgp_triangle) tris = NULL;
    dynlist_init(tris, &g->frame_arena, count * 2);
    dynlist_resize(tris, count * 2);

    for (usize i = 0; i < count; i++) {
        get_thick_line_triangles(
            lines[i].a.x, lines[i].a.y,
            lines[i].b.x, lines[i].b.y,
            thickness,
            &tris[i * 2]);
    }

    sgp_draw_filled_triangles(tris, count * 2);
}

void sgp_ext_draw_filled_box2f(box2f_t box2i) {
    sgp_draw_filled_rect(
        box2i.min.x,
        box2i.min.y,
        box2i.max.x - box2i.min.x,
        box2i.max.y - box2i.min.y);
}

void sgp_ext_draw_box2f(box2f_t box2i, float thickness) {
    sgp_ext_draw_thick_line(
        box2i.min.x, box2i.min.y, box2i.max.x, box2i.min.y, thickness);
    sgp_ext_draw_thick_line(
        box2i.max.x, box2i.min.y, box2i.max.x, box2i.max.y, thickness);
    sgp_ext_draw_thick_line(
        box2i.max.x, box2i.max.y, box2i.min.x, box2i.max.y, thickness);
    sgp_ext_draw_thick_line(
        box2i.min.x, box2i.max.y, box2i.min.x, box2i.min.y, thickness);
}

#define N_CIRCLE_POINTS 24
#define sgp_point_from_v2(_v) \
    ({ typeof(_v) __v = (_v); (sgp_point) { __v.x, __v.y }; })

static void make_circle_points(v2 p, f32 r, v2 *points, int n) {
    ASSERT(n >= 3);
    const f32 step = TAU / (n - 1);

    for (int i = 0; i < n; i++) {
        const f32 a = i * step;
        points[i] = v2_add(p, v2_scale(v2_of(fast_cos(a), fast_sin(a)), r));
    }
}

void sgp_ext_fill_circle(v2 p, f32 r) {
    v2 ps[N_CIRCLE_POINTS];
    make_circle_points(p, r, ps, N_CIRCLE_POINTS);

    sgp_triangle triangles[N_CIRCLE_POINTS];

    for (int i = 0; i < N_CIRCLE_POINTS; i++) {
        triangles[i] = (sgp_triangle) {
            .a = sgp_point_from_v2(ps[(i + 1) % N_CIRCLE_POINTS]),
            .b = sgp_point_from_v2(p),
            .c = sgp_point_from_v2(ps[i]),
        };
    }

    sgp_draw_filled_triangles(triangles, N_CIRCLE_POINTS);
}

void sgp_ext_draw_circle(v2 p, f32 r) {
    v2 ps[N_CIRCLE_POINTS];
    make_circle_points(p, r, ps, N_CIRCLE_POINTS);

    sgp_line lines[N_CIRCLE_POINTS];

    for (int i = 0; i < N_CIRCLE_POINTS; i++) {
        lines[i] = (sgp_line) {
            .a = sgp_point_from_v2(ps[i]),
            .b = sgp_point_from_v2(ps[(i + 1) % N_CIRCLE_POINTS]),
        };
    }

    sgp_draw_lines(lines, N_CIRCLE_POINTS);
}

void sgp_ext_draw_thick_circle(v2 p, f32 r, f32 thickness) {
    v2 ps[N_CIRCLE_POINTS];
    make_circle_points(p, r, ps, N_CIRCLE_POINTS);

    sgp_line lines[N_CIRCLE_POINTS];

    for (int i = 0; i < N_CIRCLE_POINTS; i++) {
        lines[i] = (sgp_line) {
            .a = sgp_point_from_v2(ps[i]),
            .b = sgp_point_from_v2(ps[(i + 1) % N_CIRCLE_POINTS]),
        };
    }

    sgp_ext_draw_thick_lines(lines, N_CIRCLE_POINTS, thickness);
}
