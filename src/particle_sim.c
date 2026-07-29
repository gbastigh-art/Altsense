#include "particle_sim.h"
#include "cam.h"
#include "gfx/model.h"
#include "gfx/renderer.h"
#include "util/assert.h"
#include "util/dynlist.h"
#include "util/rand.h"
#include "util/time.h"
#include "ext/cimgui.h"
#include "reloadhost.h"
#include "game.h"

#define MASS 1.0f
#define COLLISION_EPS 0.001f
#define PRESSURE_EPS  0.00000001f

// particle simulation data
typedef struct particle_sim_internal {
    // sim-local arena
    allocator_t arena;

    // locked when simulating (sim thread) or updating (main thread)
    mtx_t lock;

    // thread simulation runs on
    thrd_t thread;

    // for managing reloadhost state
    atomic_bool pause_requested, is_paused;

    // thread look function
    void (*loop_fn)(struct particle_sim_internal*);

    // if true, sim is running
    atomic_bool running;

    // if true, thread should quit
    atomic_bool quit_requested;

    // description
    particle_sim_desc_t desc;

    i64 last_step;
    i64 last_second;
    int steps;
    int sps;

    // total number of particles
    int num_particles;

    // computed from bounds
    // includes +/- one cell on each side
    int num_cells;
    v2i cell_dims;

    // computed constants
    f32 poly6;
    f32 spiky_pow3;
    f32 spiky_pow3_grad;
    f32 spiky_pow2;
    f32 spiky_pow2_grad;
    f32 kern_radius_sq;

    // left/right targets
    int n_target_particles[2];

    // particle data, synced at simulation step beginning/end
    DYNLIST(sim_particle_t) particles;

    struct {
        DYNLIST(v2) pos, vel, pos_pred, force;
        DYNLIST(f32) density, pressure, near_density, near_pressure;
        DYNLIST(int) cell;
    } p;

    DYNLIST(sim_cell_t) cells;

    struct {
        // hash of something identifying this model, i.e. name + index group
        // name
        hash_t hash;

        // model vertices (local)
        DYNLIST(v3) vertices;

        // model vertices (local) pruned to some distance threshold
        DYNLIST(v3) vertices_pruned;

        // "hash" when vertices_pruned was last computed
        hash_t last_pruned_compute_hash;

        // transformation matrix
        m4 transform;

        // hull lines
        DYNLIST(line2f_t) lines;
    } hulls[2];

    // camera/render info
    struct {
        m4 view_proj;
        v2i target_size;
    } cam;

    // g->cam.view_proj
    m4 view_proj;

    // particles sorted by hand
    // sorted by y-value at simulation end, pointers valid when not simulating
    DYNLIST(sim_particle_t) hand_particles[2];

    // random, used for fixing particles which are on top of each other
    rand_t rand;

    // simulation stats
    particle_sim_stats_t stats;

    // if true, sim resets on next step
    bool reset;
} particle_sim_internal_t;

#if defined(_OPENMP)
    #include <omp.h>
    #define SIM_OPENMP_PARALLEL_FOR _Pragma("omp parallel for")
#else
    #define SIM_OPENMP_PARALLEL_FOR
#endif

RELOAD_VISIBLE void thread_loop(particle_sim_internal_t*);
static int thread_entry(void*);
static void sim_step(particle_sim_internal_t *ps);

void particle_sim_init(
        particle_sim_t *ps,
        allocator_t *allocator,
        const particle_sim_desc_t *desc) {
    *ps = (particle_sim_t) { .desc = *desc, };
    heap_allocator_init(&ps->arena, allocator, NULL);

    dynlist_init(ps->hulls[0], &ps->arena);
    dynlist_init(ps->hulls[1], &ps->arena);
    dynlist_init(ps->vertices[0], &ps->arena);
    dynlist_init(ps->vertices[1], &ps->arena);
    dynlist_init(ps->particles, &ps->arena);
    dynlist_init(ps->cells, &ps->arena);

    particle_sim_internal_t *psi =
        mem_alloc(&ps->arena, sizeof(*psi));
    ps->internal = psi;

    *psi = (particle_sim_internal_t) {
        .running = false,
        .loop_fn = thread_loop,
        .desc = *desc,
    };

    RELOAD_FUNCPTR(&psi->loop_fn);

    // parent on malloc so that there are no threading issues
    heap_allocator_init(&psi->arena, g_mallocator, NULL);
    psi->rand = rand_create(0x12345);
    dynlist_init(psi->particles, &psi->arena);
    dynlist_init(psi->cells, &psi->arena);
    for (int i = 0; i < ARRLEN(psi->hulls); i++) {
        dynlist_init(psi->hulls[i].vertices, &psi->arena);
        dynlist_init(psi->hulls[i].vertices_pruned, &psi->arena);
        dynlist_init(psi->hulls[i].lines, &psi->arena);
    }
    dynlist_init(psi->hand_particles[0], &psi->arena);
    dynlist_init(psi->hand_particles[1], &psi->arena);

    dynlist_init(psi->p.pos, &psi->arena);
    dynlist_init(psi->p.vel, &psi->arena);
    dynlist_init(psi->p.pos_pred, &psi->arena);
    dynlist_init(psi->p.force, &psi->arena);
    dynlist_init(psi->p.density, &psi->arena);
    dynlist_init(psi->p.pressure, &psi->arena);
    dynlist_init(psi->p.near_density, &psi->arena);
    dynlist_init(psi->p.near_pressure, &psi->arena);
    dynlist_init(psi->p.cell, &psi->arena);

    ASSERT(
        mtx_init(&ps->internal->lock, mtx_recursive) == thrd_success);
    ASSERT(
        thrd_create(&psi->thread, thread_entry, psi) == thrd_success);
}

