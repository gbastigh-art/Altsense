#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#ifdef __OBJC__
    #include <AppKit/AppKit.h>
#endif // ifdef __OBJC__

#define RELOAD_HOST

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "../lib/cimgui/cimgui.h"

#ifndef CLANGD
    #define SOKOL_IMPL
#endif // ifndef CLANGD

#define SOKOL_METAL
#include "../lib/sokol/sokol_gfx.h"
#include "../lib/sokol/sokol_app.h"
#include "../lib/sokol/sokol_glue.h"
#include "../lib/sokol/util/sokol_imgui.h"
#include "../lib/sokol_gp/sokol_gp.h"

#define SOKOL_C_DO_NOT_INCLUDE_SOKOL_H
#include "../src/ext/sokol.c"

#define SOKOL_SHDC_IMPL
#include "sim_draw.glsl.h"
#include "sim_blit.glsl.h"

#include "util/dynlist.h"
#include "util/assert.h"
#include "util/log.h"
#include "util/time.h"
#include "util/math.h"
#include "util/rand.h"
#include "util/color.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

// comment to disable openmp usage
#define USE_OPENMP

#ifdef USE_OPENMP
    #include <omp.h>
    #define M_OPENMP_PARLLEL _Pragma("omp parallel for")
#else
    #define M_OPENMP_PARLLEL
#endif // ifdef USE_OPENMP

#define TICKS_PER_SECOND 60

#define MAX_PARTICLES 65536

// optimization opportunity: SoA instead of AoS
typedef struct {
    v2 pos, vel, pos_pred, force;
    f32 density, pressure, near_density, near_pressure;
    int cell;
} particle_t;

typedef struct { int start, count; } cell_t;

struct {
    // config
    int num_particles;
    f32 kern_radius;
    f32 boundary_thickness;
    f32 particle_display_radius;
    f32 mass;
    f32 visc;
    f32 target_density;
    f32 pressure_mult;
    f32 near_pressure_mult;
    f32 interact_strength;
    f32 interact_radius;
    v2 gravity;
    f32 scale; // px/unit
    f32 bounds_resitution;
    f32 lookahead;
    f32 color_mag;
    f32 spacing;
    f32 impulse_strength;
    f32 dt_scale;
    f32 disp_radius;
    f32 disp_scale;
    bool paused;
    bool draw_particles;

    v3 density_low_color, density_high_color;

    // computed from window size, bounds
    // includes +/- one cell on each side
    int num_cells;
    v2i cell_dims;

    // constants
    f32 poly6;
    f32 spiky_pow3;
    f32 spiky_pow3_grad;
    f32 spiky_pow2;
    f32 spiky_pow2_grad;
    f32 kern_radius_sq;

    i64 last_frame;
    i64 last_second;
    int frames;
    int fps;

    // acute force from window movement
    v2 impulse;

    v2 cursor_raw;
    v2 cursor;
    i64 sim_avg;

    rand_t rand;

    bool mouse_left, mouse_right;

    // always has g.num_particles particles
    DYNLIST(particle_t) particles;

    // data for each cell
    DYNLIST(cell_t) cells;

    // collision lines in bounds-space
    DYNLIST(line2f_t) collision_lines;

    v2i offscreen_size, last_offscreen_size;
    sg_image offscreen;
    sg_sampler sampler_nearest;
    sg_attachments offscreen_atts;
    sg_shader shader_draw, shader_blit;
    sg_pipeline draw_pip, blit_pip;
    sg_buffer gpu_particles;

    struct {
        sg_buffer ibuf, vbuf;
    } screenquad;
} g;

