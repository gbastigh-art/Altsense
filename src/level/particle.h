#pragma once

#include "defs.h"

// update all particles in a level, managers FLOATERs as well
void particles_tick(level_t *level);

particle_t *particle_new(level_t *level, v2 pos, const particle_t *defaults);

// tag particle for deletion, actual deleting happens under per-sector iteration
void particle_enqueue_delete(level_t *level, particle_t *particle);

void particle_fixed_update(level_t *level, particle_t *p);

void particle_tick(level_t *level, particle_t *particle);

void particle_inst_desc(
        level_t *level,
        particle_t *p,
        particle_inst_desc_t *desc);

light_desc_t particle_light(const level_t *level, const particle_t *particle);