void particle_sim_end_frame(particle_sim_t *ps) {
    // update running state according to game state
    // TODO: psim on main menu?
    ps->internal->running = (g->mode == GAMEMODE_GAME);

    if (!ps->internal->running) {
        // if not running, update nothing
        return;
    }

    const i64 start_ns = time_ns();

    // copy into internal sim
    particle_sim_internal_t *psi = ps->internal;

    // TODO: if we've gone >= N ms without updating, lock and force update

    if (mtx_trylock(&psi->lock) == thrd_success) {
        if (ps->reset) {
            ps->reset = false;
            psi->reset = true;
        }

        // copy desc from sim base -> io
        psi->desc = ps->desc;

        // copy simulation data
        dynlist_copy_from(ps->particles, psi->particles);
        dynlist_copy_from(ps->cells, psi->cells);
        ps->cell_dims = psi->cell_dims;

        psi->cam.view_proj = g->cam.view_proj;
        psi->cam.target_size = g->target_size;

        for (int i = 0; i < 2; i++) {
            // copy target particles
            psi->n_target_particles[i] = ps->n_target_particles[i];

            dynlist_copy_from(ps->hulls[i], psi->hulls[i].lines);
            dynlist_copy_from(ps->vertices[i], psi->hulls[i].vertices_pruned);

            // TODO: use index groups
            // copy hull data
            if (str_is_empty(ps->hand_models[i].name)) {
                continue;
            }

            const model_data_t *model =
                model_atlas_lookup(ps->hand_models[i].name);
            if (!model) {
                continue;
            }
            
            hash_t hash = 0x12345;
            hash = hash_add_str(hash, ps->hand_models[i].name);
            hash = hash_add_str(hash, ps->hand_models[i].index_group);
            psi->hulls[i].hash = hash;

            psi->hulls[i].transform = ps->hand_models[i].transform;

            dynlist_resize(
                psi->hulls[i].vertices,
                dynlist_size(model->vertices));

            dynlist_each(model->vertices, it) {
                psi->hulls[i].vertices[it.i] = it.el->pos;
            }
        }

        ps->stats = psi->stats;

        mtx_unlock(&psi->lock);
    }

    g->debug.psim_lock_ms = ns_to_ms(time_ns() - start_ns);
}

void particle_sim_destroy(particle_sim_t *ps) {
    ps->internal->quit_requested = true;

    ASSERT(thrd_join(ps->internal->thread, NULL) == thrd_success);
    mtx_destroy(&ps->internal->lock);
    heap_allocator_destroy(&ps->internal->arena);
    heap_allocator_destroy(&ps->arena);
    *ps = (particle_sim_t) { 0 };
}

bool particle_sim_desc_imgui(particle_sim_desc_t *desc) {
    bool change = false;
    change |= igInputFloat2( "bounds", desc->bounds.raw, "%.5f", 0);
    desc->bounds.x = max(desc->bounds.x, 0.001f);
    desc->bounds.y = max(desc->bounds.y, 0.001f);
    change |= igInputFloat2("render_offset", desc->render_offset.raw, "%.5f", 0);
    change |= igInputFloat(
        "kern_radius", &desc->kern_radius, 0.01f, 0.1f, "%.5f", 0);
    desc->kern_radius = max(desc->kern_radius, 0.001f);
    change |= igInputFloat(
        "collision_radius", &desc->collision_radius, 0.01f, 0.1f, "%.5f", 0);
    desc->collision_radius = max(desc->collision_radius, 0.001f);
    change |= igInputFloat(
        "boundary_thickness", &desc->boundary_thickness, 0.01f, 0.1f, "%.5f", 0);
    desc->boundary_thickness = max(desc->boundary_thickness, 0.001f);
    change |= igInputFloat(
        "visc", &desc->visc, 1.0f, 5.0f, "%.5f", 0);
    desc->visc = max(desc->visc, 0.0f);
    change |= igInputFloat(
        "target_density", &desc->target_density, 1.0f, 5.0f, "%.5f", 0);
    desc->target_density = max(desc->target_density, 0.0f);
    change |= igInputFloat(
        "pressure_mult", &desc->pressure_mult, 1.0f, 5.0f, "%.5f", 0);
    desc->pressure_mult = max(desc->pressure_mult, 0.0f);
    change |= igInputFloat(
        "near_pressure_mult", &desc->near_pressure_mult, 1.0f, 5.0f, "%.5f", 0);
    desc->near_pressure_mult = max(desc->near_pressure_mult, 0.0f);
    change |= igInputFloat(
        "boundary_force", &desc->boundary_force, 1.0f, 5.0f, "%.5f", 0);
    desc->boundary_force = max(desc->boundary_force, 0.0f);
    change |= igInputFloat(
        "boundary_force_dist", &desc->boundary_force_dist, 1.0f, 5.0f, "%.5f", 0);
    desc->boundary_force_dist = max(desc->boundary_force_dist, 0.0f);
    change |= igInputFloat(
        "gravity.x", &desc->gravity.x, 1.0f, 5.0f, "%.5f", 0);
    change |= igInputFloat(
        "gravity.y", &desc->gravity.y, 1.0f, 5.0f, "%.5f", 0);
    change |= igInputFloat(
        "restitution", &desc->bounds_restitution, 0.1f, 0.1f, "%.5f", 0);
    desc->bounds_restitution = max(desc->bounds_restitution, 0.0f);
    change |= igInputFloat(
        "lookahead", &desc->lookahead, 0.01f, 0.01f, "%.5f", 0);
    desc->lookahead = max(desc->lookahead, 0.001f);
    change |= igInputFloat(
        "dt_scale", &desc->dt_scale, 0.1f, 0.25f, "%.5f", 0);
    change |= igInputFloat(
        "dt_target", &desc->dt_target, 0.1f, 0.25f, "%.5f", 0);
    return change;
}