static void slog(
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

static void init() {
    g.rand = rand_create(0x12355);
    sg_setup(
        &(sg_desc) {
            .logger.func = slog,
            .environment = sglue_environment(),
        });
    simgui_setup(
        &(simgui_desc_t) {
            .logger.func = slog,
            .sample_count = 4,
        });
    sgp_setup(
        &(sgp_desc) {
            .color_format = sglue_environment().defaults.color_format,
            .depth_format = sglue_environment().defaults.depth_format,
            .sample_count = 4,
            .max_vertices = 65536 * 64,
            .max_commands = 65536,
        });

    // initial settings
    g.num_particles = 500;
    g.kern_radius = 0.50f;
    g.boundary_thickness = 0.0f;
    g.particle_display_radius = 0.055f;
    g.mass = 1.0f;
    g.visc = 15.0f;
    g.target_density = 35.0f;
    g.pressure_mult = 200.0f;
    g.near_pressure_mult = 100.0f;
    g.gravity = v2_of(0.0f, -700.0f);
    g.scale = 120.0f;
    g.bounds_resitution = 0.6f;
    g.lookahead = 0.008f;
    g.color_mag = 10.0f;
    g.spacing = g.kern_radius / 3.0f;
    g.impulse_strength = 50.0f;
    g.interact_strength = 2500.0f;
    g.interact_radius = 1.5f;
    g.dt_scale = 1.0f;
    g.disp_radius = 0.3f;
    g.disp_scale = 100.0f;
    g.paused = true;
    g.draw_particles = true;
    g.offscreen_size = v2i_of(128, 72);
    g.density_low_color = v3_of(0.1f, 0.1f, 0.5f);
    g.density_high_color = v3_of(0.6f, 0.8f, 1.0f);

    g.shader_draw =
        sg_make_shader(sim_draw_program_shader_desc(sg_query_backend()));
    g.shader_blit =
        sg_make_shader(sim_blit_program_shader_desc(sg_query_backend()));

    const u16 indices[] = { 0, 1, 2, 0, 2, 3 };

    const f32 vertices[] = {
        // pos       // uv
        0.0f, 0.0f,  0.0f, 1.0f,
        1.0f, 0.0f,  1.0f, 1.0f,
        1.0f, 1.0f,  1.0f, 0.0f,
        0.0f, 1.0f,  0.0f, 0.0f,
    };

    g.screenquad.ibuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_INDEXBUFFER,
                .data = SG_RANGE(indices),
            });

    g.screenquad.vbuf =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .data = SG_RANGE(vertices),
            });

    g.gpu_particles =
        sg_make_buffer(
            &(sg_buffer_desc) {
                .type = SG_BUFFERTYPE_STORAGEBUFFER,
                .usage = SG_USAGE_STREAM,
                .size = MAX_PARTICLES * sizeof(sim_draw_gpu_particle_t),
            });

    g.draw_pip = sg_make_pipeline(&(sg_pipeline_desc) {
        .shader = g.shader_draw,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT2,
                [1].format = SG_VERTEXFORMAT_FLOAT2,
            }
        },
        .depth = {
            .pixel_format = SG_PIXELFORMAT_NONE,
        },
        .colors[0] = {
            .pixel_format = SG_PIXELFORMAT_RGBA8,
            .blend = {
                .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .op_rgb = SG_BLENDOP_ADD,
                .src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .op_alpha = SG_BLENDOP_ADD,
            },
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
        .sample_count = 1,
    });

    const sg_environment env = sg_query_desc().environment;
    g.blit_pip = sg_make_pipeline(&(sg_pipeline_desc) {
        .shader = g.shader_blit,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT2,
                [1].format = SG_VERTEXFORMAT_FLOAT2,
            }
        },
        .depth = {
            .pixel_format = env.defaults.depth_format,
            .write_enabled = false,
            .compare = SG_COMPAREFUNC_ALWAYS,
        },
        .colors[0] = {
            .pixel_format = env.defaults.color_format,
            .blend = {
                .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .op_rgb = SG_BLENDOP_ADD,
                .src_factor_alpha = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .op_alpha = SG_BLENDOP_ADD,
            },
        },
        .face_winding = SG_FACEWINDING_CCW,
        .cull_mode = SG_CULLMODE_BACK,
        .sample_count = env.defaults.sample_count,
    });

    g.sampler_nearest =
        sg_make_sampler(
            &(sg_sampler_desc) {
                .min_filter = SG_FILTER_NEAREST,
                .mag_filter = SG_FILTER_NEAREST,
                .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
                .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
            });
}

M_INLINE f32 k_poly6(f32 d) {
    if (d > g.kern_radius) { return 0.0f; }
    const f32 v = (g.kern_radius_sq - (d * d));
    // const f32 poly6 = (4.0f / (PI * powf(r, 8.0f)));
    return POW3(v) * g.poly6;
}

M_INLINE f32 k_spiky_pow3(f32 d) {
    if (d > g.kern_radius) { return 0.0f; }
    const f32 v = (g.kern_radius - d);
    // const f32 spiky_pow3 = (10.0f / (PI * powf(r, 5.0f)));
    return POW3(v) * g.spiky_pow3;
}

M_INLINE f32 k_spiky_pow3_grad(f32 d) {
    if (d > g.kern_radius) { return 0.0f; }
    const f32 v = (g.kern_radius - d);
    // const f32 spiky_pow3_grad = (30.0f / (PI * powf(r, 5.0f)));
    return -v * v * g.spiky_pow3_grad;
}

