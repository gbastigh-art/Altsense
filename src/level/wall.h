#pragma once

#include "defs.h"

// create new wall
wall_t *wall_new(
    level_t *level,
    vertex_t *v0,
    vertex_t *v1);

// remove wall
void wall_delete(level_t *level, wall_t *wall);

// recalculate wall fields after update
void wall_recalculate(level_t *level, wall_t *wall);

// true if wall is part of sector
bool wall_in_sector(
    const level_t *level,
    const wall_t *wall,
    const sector_t *sector);

// finds sector two walls share, NULL if they share no sector
sector_t *walls_shared_sector(
    const level_t *level,
    const wall_t *w0,
    const wall_t *w1);

// set vertex i (0 or 1) for wall
void wall_set_vertex(
    level_t *level,
    wall_t *wall,
    int i,
    vertex_t *v);

// replaces vertex "oldvert" on wall with "newvert" in same position
void wall_replace_vertex(
    level_t *level,
    wall_t *wall,
    vertex_t *oldvert,
    vertex_t *newvert);

// set side i (0 or 1) for wall
void wall_set_side(
    level_t *level,
    wall_t *wall,
    int i,
    side_t *s);

// get midpoint of wall
v2 wall_midpoint(wall_t *wall);

// split a wall at some pos "p" which lies either on it or very close to it
// (result)->v0 is the new vertex
wall_t *wall_split(level_t *level, wall_t *wall, v2 p);