static int thread_entry(void *userdata) {
    particle_sim_internal_t *psi = userdata;
    RELOAD_THREAD(psi->thread, &psi->pause_requested, &psi->is_paused);

    while (!psi->quit_requested) {
        if (psi->pause_requested) {
            psi->is_paused = true;
            thrd_sleep(
                &(struct timespec) { .tv_nsec = secs_to_ns(0.05f), },
                NULL);
        } else {
            psi->is_paused = false;
            psi->loop_fn(psi);
        }
    }

    RELOAD_DELETE_THREAD(psi->thread);
    return 0;
}

RELOAD_VISIBLE void thread_loop(particle_sim_internal_t *ps) {
    // nanoseconds to sleep
    i64 sleep_ns = 0;

    if (ps->running) {
        // ensure reasonable dt target to ensure that sleep time is reasonable
        const f32 dt_target =
            clamp(
                ps->desc.dt_target,
                1.0f / 120.0f,
                1.0f / 20.0f);

        // run simulation loop at requested timestep
        const i64 now = time_ns();
        sim_step(ps);
        sleep_ns =
            max(
                secs_to_ns(dt_target) - (time_ns() - now),
                0);
    } else {
        // busy wait...
        sleep_ns = secs_to_ns(0.05f);
    }

    if (sleep_ns > 0) {
        thrd_sleep(&(struct timespec) { .tv_nsec = sleep_ns, }, NULL);
    }
}

M_INLINE f32 k_poly6(const particle_sim_internal_t *ps, f32 d) {
    if (d > ps->desc.kern_radius) { return 0.0f; }
    const f32 v = (ps->kern_radius_sq - (d * d));
    // const f32 poly6 = (4.0f / (PI * powf(r, 8.0f)));
    return POW3(v) * ps->poly6;
}

M_INLINE f32 k_spiky_pow3(const particle_sim_internal_t *ps, f32 d) {
    if (d > ps->desc.kern_radius) { return 0.0f; }
    const f32 v = (ps->desc.kern_radius - d);
    // const f32 spiky_pow3 = (10.0f / (PI * powf(r, 5.0f)));
    return POW3(v) * ps->spiky_pow3;
}

M_INLINE f32 k_spiky_pow3_grad(const particle_sim_internal_t *ps, f32 d) {
    if (d > ps->desc.kern_radius) { return 0.0f; }
    const f32 v = (ps->desc.kern_radius - d);
    // const f32 spiky_pow3_grad = (30.0f / (PI * powf(r, 5.0f)));
    return -v * v * ps->spiky_pow3_grad;
}

M_INLINE f32 k_spiky_pow2(const particle_sim_internal_t *ps, f32 d) {
    if (d > ps->desc.kern_radius) { return 0.0f; }
    const f32 v = (ps->desc.kern_radius - d);
    // const f32 spiky_pow2 = (6.0f / (PI * powf(r, 4.0f)));
    return POW2(v) * ps->spiky_pow2;
}