M_INLINE f32 k_spiky_pow2(f32 d) {
    if (d > g.kern_radius) { return 0.0f; }
    const f32 v = (g.kern_radius - d);
    // const f32 spiky_pow2 = (6.0f / (PI * powf(r, 4.0f)));
    return POW2(v) * g.spiky_pow2;
}

M_INLINE f32 k_spiky_pow2_grad(f32 d) {
    if (d > g.kern_radius) { return 0.0f; }
    const f32 v = (g.kern_radius - d);
    // const f32 spiky_pow2_grad = (12.0f / (PI * powf(r, 4.0f)));
    return -v * g.spiky_pow2_grad;
}

#define k_density           k_spiky_pow3
#define k_density_grad      k_spiky_pow3_grad
#define k_near_density      k_spiky_pow2
#define k_near_density_grad k_spiky_pow2_grad
#define k_visc              k_poly6

static v2i pc_neighbors[] = {
    v2i_const(-1, -1),
    v2i_const(0,  -1),
    v2i_const(1,  -1),
    v2i_const(-1, 0),
    v2i_const(0,  0),
    v2i_const(1,  0),
    v2i_const(-1, 1),
    v2i_const(0,  1),
    v2i_const(1,  1),
};

// compute cell of position with specified radius (cell size)
M_INLINE v2i pc_cell_from_pos(v2 pos) {
    // offset by 1 since cell bounds include one boundary cell on each side
    return v2i_add(v2i_from_v(v2_scale(pos, 1.0f / g.kern_radius)), v2i_of(1));
}

// convert cell to its index
M_INLINE int pc_index_from_cell(v2i cell) {
    return (cell.y * g.cell_dims.x) + cell.x;
}

#define pc_each(_origin, _name, ...)                                  \
    for (int n = 0; n < 9; n++) {                                     \
        const v2i cell_pos_n = v2i_add((_origin), pc_neighbors[n]);   \
        const cell_t *cell = &g.cells[pc_index_from_cell(cell_pos_n)];\
        int m = 0;                                                    \
        particle_t *_name;                                            \
        while (m < cell->count) {                                     \
            _name = &g.particles[cell->start + m];                    \
            m++;                                                      \
            { __VA_ARGS__ }                                           \
        }                                                             \
    }                                                                 \

static int cmp_particle_cell(const void *a, const void *b, void*) {
    const particle_t *pa = a, *pb = b;
    return pa->cell - pb->cell;
}

// update particle cell table
static void pc_compute() {
    // clear indices
    for (int i = 0; i < g.num_cells; i++) {
        // INT_MAX - 1 -> no entries in cell
        g.cells[i] = (cell_t) {
            .start = INT_MAX,
            .count = 0,
        };
    }

    // compute cell for every particle
    for (int i = 0; i < g.num_particles; i++) {
        particle_t *p_i = &g.particles[i];

        const v2i cell_pos = pc_cell_from_pos(p_i->pos);
        p_i->cell = (cell_pos.y * g.cell_dims.x) + cell_pos.x;
        g.cells[p_i->cell].count++;
    }

    // sort based on cell indices
    dynlist_sort(
        g.particles,
        cmp_particle_cell,
        NULL);

    // find cell start/end indices
    {
        int i = 0;
        do {
            const particle_t *p = &g.particles[i];
            ASSERT(i == 0 || g.particles[i - 1].cell != p->cell);

            g.cells[p->cell].start = i;
            i += g.cells[p->cell].count;

            // next cell must be different
            ASSERT(i == g.num_particles || g.particles[i].cell != p->cell);
            ASSERT(g.particles[i - 1].cell == p->cell);
        } while (i < g.num_particles);
    }
}

static void predict_positions() {
    M_OPENMP_PARLLEL
    for (int i = 0; i < g.num_particles; i++) {
        particle_t *p_i = &g.particles[i];
        p_i->pos_pred = v2_add(p_i->pos, v2_scale(p_i->vel, g.lookahead));
    }
}

static void compute_density_and_pressure() {
    const f32 r2 = POW2(g.kern_radius);

    M_OPENMP_PARLLEL
    for (int i = 0; i < g.num_particles; i++) {
        particle_t *p_i = &g.particles[i];
        p_i->density = 0.0f;
        p_i->near_density = 0.0f;

        const v2i cell_org = pc_cell_from_pos(p_i->pos);

        pc_each(cell_org, p_j, {
            const v2 p_i_to_p_j = v2_sub(p_j->pos_pred, p_i->pos_pred);
            const f32 dist2 = v2_norm2(p_i_to_p_j);
            if (dist2 < r2) {
                const f32 dist = sqrtf(dist2);
                p_i->density += g.mass * k_density(dist);
                p_i->near_density += g.mass * k_near_density(dist);
            }
        })

        p_i->pressure =
            g.pressure_mult * (p_i->density - g.target_density);
        p_i->near_pressure =
            g.near_pressure_mult * (p_i->near_density - g.target_density);
    }
}

