#pragma once

#include "level/level_types.h"

// can return NULL
decal_t *decal_new(level_t *level, const decal_t *defaults);

void decal_delete(level_t *level, decal_t *decal);

// set decal type
void decal_set_type(level_t *level, decal_t *decal, decal_type_e type);

decal_t *decal_new_on_trace_hit(
        level_t *level,
        const trace_hit_t *hit,
        f32 z,
        const decal_t *defaults);

void decal_tick(level_t *level, decal_t *decal);

// recalulates decal firelds after adjustment or adjustment to parent
void decal_recalculate(level_t *level, decal_t *decal);

// get decal pos in world space
v3 decal_worldpos(const decal_t *decal);

// get plane of surface which decal is on
v4 decal_surface_plane(const level_t *level, const decal_t *decal);

// set side decal is on
void decal_set_side(
        level_t *level,
        decal_t *decal,
        side_t *side);

// set sector decal is on
void decal_set_sector(
        level_t *level,
        decal_t *decal,
        sector_t *sector,
        plane_type_e plane);

// gets 2D bounds of decal on its surface
// if on side, bounds are XY / Z, on sector, they are X/Y
// on side, coordinates are side-relative "x" and side-relative z
// on sector, coordinates are world space
box2f_t decal_bounds(const level_t *level, const decal_t *decal);

// get side segment decal is lying on
side_segment_t decal_side_segment(const level_t *level, const decal_t *decal);

// 3D center of decal
v3 decal_center(const level_t *level, const decal_t *decal);

light_desc_t decal_light(const level_t *level, const decal_t *decal);
