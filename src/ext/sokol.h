#pragma once

#include "config.h"

#if !defined(SOKOL_METAL) && !defined(SOKOL_GLCORE)
    #error "please pick a sokol backend"
#endif

#ifndef DO_NOT_INCLUDE_SOKOL_HEADERS
    #define SOKOL_TRACE_HOOKS
    #define SOKOL_IMGUI_NO_SOKOL_APP

    #include "../../lib/sokol/sokol_gfx.h"
    #include "../../lib/sokol_gp/sokol_gp.h"
    #include "../../lib/sokol/util/sokol_shape.h"
    #include "../../lib/sokol/util/sokol_gl.h"
    #include "../../lib/sokol/util/sokol_imgui.h"
    #include "../../lib/sokol/util/sokol_gfx_imgui.h"
#endif // ifndef DO_NOT_INCLUDE_SOKOL_HEADERS

#include "util/types.h"

// used to indicate no slot in sg_bindings structs
#define SLOT_NONE -1

// DYNLIST(*) -> sg_range
#define sg_range_from_dynlist(_l)                                              \
    ((sg_range) { .size = dynlist_size_bytes((_l)), .ptr = (_l) })

// gets global sokol arena
allocator_t *sokol_ext_arena();

void *sokol_ext_alloc(size_t n, void*);

void sokol_ext_free(void *p, void*);

void sokol_ext_log(
    const char* tag,
    uint32_t log_level,
    uint32_t log_item_id,
    const char* message_or_null,
    uint32_t line_nr,
    const char* filename_or_null,
    void* user_data);

void sgp_ext_draw_thick_line(
    float ax,
    float ay,
    float bx,
    float by,
    float thickness);

typedef struct sgp_line sgp_line;
void sgp_ext_draw_thick_lines(
    const sgp_line *lines,
    usize count,
    float thickness);

typedef struct box2f box2f_t;
void sgp_ext_draw_filled_box2f(box2f_t box);
void sgp_ext_draw_box2f(box2f_t box, float thickness);

void sgp_ext_fill_circle(v2 p, f32 r);
void sgp_ext_draw_circle(v2 p, f32 r);
void sgp_ext_draw_thick_circle(v2 p, f32 r, f32 thickness);

/// BEGIN PLATFORM-SPECIFIC FUNCTIONS ///
// (these are implemented in platform_*.* files)

// query image pixels, with (potentially async!) callback with data
void sg_ext_query_image_pixels(
    sg_image _img,
    void (*callback)(sg_image, const void*, int, void*),
    void *userdata);

// update part of a buffer (NOTE: only legal once per frame!)
void sg_ext_update_buffer_partial(
    sg_buffer buffer,
    int offset,
    const sg_range *data);

// specified whether subsequent draw commands should be wireframe
void sg_ext_set_wireframe(bool wireframe);

// per-frame multi draw limit
#define SG_EXT_DRAW_MULTI_MAX 16384

// see sg_ext_draw_multi
typedef struct {
    int base_instance;
    int num_instances;

    int base_vertex;

    int base_element;
    int num_elements;
} sg_ext_indirect_command;

// draw multiple elements with a base vertex, equivalent to:
// for (sg_ext_indirect_command c in commands) {
//     base_vertex = c.base_vertex
//     base_instance = c.base_instance
//     sg_draw(c.base_element, c.num_elements, c.num_instances)
// }
void sg_ext_draw_multi(const sg_range *commands);

/// END PLATFORM-SPECIFIC FUNCTIONS ///