static void compute_forces(
    v2 interact_pos,
    f32 interact_strength,
    f32 interact_radius) {
    const f32 r2 = POW2(g.kern_radius);

    // optimization opportunity (but not really because you shouldn't do it)
    // interactions can be halved if each force is applied symmetrically - this
    // seems simple but it would really complicate the parallelism
    M_OPENMP_PARLLEL
    for (int i = 0; i < g.num_particles; i++) {
        particle_t *p_i = &g.particles[i];
        v2
            f_interact = v2_of(0.0f),
            f_pressure = v2_of(0.0f),
            f_viscosity = v2_of(0.0f),
            f_gravity = g.gravity;

        const v2i cell_org = pc_cell_from_pos(p_i->pos);
        pc_each(cell_org, p_j, {
            if (p_i == p_j) { continue; }

            const v2 p_i_to_p_j = v2_sub(p_j->pos_pred, p_i->pos_pred);
            const f32 dist2 = v2_norm2(p_i_to_p_j);
            if (dist2 > r2) { continue; }

            const f32 dist = sqrtf(dist2);
            const v2 p_i_to_p_j_normalized = v2_scale(p_i_to_p_j, 1.0f / dist);

            // base pressure
            f_pressure =
                v2_add(
                    f_pressure,
                    v2_scale(
                        p_i_to_p_j_normalized,
                        g.mass
                            * ((p_i->pressure + p_j->pressure) / 2.0f)
                            * (1.0f / p_j->density)
                            * k_density_grad(dist)));

            // near pressure
            f_pressure =
                v2_add(
                    f_pressure,
                    v2_scale(
                        p_i_to_p_j_normalized,
                        g.mass
                            * ((p_i->near_pressure + p_j->near_pressure) / 2.0f)
                            * (1.0f / p_j->near_density)
                            * k_near_density_grad(dist)));

            f_viscosity =
                v2_add(
                    f_viscosity,
                    v2_scale(
                        v2_sub(p_j->vel, p_i->vel),
                        g.visc
                            * g.mass
                            * k_visc(dist)));
        })

        if (fabsf(interact_strength) > 0.00001f) {
            const v2 p_i_to_interact = v2_sub(interact_pos, p_i->pos);
            const f32 dist2 = v2_norm2(p_i_to_interact);
            if (dist2 < interact_radius * interact_radius) {
                const f32 dist = sqrtf(dist2);
                const v2 dir =
                    dist < 0.00001f ?
                        v2_of(0)
                        : v2_scale(p_i_to_interact, 1.0f / dist);

                f_interact =
                    v2_scale(
                        v2_sub(
                            v2_scale(dir, interact_strength),
                            v2_scale(p_i->vel, 1.0f / 60.0f)),
                        1.0f - (dist / interact_radius));
            }
        }

        p_i->force =
            v2_add(
                v2_add(f_gravity, f_interact),
                v2_add(f_pressure, f_viscosity));

        // impulse
        p_i->force = v2_add(p_i->force, v2_scale(g.impulse, g.impulse_strength));
    }
}

static v2 get_bounds() {
    const v2i window_size = v2i_of(sapp_width(), sapp_height());
    return
        v2_of(
            window_size.x / g.scale,
            window_size.y / g.scale);
}

