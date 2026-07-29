#pragma once

#include "defs.h"

// update sightline for entity, resetting counters, target_*, etc.
// offsets are relative to center
void enemy_update_sightline(
        level_t *level,
        entity_t *ent,
        v3 eye_offset,
        v3 target_offset);

// tries to find path from entity center to goal
// updates entity_t.last_path_tick
// returns true if path was found
bool enemy_try_find_path(level_t *level, entity_t *ent, v3 goal);

// returns true if movement to desitation is "trivial" (does not need path)
bool enemy_trivial_to_point(level_t *level, entity_t *ent, v2 dst);

// returns true if this entity should move back/is headed for a place they don't
// want to be (cliff, liquid, etc.)
bool enemy_should_move_back(const level_t *level, const entity_t *ent);

// call before anything else in *_tick for AI entities
void enemy_pre_tick(level_t *level, entity_t *ent);

// call after everything else in *_tick for AI entities
void enemy_post_tick(level_t *level, entity_t *ent);

// check for enemy death, corpse + explode into blood if dead
// returns true if enemy is dead
bool enemy_check_death_and_explode(level_t *level, entity_t *ent);

// walk enemy towards direction with specified base speed
// returns actual moved speed
f32 enemy_walk_towards(
        level_t *level,
        entity_t *ent,
        f32 base_speed,
        v2 dir,
        f32 dt);

// FLY enemy towards direction with specified base speed
// returns actual moved speed
f32 enemy_fly_towards(
        level_t *level,
        entity_t *ent,
        f32 base_speed,
        v3 dir,
        f32 dt);