M_INLINE f32 k_spiky_pow2_grad(const particle_sim_internal_t *ps, f32 d) {
    if (d > ps->desc.kern_radius) { return 0.0f; }
    const f32 v = (ps->desc.kern_radius - d);
    // const f32 spiky_pow2_grad = (12.0f / (PI * powf(r, 4.0f)));
    return -v * ps->spiky_pow2_grad;
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
M_INLINE v2i pc_cell_from_pos(v2 pos, f32 kern_radius) {
    // offset by 1 since cell bounds include one boundary cell on each side
    return
        v2i_add(
            v2i_from_v(
                v2_scale(pos, 1.0f / kern_radius)),
            v2i_of(1));
}

// convert cell to its index
M_INLINE int pc_index_from_cell(const particle_sim_internal_t *ps, v2i cell) {
    return (cell.y * ps->cell_dims.x) + cell.x;
}

#define pc_each(_origin, _name, ...)                                         \
    for (int n = 0; n < 9; n++) {                                            \
        const v2i cell_pos_n = v2i_add((_origin), pc_neighbors[n]);          \
        const sim_cell_t *cell = &ps->cells[pc_index_from_cell(ps, cell_pos_n)]; \
        int m = 0;                                                           \
        int _name;                                                           \
        while (m < cell->count) {                                            \
            _name = cell->start + m;                                         \
            m++;                                                             \
            { __VA_ARGS__ }                                                  \
        }                                                                    \
    }                                                                        \

static int cmp_particle_cell(const void *a, const void *b, void*) {
    const sim_particle_t *pa = a, *pb = b;
    return pa->cell - pb->cell;
}

M_INLINE void enforce_particle_ptr_bounds(
        const particle_sim_internal_t *ps,
        sim_particle_t *p) {
    v2 *pos = &p->pos;
    v2 *vel = &p->vel;

    UNROLL(2)
    for (int axis = 0; axis < 2; axis++) {
        const f32
            min_bound = ps->desc.boundary_thickness,
            max_bound = ps->desc.bounds.raw[axis] - ps->desc.boundary_thickness;

        if (pos->raw[axis] < min_bound) {
            pos->raw[axis] = min_bound;
            vel->raw[axis] *= -ps->desc.bounds_restitution;
        }

        if (pos->raw[axis] > max_bound) {
            pos->raw[axis] = max_bound;
            vel->raw[axis] *= -ps->desc.bounds_restitution;
        }

        pos->raw[axis] =
            clamp(
                pos->raw[axis],
                ps->desc.boundary_thickness,
                max_bound);
    }
}

M_INLINE void enforce_particle_bounds(
        const particle_sim_internal_t *ps,
        int index) {
    v2 *pos = &ps->p.pos[index];
    v2 *vel = &ps->p.vel[index];

    UNROLL(2)
    for (int axis = 0; axis < 2; axis++) {
        const f32
            min_bound = ps->desc.boundary_thickness,
            max_bound = ps->desc.bounds.raw[axis] - ps->desc.boundary_thickness;

        if (pos->raw[axis] < min_bound) {
            pos->raw[axis] = min_bound;
            vel->raw[axis] *= -ps->desc.bounds_restitution;
        }

        if (pos->raw[axis] > max_bound) {
            pos->raw[axis] = max_bound;
            vel->raw[axis] *= -ps->desc.bounds_restitution;
        }

        pos->raw[axis] =
            clamp(
                pos->raw[axis],
                ps->desc.boundary_thickness,
                max_bound);
    }
}

// update particle cell table
// NOTE: sorts "particles"!
static void compute_cells(
        DYNLIST(sim_cell_t) *cells,
        DYNLIST(sim_particle_t) *particles,
        v2i cell_dims,
        f32 kern_radius) {
    const int n_cells = dynlist_size(*cells);
    const int n_particles = dynlist_size(*particles);

    // clear indices
    for (int i = 0; i < n_cells; i++) {
        // INT_MAX -> no entries in cell
        (*cells)[i] = (sim_cell_t) {
            .start = INT_MAX,
            .count = 0,
        };
    }

    // compute cell for every particle
    // NOTE: uses "particles" array!
    for (int i = 0; i < n_particles; i++) {
        sim_particle_t *p_i = &(*particles)[i];

        const v2i cell_pos = pc_cell_from_pos(p_i->pos, kern_radius);

        // ensure particles are within simulation boundaries
        ASSERT(
            cell_pos.x >= 1
            && cell_pos.y >= 1
            && cell_pos.x <= (cell_dims.x - 2)
            && cell_pos.y <= (cell_dims.y - 2));

        p_i->cell = (cell_pos.y * cell_dims.x) + cell_pos.x;
        (*cells)[p_i->cell].count++;
    }

    // sort based on cell indices
    dynlist_sort(
        *particles,
        cmp_particle_cell,
        NULL);

    // find cell start/end indices
    {
        int i = 0;
        do {
            const sim_particle_t *p = &(*particles)[i];
            ASSERT(i == 0 || (*particles)[i - 1].cell != p->cell);

            (*cells)[p->cell].start = i;
            i += (*cells)[p->cell].count;

            // next cell must be different
            ASSERT_DEBUG(i == n_particles || (*particles)[i].cell != p->cell);
            ASSERT_DEBUG((*particles)[i - 1].cell == p->cell);
        } while (i < n_particles);
    }
}

static int cmp_sim_particle_y_value(const void *a, const void *b, void*) {
    const sim_particle_t *pa = a, *pb = b;
    return (int) sign(pa->pos.y - pb->pos.y);
}

static void sim_update_hulls(particle_sim_internal_t *psi) {
    const nstime_t start_ns = time_ns();

    allocator_t bump;
    bump_allocator_init(&bump, g_mallocator, 64 * 1024, NULL);

    // near prune distance threshold (world distance)
#define PRUNE_NEAR_THRESHOLD 0.05f

    // check if we need to prune vertices
    for (int i = 0; i < ARRLEN(psi->hulls); i++) {
        if (psi->hulls[i].last_pruned_compute_hash == psi->hulls[i].hash) {
            continue;
        }

        psi->hulls[i].last_pruned_compute_hash = psi->hulls[i].hash;

        const f32 prune_near2 = PRUNE_NEAR_THRESHOLD * PRUNE_NEAR_THRESHOLD;

        // reset/recompute pruned local-space vertices
        dynlist_resize(psi->hulls[i].vertices_pruned, 0);

        dynlist_each(psi->hulls[i].vertices, it0) {
            bool near = false;

            dynlist_each(psi->hulls[i].vertices_pruned, it1) {
                const f32 d2 = v3_distance2(*it0.el, *it1.el);
                if (d2 <= prune_near2) {
                    near = true;
                    break;
                }
            }

            if (!near) {
                *dynlist_push(psi->hulls[i].vertices_pruned) = *it0.el;
            }
        }
    }

    const v2 screen_to_bounds =
        v2_div(
            psi->desc.bounds,
            v2_from_i(psi->cam.target_size));

    const frustum_3d_t frustum = cam_view_proj_to_frustum(&psi->cam.view_proj);

    // use 1% of bounds size as near threshold for convex hull points
    const f32
        near_threshold = v2_max(psi->desc.bounds) * 0.0125,
        near_threshold2 = near_threshold * near_threshold;

    for (int i = 0; i < ARRLEN(psi->hulls); i++)  {
        if (!psi->hulls[i].hash) {
            continue;
        }

        DYNLIST(v2) points = dynlist_create(v2, &bump);

        dynlist_each(psi->hulls[i].vertices_pruned, it) {
            const v3 v_world =
                v3_from(
                    m4_mulv(
                        psi->hulls[i].transform,
                        v4_of(*it.el, 1.0f)));

            const v2 v_screen =
                world_pos_to_clamped_pixel(
                     &psi->cam.view_proj,
                     &frustum,
                     psi->cam.target_size,
                     v_world);

            const v2 v_bounds =
                v2_sub(
                    v2_mul(v_screen, screen_to_bounds),
                    psi->desc.render_offset);

            // only add if not near
            bool near = false;
            dynlist_each(points, it) {
                const f32 d2 = v2_distance2(v_bounds, *it.el);
                if (d2 < near_threshold2) {
                    near = true;
                    break;
                }
            }

            if (!near) {
                *dynlist_push(points) = v_bounds;
            }
        }

        // compute hull in particle-sim-space
        if (dynlist_size(points) < 3) {
            dynlist_resize(psi->hulls[i].lines, 0);
        } else {
            convex_hull_from_points(
                &bump,
                points,
                dynlist_size(points),
                &psi->hulls[i].lines);
        }

        if (dynlist_size(psi->hulls[i].lines) == 0) {
            continue;
        }

        // sort lines
        DYNLIST(line2f_t) *plines = &psi->hulls[i].lines;
        const int n_lines = dynlist_size(*plines);
        bool sort_failed = false;

        // sort until n - 1 since last line should be decided on second to last
        // iteration
        for (int j = 0; j < n_lines - 1; j++) {
            line2f_t *l_j = &(*plines)[j];
            line2f_t *l_j_plus_one = &(*plines)[j + 1];

            // find successor for j
            bool found = false;
            for (int k = j + 1; k < n_lines; k++) {
                line2f_t *l_k = &(*plines)[k];
                if (v2_eqv_eps(l_j->b, l_k->a)) {
                    if (k != j + 1) {
                        // swap line k into this position
                        swap(*l_j_plus_one, *l_k);
                    }
                    found = true;
                }
            }

            if (!found) {
                WARN("could not find successor in lines for %d", i);
                sort_failed = true;
                break;
            }
        }

        if (!sort_failed
            && !v2_eqv_eps((*plines)[n_lines - 1].b, (*plines)[0].a)) {
            // failed, last line should continue into first
            WARN("failed to sort lines for %d (wrap is wrong)", i);
            sort_failed = true;
        }

        if (sort_failed) {
            continue;
        }

        // find center, lowest points
        v2 center = v2_of(0);
        dynlist_each(*plines, it) {
            center = v2_add(center, it.el->a);
        }
        center = v2_divs(center, dynlist_size(psi->hulls[i].lines));

        // move points away from center, down if close to y_min
        for (int j = 0; j < n_lines; j++) {
            (*plines)[j].b =
                v2_add(
                    (*plines)[j].b,
                    v2_scale(v2_dir(center, (*plines)[j].b), 0.35f));

            // copy into start of next line
            (*plines)[(j + 1) % n_lines].a = (*plines)[j].b;
        }
    }

    bump_allocator_destroy(&bump);

    psi->stats.hull_avg_ms =
        (ns_to_ms(time_ns() - start_ns) * (1.0f / 60.0f)) +
            (psi->stats.hull_avg_ms * (59.0f / 60.0f));
}

static void sim_step(particle_sim_internal_t *ps) {
    if (mtx_lock(&ps->lock) != thrd_success) {
        WARN("could not lock for sim?");
        return;
    }

    ASSERT(ps);

    const i64 now = time_ns();
    if (now - ps->last_second > NS_PER_SECOND) {
        ps->last_second = now;
        ps->sps = ps->steps;
        ps->steps = 0;
    }

    ps->steps++;

    f32 dt;
    if (ps->last_step == 0) {
        dt = ps->desc.lookahead;
    } else {
        dt = ns_to_secs(now - ps->last_step);
    }

    dt = clamp(dt, 1.0f / 120.0f, 1.0f / 20.0f);

    if (ps->reset) {
        ps->reset = false;

        ps->n_target_particles[0] = 0;
        ps->n_target_particles[1] = 0;
        dynlist_resize(ps->particles, 0);
        dynlist_resize(ps->cells, 0);
        dynlist_resize(ps->p.pos, 0);
        dynlist_resize(ps->p.vel, 0);
        dynlist_resize(ps->p.pos_pred, 0);
        dynlist_resize(ps->p.force, 0);
        dynlist_resize(ps->p.density, 0);
        dynlist_resize(ps->p.pressure, 0);
        dynlist_resize(ps->p.near_density, 0);
        dynlist_resize(ps->p.near_pressure, 0);
        dynlist_resize(ps->p.cell, 0);

        for (int i = 0; i < 2; i++) {
            ps->hulls[i].hash = 0;
            dynlist_resize(ps->hulls[i].vertices, 0);
            dynlist_resize(ps->hulls[i].vertices_pruned, 0);
            dynlist_resize(ps->hulls[i].lines, 0);
            dynlist_resize(ps->hand_particles[i], 0);
        }
    }

    // update hulls
    sim_update_hulls(ps);

    // validate simulation parameters
#define VALIDATE(name, ...)                                    \
    if (!({ __auto_type v = ps->desc.name; __VA_ARGS__; })) {  \
        WARN("bad " #name ": %.3f", ps->desc.name);            \
        goto done;                                             \
    }

#define VALIDATE_EPS 0.00001f

    VALIDATE(kern_radius,         v > VALIDATE_EPS)
    VALIDATE(collision_radius,    v > VALIDATE_EPS)
    VALIDATE(boundary_thickness,  v > VALIDATE_EPS)
    VALIDATE(visc,                v > VALIDATE_EPS)
    VALIDATE(target_density,      v > VALIDATE_EPS)
    VALIDATE(pressure_mult,       v > VALIDATE_EPS)
    VALIDATE(near_pressure_mult,  v > VALIDATE_EPS)
    VALIDATE(boundary_force,      v >= 0.0f)
    VALIDATE(boundary_force_dist, v >= 0.0f)
    VALIDATE(lookahead,           v <= (1.0f / 30.0f))
    VALIDATE(dt_scale,            v >= 0.0f && v <= 2.0f)
    VALIDATE(bounds.x,            v > VALIDATE_EPS)
    VALIDATE(bounds.y,            v > VALIDATE_EPS)
    VALIDATE(dt_target,           v > VALIDATE_EPS)

#undef VALIDATE

    // prevent chaos within the particles
    dynlist_each(ps->particles, it) {
        if (!v2_isvalid(it.el->pos)
            || !v2_isvalid(it.el->vel)
            || !v2_isvalid(it.el->force)
            || isnan(it.el->density)
            || isinf(it.el->density)
            || isnan(it.el->near_density)
            || isinf(it.el->near_density)
            || isnan(it.el->pressure)
            || isinf(it.el->pressure)) {
            if (!v2_isvalid(it.el->pos))   { WARN("bad pos");      }
            if (!v2_isvalid(it.el->vel))   { WARN("bad vel");      }
            if (!v2_isvalid(it.el->force)) { WARN("bad force");    }
            if (isnan(it.el->density))     { WARN("bad density");  }
            if (isinf(it.el->density))     { WARN("bad density");  }
            if (isnan(it.el->near_density)){ WARN("bad near density");  }
            if (isinf(it.el->near_density)){ WARN("bad near density");  }
            if (isnan(it.el->pressure))    { WARN("bad pressure"); }
            if (isinf(it.el->pressure))    { WARN("bad pressure"); }
            dynlist_remove_it(ps->particles, it);
        }
    }

    const f32 r = ps->desc.kern_radius;
    ps->kern_radius_sq  = r * r;
    ps->poly6           = (4.0f  / (PI * powf(r, 8.0f)));
    ps->spiky_pow3      = (10.0f / (PI * powf(r, 5.0f)));
    ps->spiky_pow3_grad = (30.0f / (PI * powf(r, 5.0f)));
    ps->spiky_pow2      = (6.0f  / (PI * powf(r, 4.0f)));
    ps->spiky_pow2_grad = (12.0f / (PI * powf(r, 4.0f)));

    ps->num_particles = dynlist_size(ps->particles);

    // add +/- one cell on each boundary
    ps->cell_dims =
        v2i_add(
            v2i_from_v(
                v2_ceil(v2_divs(ps->desc.bounds, ps->desc.kern_radius))),
            v2i_of(2));
    ps->num_cells = ps->cell_dims.x * ps->cell_dims.y;

    if (!ps->cells) {
        dynlist_init(ps->cells, &ps->arena);
    }

    dynlist_resize(ps->cells, ps->num_cells);

    const i64 sim_start_ns = time_ns();

    const f32 r2 = POW2(ps->desc.kern_radius);
    const f32 bfd2 = POW2(ps->desc.boundary_force_dist);

    if (ps->num_particles == 0) {
        goto done_simulating;
    }

    // ensure sizes
    dynlist_resize(ps->p.pos,           ps->num_particles);
    dynlist_resize(ps->p.vel,           ps->num_particles);
    dynlist_resize(ps->p.pos_pred,      ps->num_particles);
    dynlist_resize(ps->p.force,         ps->num_particles);
    dynlist_resize(ps->p.density,       ps->num_particles);
    dynlist_resize(ps->p.pressure,      ps->num_particles);
    dynlist_resize(ps->p.near_density,  ps->num_particles);
    dynlist_resize(ps->p.near_pressure, ps->num_particles);
    dynlist_resize(ps->p.cell,          ps->num_particles);

    #pragma omp parallel
    {
        // enforce bounds early in case bounds changed
        #pragma omp for
        for (int i = 0; i < ps->num_particles; i++) {
            enforce_particle_ptr_bounds(ps, &ps->particles[i]);
        }

        // compute cells on a single thread
        #pragma omp single
        {
            compute_cells(
                &ps->cells,
                &ps->particles,
                ps->cell_dims,
                ps->desc.kern_radius);
        }

        // distribute data into constituent arrays
        #pragma omp for
        for (int i = 0; i < ps->num_particles; i++) {
            const sim_particle_t *const p_i = &ps->particles[i];
            ps->p.pos[i]           = p_i->pos;
            ps->p.vel[i]           = p_i->vel;
            ps->p.pos_pred[i]      = p_i->pos_pred;
            ps->p.force[i]         = p_i->force;
            ps->p.density[i]       = p_i->density;
            ps->p.pressure[i]      = p_i->pressure;
            ps->p.near_density[i]  = p_i->near_density;
            ps->p.near_pressure[i] = p_i->near_pressure;
            ps->p.cell[i]          = p_i->cell;
        }

        // predict positions
        #pragma omp for
        for (int i = 0; i < ps->num_particles; i++) {
            ps->p.pos_pred[i] =
                v2_add(
                    ps->p.pos[i],
                    v2_scale(ps->p.vel[i], ps->desc.lookahead));
        }

        // compute density/pressure
        #pragma omp for
        for (int i = 0; i < ps->num_particles; i++) {
            ps->p.density[i] = 0.0f;
            ps->p.near_density[i] = 0.0f;

            const v2i cell_org =
                pc_cell_from_pos(ps->p.pos[i], ps->desc.kern_radius);

            pc_each(cell_org, j, {
                const v2 p_i_to_p_j = v2_sub(ps->p.pos_pred[j], ps->p.pos_pred[i]);
                const f32 dist2 = v2_norm2(p_i_to_p_j);
                if (dist2 < r2) {
                    const f32 dist = sqrtf(dist2);
                    ps->p.density[i] += MASS * k_density(ps, dist);
                    ps->p.near_density[i] += MASS * k_near_density(ps, dist);
                }
            })

            ps->p.pressure[i] =
                ps->desc.pressure_mult
                    * (ps->p.density[i] - ps->desc.target_density);
            ps->p.near_pressure[i] =
                ps->desc.near_pressure_mult
                    * (ps->p.near_density[i] - ps->desc.target_density);
        }

        #pragma omp for
        for (int i = 0; i < ps->num_particles; i++) {
            v2
                f_interact = v2_of(0.0f),
                f_pressure = v2_of(0.0f),
                f_viscosity = v2_of(0.0f),
                f_boundary = v2_of(0.0f),
                f_gravity = ps->desc.gravity;

            const v2i cell_org =
                pc_cell_from_pos(ps->p.pos[i], ps->desc.kern_radius);
            pc_each(cell_org, j, {
                if (i == j) { continue; }

                const v2 p_i_to_p_j = v2_sub(ps->p.pos_pred[j], ps->p.pos_pred[i]);
                const f32 dist2 = v2_norm2(p_i_to_p_j);
                if (dist2 > r2) { continue; }

                f32 dist = sqrtf(dist2);
                v2 p_i_to_p_j_normalized;

                if (dist < PRESSURE_EPS || isnan(dist) || isinf(dist)) {
                    dist = PRESSURE_EPS;
                    p_i_to_p_j_normalized = rand_v2_dir(&ps->rand);
                } else {
                    p_i_to_p_j_normalized = v2_scale(p_i_to_p_j, 1.0f / dist);
                }

                // base pressure
                f_pressure =
                    v2_add(
                        f_pressure,
                        v2_scale(
                            p_i_to_p_j_normalized,
                            MASS
                                * ((ps->p.pressure[i] + ps->p.pressure[j]) / 2.0f)
                                * (1.0f / ps->p.density[j])
                                * k_density_grad(ps, dist)));

                // near pressure
                f_pressure =
                    v2_add(
                        f_pressure,
                        v2_scale(
                            p_i_to_p_j_normalized,
                            MASS
                                * ((ps->p.near_pressure[i] + ps->p.near_pressure[j]) / 2.0f)
                                * (1.0f / ps->p.near_density[j])
                                * k_near_density_grad(ps, dist)));

                f_viscosity =
                    v2_add(
                        f_viscosity,
                        v2_scale(
                            v2_sub(ps->p.vel[j], ps->p.vel[i]),
                            ps->desc.visc
                                * MASS
                                * k_visc(ps, dist)));
            })

            const int hull = ps->particles[i].is_right ? 1 : 0;
            dynlist_each(ps->hulls[hull].lines, it) {
                const v2 proj =
                    point_project_line(
                        ps->p.pos_pred[i],
                        it.el->b,
                        it.el->b);

                const v2 proj_to_pos = v2_sub(ps->p.pos_pred[i], proj);
                const f32 len2 = v2_norm2(proj_to_pos);
                if (len2 <= bfd2) {
                    const f32 len = sqrtf(len2);
                    const v2 dir = v2_divs(proj_to_pos, len);
                    f32 force = 1.0f / len2;            // inv square
                    force *= (1.0f / ps->p.density[i]); // scale with density
                    force *= ps->desc.boundary_force;   // scale with force
                    f_boundary =
                        v2_add(
                            f_boundary,
                            v2_scale(dir, force));
                }
            }

            ps->p.force[i] =
                v2_add(
                    v2_add(f_gravity, f_interact),
                    v2_add(
                        v2_add(f_pressure, f_viscosity),
                        f_boundary));
        }

        #pragma omp for
        for (int i = 0; i < ps->num_particles; i++) {
            const sim_particle_t *const p_i = &ps->particles[i];

            // accelerate by F = ma (m is density)
            ps->p.vel[i] =
                v2_add(
                    ps->p.vel[i],
                    v2_scale(ps->p.force[i], dt / ps->p.density[i]));

            const f32 r = ps->desc.collision_radius;

            // ensure containment by collision lines
            DYNLIST(line2f_t) *containing_hull =
                &ps->hulls[p_i->is_right ? 1 : 0].lines;

            dynlist_each(*containing_hull, it) {
                v2 resolved;
                f32 t_circle, t_segment;
                if (!sweep_circle_line_segment(
                        ps->p.pos[i],
                        r,
                        v2_scale(ps->p.vel[i], dt),
                        it.el->a,
                        it.el->b,
                        &t_circle,
                        &t_segment,
                        &resolved)) {
                    continue;
                }

                // get normal of line pointing towards particle
                v2 normal = line_right_normal(it.el->a, it.el->b);
                if (point_side(resolved, it.el->a, it.el->b) > 0) {
                    normal = v2_scale(normal, -1);
                }

                ps->p.pos[i] =
                    v2_add(resolved, v2_scale(normal, COLLISION_EPS)); 

                const v2 tangent = v2_rotate(normal, PI_2);

                // project velocity along side
                const v2
                    v_towards_wall = v2_proj(v2_normalize(ps->p.vel[i]), normal),
                    restitution =
                        v2_scale(
                            v_towards_wall,
                            -1.0f
                                * v2_norm(ps->p.vel[i])
                                * ps->desc.bounds_restitution);

                // projected velocity onto tangent, add resitution
                ps->p.vel[i] =
                    v2_add(
                        v2_proj(
                            v2_scale(ps->p.vel[i], ps->desc.bounds_restitution),
                            tangent),
                        v2_add(
                            restitution,
                            v2_scale(normal, 0.01f)));
            }

            // move by velocity
            ps->p.pos[i] = v2_add(ps->p.pos[i], v2_scale(ps->p.vel[i], dt));

            // ensure containment by collision lines
            dynlist_each(*containing_hull, it) {
                if (point_side(ps->p.pos[i], it.el->a, it.el->b) > 0.0f) {
                    const v2 normal = line_right_normal(it.el->a, it.el->b);
                    ps->p.pos[i] =
                        v2_add(
                            point_project_segment(ps->p.pos[i], it.el->a, it.el->b),
                            v2_scale(normal, r + COLLISION_EPS));

                    ps->p.vel[i] = v2_scale(normal, 1.0f);
                }
            }

            enforce_particle_bounds(ps, i);
        }
    }

done_simulating:
    // sort particles on weapon/arm, y-value
    for (int i = 0; i < 2; i++) {
        dynlist_resize(ps->hand_particles[i], 0);
    }

    // sort into weapon, arm and reconstruct simultaneously
    for (int i = 0; i < ps->num_particles; i++) {
        DYNLIST(sim_particle_t) *dst =
            &ps->hand_particles[ps->particles[i].is_right ? 1 : 0];

        *dynlist_push(*dst) =
            (sim_particle_t) {
                .pos           = ps->p.pos[i],
                .vel           = ps->p.vel[i],
                .pos_pred      = ps->p.pos_pred[i],
                .force         = ps->p.force[i],
                .density       = ps->p.density[i],
                .pressure      = ps->p.pressure[i],
                .near_density  = ps->p.near_density[i],
                .near_pressure = ps->p.near_pressure[i],
                .cell          = ps->p.cell[i],
                .is_right      = ps->particles[i].is_right,
                .remove        = ps->particles[i].remove,
            };
    }


    for (int i = 0; i < 2; i++) {
        dynlist_sort(ps->hand_particles[i], cmp_sim_particle_y_value, NULL);
    }

    // reset particles, they're getting re-sorted...
    dynlist_resize(ps->particles, 0);

    for (int i = 0; i < 2; i++) {
        DYNLIST(line2f_t) *phull = &ps->hulls[i].lines;

        if (dynlist_size(*phull) < 3) {
            // bad hull, do nothing
            continue;
        }

        DYNLIST(sim_particle_t) *list = &ps->hand_particles[i];
        const int target = ps->n_target_particles[i];

        // compute hull center, bottom
        v2 center = v2_of(0);
        dynlist_each(*phull, it) {
            center = v2_add(center, it.el->a);
        }
        center = v2_divs(center, dynlist_size(*phull));

        const v2 bottom =
            convex_poly_project_onto_edge(
                &(*phull)[0],
                dynlist_size(*phull),
                v2_of(center.x, -10.0f));

        // remove lowest particles
        while (dynlist_size(*list) > 0 && dynlist_size(*list) > target) {
            dynlist_remove(*list, 0);
        }

        // add at bottom if needed
        while (dynlist_size(*list) < target) {
            sim_particle_t *p;
            *(p = dynlist_push(*list)) =
                (sim_particle_t) {
                    .pos =
                        v2_add(
                            bottom,
                            rand_v2(
                                &ps->rand,
                                v2_of(-0.25f, 0.00f),
                                v2_of(+0.25f, 0.02f))),
                    .is_right = i == 1,
                };

            enforce_particle_ptr_bounds(ps, p);
        }

        // merge back into particles list
        dynlist_push_all(ps->particles, *list);

        ps->stats.n_hand_particles[i] = dynlist_size(*list);
    }

    // TODO: we can re-use these cells? for the next time around?
    // compute into cells
    dynlist_resize(ps->cells, ps->num_cells);
    ps->cell_dims = ps->cell_dims;

    if (ps->num_particles != 0 && dynlist_size(ps->particles) != 0) {
        compute_cells(
            &ps->cells,
            &ps->particles,
            ps->cell_dims,
            ps->desc.kern_radius);
    }

    const f32 step_ms = ns_to_ms(time_ns() - sim_start_ns);

    ps->stats.step_avg_ms =
        (step_ms * (1.0f / 60.0f)) +
            (ps->stats.step_avg_ms * (59.0f / 60.0f));
    ps->stats.dt_avg_ms =
        (secs_to_ms(dt) * (1.0f / 60.0f)) +
            (ps->stats.dt_avg_ms * (59.0f / 60.0f));

#ifdef _OPENMP
    ps->stats.threads = omp_get_max_threads();
#else
    ps->stats.threads = 1;
#endif // ifdef _OPENMP

    ps->last_step = now;

done:
    mtx_unlock(&ps->lock);
}
