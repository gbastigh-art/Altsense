#pragma once

#include "level/level_types.h"
#include "defs.h"

// iterate a level list
#define level_each(_T, _list, _p) genlist_each(_T, (_list), (_p))

// returns true if alloc'ing from the specified list would not return NULL
bool level_can_alloc(level_t *level, genlist_t *list);

// allocate a level element (T_*) from the specified list; handles generational
// indices and zero-ing (elements are zeroed when allocated !!)
// can return NULL if !level_can_alloc(level, list)
void *level_try_alloc(level_t *level, genlist_t *list);

// level_try_alloc, but only try to get a specific index in the list
void *level_try_alloc_at(level_t *level, genlist_t *list, int index);

// free a level element from the specified list previously allocated by
// level_try_alloc
void level_free(level_t *level, genlist_t *list, void *ptr);

// LTF_* (| LTF_* ...) -> string
char *level_types_to_str(int types, allocator_t *al);

// init level
void level_init(level_t *level, allocator_t *allocator);

// destroy level
void level_destroy(level_t *level);

void level_set_name(level_t *level, const char *name);

// update level (per frame)
void level_frame_update(level_t *level);

// fixed update (physics) level
void level_fixed_update(level_t *level);

// tick level
void level_tick(level_t *level);

// enqueue ptr to be deleted next frame update
void level_enqueue_delete(level_t *level, lptr_t ptr);

// enqueue ptr to be recalc'd next frame update (if it isn't recalc'd before
// that)
void level_enqueue_recalc(level_t *level, lptr_t ptr);

// pushes a side to get level_update_side_sector'd
void level_push_dirty_sect_side(level_t *level, side_t *side);

// traces a continous chain of sides with a minimal inner angle starting from
// startside. returns 0 on failure to trace a complete shape.
//
// can optionally use fail_trace_sides to have trace fail if one of these sides
// it encountered
int level_trace_sides(
    level_t *level,
    side_t *start_side,
    DYNLIST(side_t*) *sides,
    const DYNLIST(side_t*) *fail_trace_sides);

// finds sector vertex and wall share, NULL if they share no sector
// find a side near vertex in sector, otherwise null
side_t *level_find_near_side(
    const level_t *level,
    const vertex_t *vertex,
    const sector_t *preferred_sector);

// find a sector near to a point, otherwise NULL
sector_t *level_find_near_sector(
    const level_t *level,
    v2 point);

// update the sector of a side, trying to form new sectors as appropriate
sector_t *level_update_side_sector(
    level_t *level,
    side_t *side);

// intersect all level walls along p0 -> p1, returning a list of walls hit AND
// a list of pointers to which sides of each wall the "exit" is on (side whose
// normal is closest to that of p0 -> p1, regardless of if it exists)
int level_intersect_walls_on_line(
    level_t *level,
    v2 p0,
    v2 p1,
    wall_t **walls,
    side_t ***sides,
    int n);

// returns side in list which has an other side which is on the same wall with
// the same (non-NULL) sector
// if none, returns NULL
side_t *level_sides_find_double(
    level_t *level,
    side_t **sides,
    int n);

// returns side in sector which has other which is also in sector
// if none, returns NULL
side_t *level_sector_find_double_side(
    level_t *level,
    sector_t *sector);

// a sector is "coherent" iff:
// * all sides which can be traced from other sector sides are also part of the
//   sector
// * all holes are also part of the sector, and follow the above criteria
bool level_sector_is_coherent(
    level_t *level,
    sector_t *sector);

// finds nearest side to point, NULL if there is no such side
side_t *level_nearest_side(
    const level_t *level,
    v2 point);

// finds nearest vertex to point, NULL if there is no such vertex
vertex_t *level_nearest_vertex(
    const level_t *level,
    v2 point,
    f32 *dist);

// "updates" the sector of a point by searching in nearby sectors first to see
// if they contain the point
// returns SECTOR_NONE if the point is not in a sector
sector_t *level_find_point_sector(
    const level_t *level,
    v2 point,
    const sector_t *sector);

// find point subsector
subsector_t *level_find_point_subsector(
        const level_t *level,
        v2 point,
        const subsector_t *sub);

// clamp point to be inside level boundaries
v2 level_clamp_point(const level_t *level, v2 point);

// clamps point to be inside level boundaries, also on Z axis
v3 level_clamp_point_3d(const level_t *level, v3 point, f32 height);

typedef struct level_sides_in_radius_params {
    v2 pos;
    f32 r;
    DYNLIST(side_t*) *out;
    bool (*filter_fn)(
        const side_t*,
        const struct level_sides_in_radius_params*);
    void *userdata;
} level_sides_in_radius_params_t;

// get all sides in a specified radius
void level_sides_in_radius(
    level_t *level,
    const level_sides_in_radius_params_t *params);

// get all walls in specified radius
int level_walls_in_radius(
    level_t *level, v2 pos, f32 r, DYNLIST(wall_t*) *out);

// get all walls in specified box
int level_walls_in_area(
    level_t *level, box2f_t area, DYNLIST(wall_t*) *out);

int level_sectors_in_radius(
    level_t *level, v2 pos, f32 r, DYNLIST(sector_t*) *out);

// TODO: remove?
int level_contiguous_sectors_in_radius(
    level_t *level, sector_t *start,
    v2 pos, f32 r, DYNLIST(sector_t*) *out);

void level_entities_in_radius(
    level_t *level, v2 pos, f32 r, DYNLIST(entity_t*) *out);

void level_entities_in_radius_3d(
    level_t *level, v3 pos, f32 r, DYNLIST(entity_t*) *out);

// r = 0.0f -> unlimited radius
entity_t *level_nearest_entity_with_type(
        level_t *level,
        v3 pos,
        f32 r,
        entity_type_e type);

void level_subsectors_in_area(
    level_t *level, box2f_t area, DYNLIST(subsector_t*) *out);

void level_subsectors_in_radius(
    level_t *level, v2 pos, f32 r, DYNLIST(subsector_t*) *out);

int level_tris_in_area(
    level_t *level, box2f_t area, DYNLIST(sect_tri_t*) *out);

int level_tris_in_radius(
    level_t *level, v2 pos, f32 r, DYNLIST(sect_tri_t*) *out);

// get random point (uniformly distributed!) within specified radius
v2 level_random_point_in_radius(
    level_t *level, rand_t *rand, v2 pos, f32 r);

// get random point (uniformly distributed across 3D volume) within specified
// radius
v2 level_random_point_in_radius_by_volume(
    level_t *level, rand_t *rand, v2 pos, f32 r);

// shift a list of lptrs by an offset, or the entire level if ptrs is NULL
void level_shift(level_t *level, v3 shift, DYNLIST(lptr_t) *ptrs);

// rotate a list of lptrs about a point, or the entire level if ptrs is NULL
void level_rotate(
    level_t *level, v2 point, f32 angle, DYNLIST(lptr_t) *ptrs);

// gets point zs
rangef_t level_point_zs(const level_t *level, v2 point);

enum {
    LPIA_NONE       = 0 << 0,
    LPIA_WHOLE_SECT = 1 << 0, // whole sector must be in area
    LPIA_WHOLE_WALL = 1 << 1, // whole wall must be in area
};

// gets all pointers matching tags in specified area
void level_ptrs_in_area(
    const level_t *level,
    box2f_t area,
    int tags,
    DYNLIST(lptr_t) *ptrs,
    int flags);
