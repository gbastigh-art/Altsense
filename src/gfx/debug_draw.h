#pragma once

#include "util/math.h"

typedef struct { i64 frame; } debug_draw_internal_t;

typedef struct {
    v3 a, b;
    v4 color;
    int frames;
    debug_draw_internal_t _internal;
} debug_draw_line_t;

typedef struct {
    v3 p;
    v4 color;
    int frames;
    debug_draw_internal_t _internal;
} debug_draw_point_t;

typedef struct {
    box3f_t box;
    v4 color;
    int frames;
    debug_draw_internal_t _internal;
} debug_draw_box_t;

typedef struct {
    v3 p;
    float h, r;
    v4 color;
    int frames;
    debug_draw_internal_t _internal;
} debug_draw_cyl_t;

void debug_draw_line(const debug_draw_line_t *line);

void debug_draw_point(const debug_draw_point_t *point);

void debug_draw_box(const debug_draw_box_t *box);

void debug_draw_cyl(const debug_draw_cyl_t *cyl);

// do debug drawing
void debug_draw_render(const m4 *proj, const m4 *view);

// call each frame
void debug_draw_end_frame();