static void integrate(f32 dt) {
    const v2 bounds = get_bounds();

    M_OPENMP_PARLLEL
    for (int i = 0; i < g.num_particles; i++) {
        particle_t *p = &g.particles[i];

        // accelerate by F = ma (m is density)
        p->vel = v2_add(p->vel, v2_scale(p->force, dt / p->density));

        const f32 r = g.particle_display_radius;
        dynlist_each(g.collision_lines, it) {
            v2 resolved;
            f32 t_circle, t_segment;
            if (!sweep_circle_line_segment(
                    p->pos,
                    r,
                    v2_scale(p->vel, dt),
                    it.el->a,
                    it.el->b,
                    &t_circle,
                    &t_segment,
                    &resolved)) {
                continue;
            }

            p->pos = resolved;

            // get normal of line pointing towards particle
            v2 normal = line_right_normal(it.el->a, it.el->b);
            if (point_side(p->pos, it.el->a, it.el->b) > 0) {
                normal = v2_scale(normal, -1);
            }

            const v2 tangent = v2_rotate(normal, PI_2);

            // project velocity along side
            const v2
                v_towards_wall = v2_proj(v2_normalize(p->vel), normal),
                restitution =
                    v2_scale(
                        v2_scale(
                            v_towards_wall,
                            g.bounds_resitution),
                        -v2_norm(p->vel));

            // projected velocity onto tangent, add resitution
            p->vel =
                v2_add(
                    v2_proj(p->vel, tangent),
                    v2_add(
                        restitution,
                        v2_scale(normal, 0.0f))); // TODO
        }

        // move by velocity
        p->pos = v2_add(p->pos, v2_scale(p->vel, dt));

        // resolve collisions
        for (int axis = 0; axis < 2; axis++) {
            if (p->pos.raw[axis] < 0.0f) {
                p->pos.raw[axis] = g.boundary_thickness;
                p->vel.raw[axis] *= -g.bounds_resitution;
            }

            if (p->pos.raw[axis] > bounds.raw[axis] - g.boundary_thickness) {
                p->pos.raw[axis] = bounds.raw[axis] - g.boundary_thickness;
                p->vel.raw[axis] *= -g.bounds_resitution;
            }

            p->pos.raw[axis] =
                clamp(
                    p->pos.raw[axis],
                    g.boundary_thickness,
                    bounds.raw[axis] - g.boundary_thickness);
        } 
    }
}

static void deinit() {
    sgp_shutdown();
    sg_shutdown();
    simgui_shutdown();
}

static void sim(f32 dt) {
    // dts out of this range will mess things up
    dt = clamp(dt, 0.00001f, 1.0f / 60.0f);

    const v2 bounds = get_bounds();

    // ensure bounded particle positions
    M_OPENMP_PARLLEL
    for (int i = 0; i < g.num_particles; i++) {
        particle_t *p = &g.particles[i];

        for (int axis = 0; axis < 2; axis++) {
            if (p->pos.raw[axis] < 0.0f) {
                p->pos.raw[axis] = g.boundary_thickness;
                p->vel.raw[axis] *= -g.bounds_resitution;
            }

            if (p->pos.raw[axis] > bounds.raw[axis] - g.boundary_thickness) {
                p->pos.raw[axis] = bounds.raw[axis] - g.boundary_thickness;
                p->vel.raw[axis] *= -g.bounds_resitution;
            }

            p->pos.raw[axis] =
                clamp(
                    p->pos.raw[axis],
                    g.boundary_thickness,
                    bounds.raw[axis] - g.boundary_thickness);
        }
    }

    predict_positions();
    pc_compute();

    if (g.paused) { return; }

    compute_density_and_pressure();

    f32 interact_strength = 0.0f;
    if (!igGetIO()->WantCaptureMouse) {
        if (g.mouse_left) {
            interact_strength = +g.interact_strength;
        } else if (g.mouse_right) {
            interact_strength = -g.interact_strength;
        }
    }

    compute_forces(g.cursor, interact_strength, g.interact_radius);
    integrate(dt * g.dt_scale);
}

static void reset_square() {
    v2i size_particles;
    size_particles.y = sqrtf(g.num_particles);
    size_particles.x = g.num_particles / size_particles.y;

    const v2 bounds = get_bounds();
    const v2 size = v2_scale(v2_from_i(size_particles), g.spacing);
    const v2 offset = v2_divs(v2_sub(bounds, size), 2.0f);

    while (dynlist_size(g.particles) < g.num_particles) {
        const int i = dynlist_size(g.particles);
        *dynlist_push(g.particles) = (particle_t) {
            .pos =
                v2_add(
                    offset,
                    v2_of(
                        (i % size_particles.x) * g.spacing,
                        (i / size_particles.x) * g.spacing)), // NOLINT
        };
    }
}

