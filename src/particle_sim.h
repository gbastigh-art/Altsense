#pragma once

#include "util/thread.h" /* IWYU pragma: keep */
#include "util/types.h"
#include "util/alloc.h"
#include "util/math.h"
#include "defs.h"

#define MAX_SIM_PARTICLES 2048
#define MAX_SIM_CELLS 4096

// particle simulation config
typedef struct particle_sim_desc {
    v2 render_offset;
    v2 bounds;
    f32 kern_radius;
    f32 collision_radius;
    f32 boundary_thickness;
    f32 visc;
    f32 target_density;
    f32 pressure_mult;
    f32 near_pressure_mult;
    f32 boundary_force;
    f32 boundary_force_dist;
    v2 gravity;
    f32 bounds_restitution;
    f32 lookahead;
    f32 dt_scale;
    f32 dt_target;
} particle_sim_desc_t;

typedef struct particle_sim_stats {
    f32 step_avg_ms;
    f32 hull_avg_ms;
    f32 dt_avg_ms;
    int threads;
    int n_hand_particles[2];
} particle_sim_stats_t;

typedef int sim_particle_id_t;

typedef struct sim_particle {
    v2 pos, vel, pos_pred, force;
    f32 density, pressure, near_density, near_pressure;
    int  cell:     30;
    bool is_right: 1;
    bool remove:   1;
} sim_particle_t;

typedef struct {
    int start, count;
} sim_cell_t;

typedef struct particle_sim_internal particle_sim_internal_t;

typedef struct particle_sim {
    allocator_t arena;

    // parameters
    particle_sim_desc_t desc;

    particle_sim_stats_t stats;

    // internal simulation
    particle_sim_internal_t *internal;

    // per-frame hand models (lookup on model atlas)
    struct {
        char name[64], index_group[64];
        m4 transform;
    } hand_models[2];

    // left/right hand hull in particle-sim-space
    DYNLIST(line2f_t) hulls[2];

    // vertices making up each hull
    DYNLIST(v3) vertices[2];

    // left/right hand target particles
    int n_target_particles[2];

    // from internal simulation
    DYNLIST(sim_particle_t) particles;

    // cells
    DYNLIST(sim_cell_t) cells;

    // for "cells"
    v2i cell_dims;

    // if true, sim resets on next step
    bool reset;
} particle_sim_t;

void particle_sim_init(
        particle_sim_t *ps,
        allocator_t *allocator,
        const particle_sim_desc_t *desc);

void particle_sim_end_frame(particle_sim_t *ps);

void particle_sim_destroy(particle_sim_t *ps);

// shows imgui configuration options for specified particle sim desc
// returns true on change
bool particle_sim_desc_imgui(particle_sim_desc_t *desc);