static void render() {
    if (!g.draw_particles) { return; }

    const v2 bounds = get_bounds();

    sgp_viewport(0, 0, sapp_width(), sapp_height());
    sgp_project(0, bounds.x, 0, bounds.y);

    // draw collision lines
    dynlist_each(g.collision_lines, it) {
        line2f_t l = *it.el;
        l.a.y = bounds.y - l.a.y;
        l.b.y = bounds.y - l.b.y;

        sgp_set_color(0.0f, 0.0f, 0.0f, 0.8f);
        sgp_ext_draw_thick_line(
            v2_spread(l.a),
            v2_spread(l.b),
            g.particle_display_radius / 2.0f);

        sgp_set_color(1.0f, 0.5f, 0.5f, 0.8f);
        sgp_ext_fill_circle(l.a, g.particle_display_radius);

        sgp_set_color(0.5f, 1.0f, 0.5f, 0.8f);
        sgp_ext_fill_circle(l.b, g.particle_display_radius);
    }

    const v3
        hsv_slow = color_rgb_to_hsv(v3_of(0.1f, 0.1f, 0.6f)),
        hsv_fast = color_rgb_to_hsv(v3_of(1.0f, 0.6f, 0.3f));

    for (int i = 0; i < g.num_particles; i++) {
        particle_t *p = &g.particles[i];

        const v3 color =
            color_hsv_to_rgb(
                v3_lerp(hsv_slow, hsv_fast, v2_norm(p->vel) / g.color_mag));
        sgp_set_color(v3_spread(color), 1.0f);
        sgp_ext_fill_circle(
            v2_of(
                p->pos.x,
                bounds.y - p->pos.y),
            g.particle_display_radius);
    }

    sgp_set_color(1.0f, 0.0f, 1.0f, 0.4f);
    sgp_ext_draw_circle(
        v2_of(g.cursor.x, bounds.y - g.cursor.y),
        g.interact_radius);
}

static void frame() {
    const v2 bounds = get_bounds();

    const i64 now = time_ns();
    if (now - g.last_second > NS_PER_SECOND) {
        g.last_second = now;
        g.fps = g.frames;
        g.frames = 0;
        LOG(
            "fps: %d | sim: %.3f ms",
            g.fps,
            ns_to_ms(g.sim_avg));
    }

    g.frames++;

    if (!g.collision_lines) {
        dynlist_init(g.collision_lines, g_mallocator);
    }

    if (!v2i_eqv(g.offscreen_size, g.last_offscreen_size)) {
        g.last_offscreen_size = g.offscreen_size;

        if (g.offscreen.id) { sg_destroy_image(g.offscreen); }
        g.offscreen =
            sg_make_image(
                &(sg_image_desc) {
                    .render_target = true,
                    .width = g.offscreen_size.x,
                    .height = g.offscreen_size.y,
                    .pixel_format = SG_PIXELFORMAT_RGBA8,
                    .sample_count = 1,
                });

        if (g.offscreen_atts.id) { sg_destroy_attachments(g.offscreen_atts); }
        g.offscreen_atts =
            sg_make_attachments(
                &(sg_attachments_desc) {
                    .colors[0].image = g.offscreen,
                });
    }

    simgui_new_frame(
        &(simgui_frame_desc_t) {
            .width = sapp_width(),
            .height = sapp_height(),
            .delta_time = ns_to_secs(now - g.last_frame),
            .dpi_scale = sapp_dpi_scale(),
        }); 

    const f32 old_kern_radius = g.kern_radius;

    igBegin("window", NULL, 0);
    {
        igInputInt("num_particles", &g.num_particles, 10, 100, 0);
        g.num_particles = clamp(g.num_particles, 1, MAX_PARTICLES);
        igInputFloat("kern_radius", &g.kern_radius, 0.01f, 0.1f, "%.5f", 0);
        g.kern_radius = max(g.kern_radius, 0.001f);
        igInputFloat("boundary_thickness", &g.boundary_thickness, 0.01f, 0.1f, "%.5f", 0);
        g.boundary_thickness = max(g.boundary_thickness, 0.001f);
        igInputFloat("display_radius", &g.particle_display_radius, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("mass", &g.mass, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("visc", &g.visc, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("target_density", &g.target_density, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("pressure_mult", &g.pressure_mult, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("near_pressure_mult", &g.near_pressure_mult, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("gravity.x", &g.gravity.x, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("gravity.y", &g.gravity.y, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("scale", &g.scale, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("restitution", &g.bounds_resitution, 0.1f, 0.1f, "%.5f", 0);
        igInputFloat("lookahead", &g.lookahead, 0.01f, 0.01f, "%.5f", 0);
        igInputFloat("color_mag", &g.color_mag, 0.01f, 0.01f, "%.5f", 0);
        igInputFloat("spacing", &g.spacing, 0.01f, 0.1f, "%.5f", 0);
        igInputFloat("impulse_strength", &g.impulse_strength, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("interact_strength", &g.interact_strength, 1.0f, 5.0f, "%.5f", 0);
        igInputFloat("interact_radius", &g.interact_radius, 0.1f, 0.25f, "%.5f", 0);
        igInputFloat("dt_scale", &g.dt_scale, 0.1f, 0.25f, "%.5f", 0);
        igInputFloat("disp_radius", &g.disp_radius, 0.1f, 0.25f, "%.5f", 0);
        igInputFloat("disp_scale", &g.disp_scale, 0.1f, 0.25f, "%.5f", 0);
        igColorEdit3("density_low_color", &g.density_low_color.raw[0], 0);
        igColorEdit3("density_high_color", &g.density_high_color.raw[0], 0);

        igInputInt("offscreen_size.x", &g.offscreen_size.x, 0, 0, 0);
        igInputInt("offscreen_size.y", &g.offscreen_size.y, 0, 0, 0);
        g.offscreen_size = v2i_maxv(g.offscreen_size, v2i_of(32));

        igCheckbox("draw_particles", &g.draw_particles);

        if (igButton("reset", (ImVec2) {})) {
            dynlist_resize_no_contract(g.particles, 0);
            reset_square();
        }

        igSameLine(0, -1);
        if (igButton(g.paused ? "unpause" : "pause", (ImVec2) {})) {
            g.paused = !g.paused;
        }
    }
    igEnd();

    if (g.poly6 == 0.0f
        || fabsf(g.kern_radius - old_kern_radius) > 0.00001f) {
        const f32 r = g.kern_radius;
        g.kern_radius_sq = r * r;
        g.poly6 = (4.0f / (PI * powf(r, 8.0f)));
        g.spiky_pow3 = (10.0f / (PI * powf(r, 5.0f)));
        g.spiky_pow3_grad = (30.0f / (PI * powf(r, 5.0f)));
        g.spiky_pow2 = (6.0f / (PI * powf(r, 4.0f)));
        g.spiky_pow2_grad = (12.0f / (PI * powf(r, 4.0f)));
    }

    if (!g.particles) {
        dynlist_init(g.particles, g_mallocator);
        reset_square();
    }

    dynlist_each(g.particles, it) {
        if (!v2_isvalid(it.el->pos)
            || !v2_isvalid(it.el->vel)
            || !v2_isvalid(it.el->force)
            || isnan(it.el->density)
            || isinf(it.el->density)
            || isnan(it.el->pressure)
            || isinf(it.el->pressure)) {
            if (!v2_isvalid(it.el->pos))   { LOG("bad pos");      }
            if (!v2_isvalid(it.el->vel))   { LOG("bad vel");      }
            if (!v2_isvalid(it.el->force)) { LOG("bad force");    }
            if (isnan(it.el->density))     { LOG("bad density");  }
            if (isinf(it.el->density))     { LOG("bad density");  }
            if (isnan(it.el->pressure))    { LOG("bad pressure"); }
            if (isinf(it.el->pressure))    { LOG("bad pressure"); }
            dynlist_remove_it(g.particles, it);
        }
    }

    while (dynlist_size(g.particles) < g.num_particles) {
        *dynlist_push(g.particles) = (particle_t) {
            .pos =
                rand_v2(
                    &g.rand,
                    v2_of(g.kern_radius),
                    v2_sub(bounds, v2_of(g.kern_radius))),
        };
    }

    while (dynlist_size(g.particles) > g.num_particles) {
        dynlist_remove(
            g.particles,
            rand_n(&g.rand, 0, dynlist_size(g.particles) - 1));
    }

    ASSERT(
        g.num_particles == dynlist_size(g.particles), "%d, %d",
        g.num_particles,
        dynlist_size(g.particles));

    // add +/- one cell on each boundary
    g.cell_dims =
        v2i_add(
            v2i_from_v(
                v2_ceil(v2_divs(bounds, g.kern_radius))),
            v2i_of(2));
    g.num_cells = g.cell_dims.x * g.cell_dims.y;

    // ensure sp_offsets can contain all cells
    if (!g.cells) {
        dynlist_init(g.cells, g_mallocator);
    }

    dynlist_resize(g.cells, g.num_cells);

#ifdef __OBJC__
    NSWindow *win = sapp_macos_get_window();
    CGPoint org_point = win.frame.origin;
    const v2 org = v2_of(org_point.x, org_point.y);
    static v2 last_org;
    if (g.last_second != 0) {
        g.impulse = v2_sub(org, last_org);
    }
    last_org = org;
#endif // ifdef __OBJC__

    const i64 sim_start = time_ns();
    sim(min(ns_to_secs(now - g.last_frame), 0.25f));
    g.sim_avg =
        ((1.0f / 60.0f) * (time_ns() - sim_start))
            + ((59.0f / 60.0f) * g.sim_avg);

    DYNLIST(sim_draw_gpu_particle_t) gpu_particles =
        dynlist_create(
            sim_draw_gpu_particle_t,
            g_mallocator,
            g.num_particles);
    for (int i = 0; i < g.num_particles; i++) {
        *dynlist_push(gpu_particles) = (sim_draw_gpu_particle_t) {
            .pos = g.particles[i].pos,
            .vel = g.particles[i].vel,
            .density = g.particles[i].density,
        };
    }
    sg_update_buffer(
        g.gpu_particles,
        &(sg_range) {
            .ptr = gpu_particles,
            .size = dynlist_size_bytes(gpu_particles),
        });
    dynlist_destroy(gpu_particles);

    // offscreen pass
    sg_begin_pass(
        &(sg_pass) {
            .action.colors[0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f, },
            },
            .attachments = g.offscreen_atts,
        });
    {
        sg_apply_pipeline(g.draw_pip);
        const sim_draw_vs_params_t vs_params = {
            .model = m4_identity(),
            .view = m4_identity(),
            .proj = cam_ortho(0.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f),
        };
        sg_apply_uniforms(
            SG_SHADERSTAGE_VS,
            SLOT_sim_draw_vs_params,
            &SG_RANGE(vs_params));
        const sim_draw_fs_params_t fs_params = {
            .n_particles = g.num_particles,
            .kern_radius = g.kern_radius,
            .bounds = get_bounds(),
            .disp_scale = g.disp_scale,
            .disp_radius = g.disp_radius,
            .density_low_color = g.density_low_color,
            .density_high_color = g.density_high_color,
        };
        sg_apply_uniforms(
            SG_SHADERSTAGE_FS,
            SLOT_sim_draw_fs_params,
            &SG_RANGE(fs_params));
        sg_apply_bindings(
            &(sg_bindings) {
                .index_buffer = g.screenquad.ibuf,
                .vertex_buffers[0] = g.screenquad.vbuf,
                .fs.storage_buffers[SLOT_sim_draw_particle_buffer] = g.gpu_particles,
            });
        sg_draw(0, 6, 1);
    }
    sg_end_pass();

    sg_begin_pass(
        &(sg_pass) {
            .action.colors[0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f },
            },
            .swapchain = sglue_swapchain(),
        });
    {
        sg_apply_pipeline(g.blit_pip);
        const sim_blit_vs_params_t vs_params = {
            .model = m4_identity(),
            .view = m4_identity(),
            .proj = cam_ortho(0.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f),
        };
        sg_apply_uniforms(
            SG_SHADERSTAGE_VS,
            SLOT_sim_blit_vs_params,
            &SG_RANGE(vs_params));
        sg_apply_bindings(
            &(sg_bindings) {
                .fs.samplers[SLOT_sim_blit_smp] = g.sampler_nearest,
                .fs.images[SLOT_sim_blit_tex] = g.offscreen,
                .index_buffer = g.screenquad.ibuf,
                .vertex_buffers[0] = g.screenquad.vbuf,
            });
        sg_draw(0, 6, 1);

        sgp_begin(sapp_width(), sapp_height());
        render();
        sgp_flush();
        sgp_end();
        simgui_render();
    }
    sg_end_pass();
    sg_commit();

    g.impulse = v2_of(0);
    g.last_frame = now;
}

static void on_event(const sapp_event *ev) {
    simgui_handle_event(ev);

    const v2 bounds = v2_of(sapp_width(), sapp_height());
    switch (ev->type) {
    case SAPP_EVENTTYPE_QUIT_REQUESTED:
        sapp_request_quit();
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        g.cursor_raw.x = ev->mouse_x;
        g.cursor_raw.y = ev->mouse_y;
        g.cursor_raw = v2_clampv(g.cursor_raw, v2_of(0), bounds);
        g.cursor_raw.y = bounds.y - g.cursor_raw.y - 1;
        g.cursor = v2_mul(v2_div(get_bounds(), bounds), g.cursor_raw);
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
    case SAPP_EVENTTYPE_MOUSE_DOWN: {
        const bool val = ev->type == SAPP_EVENTTYPE_MOUSE_DOWN;
        switch (ev->mouse_button) {
        case SAPP_MOUSEBUTTON_LEFT:
            g.mouse_left = val;
            break;
        case SAPP_MOUSEBUTTON_RIGHT:
            g.mouse_right = val;
            break;
        default:
        }
    } break;
    default:
    }
}

sapp_desc sokol_main(int argc, char *argv[]) {
    trig_init();
    return (sapp_desc) {
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = deinit,
        .event_cb = on_event,
        .width = 1280,
        .height = 720,
        .window_title = "sim",
        .logger.func = slog,
        .sample_count = 4,
    };
}
