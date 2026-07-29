#pragma once

#include "level/level_types.h"
#include "defs.h"

M_INLINE bool sect_line_eq(const sect_line_t *a, const sect_line_t *b) {
    return
        (v2_eqv_eps(a->a->pos, b->a->pos)
         && v2_eqv_eps(a->b->pos, b->b->pos))
        || (v2_eqv_eps(a->b->pos, b->a->pos)
            && v2_eqv_eps(a->a->pos, b->b->pos));
}

// get 3D volume represented by a sector triangle
// NOTE: volume is a rough estimate for sloped sector planes, not exact
// use max_height < 0.0f to use actual height
f32 sect_tri_volume(const sect_tri_t *tri, f32 max_height);

// get 3D volume represented by a subsector
// NOTE: volume is a rough estimate for sloped sector planes, not exact
// use max_height < 0.0f to use actual height
f32 subsector_volume(const subsector_t *sub, f32 max_height);

// distance from subsector a to b via the specified line
// accounts for disconnected portals :)
// "line" must be on "a"
f32 subsector_distance_via(
        const subsector_t *a,
        const subsector_t *b,
        const sect_line_t *line);

M_INLINE const sector_type_t *sector_type(const sector_t *sect) {
    return &SECTOR_TYPES[sect->type];
}

sectmat_data_t sector_get_mat(const sector_t *sect);

// create a sector
// copies properties from "like" if non-NULL
sector_t *sector_new(level_t *level, const sector_t *like);

void sector_delete(level_t *level, sector_t *sector);

void sector_delete_with_sides(level_t *level, sector_t *sector);

// copy properties of src onto dst
void sector_copy_props(level_t *level, sector_t *dst, const sector_t *src);

// try to set "sector->like" to "like"
bool sector_try_set_like(const level_t *level, sector_t *sector, sector_t *like);

// get definitive "like" sector
// can be NULL if !sector->like
sector_t *sector_get_like(const sector_t *sector);

void sector_fixed_update(level_t *level, sector_t *sector);

void sector_tick(level_t *level, sector_t *sector);

// see sector_traverse_neighbors
typedef bool (*sector_traverse_neighbors_f)(level_t*, sector_t*, void*);

// traverse sector neighbors, calling "callback for each"
void sector_traverse_neighbors(
    level_t *level,
    sector_t *sector,
    sector_traverse_neighbors_f callback,
    void *userdata);

// get sector vertices, returns number of vertices
int sector_vertices(
    const level_t *level,
    const sector_t *sector,
    DYNLIST(vertex_t*) *out);

// true if vertex is part of sector
bool sector_contains_vertex(
    const level_t *level,
    const sector_t *sector,
    const vertex_t *vertex);

// true if point is inside of sector
bool sector_contains_point(
    const sector_t *sector,
    v2 point);

// get evenly distributed random point in sector
v2 sector_rand_point(const sector_t *sector, rand_t *rand);

enum {
    SUBSECTOR_CLAMP_FLAGS_NONE             = 0 << 0,
    SUBSECTOR_CLAMP_FLAGS_ONLY_REAL_SIDES  = 1 << 0,
    SUBSECTOR_CLAMP_FLAGS_ONLY_SOLID_SIDES = 1 << 1,
};

// clamps point to be inside of subsector
v2 subsector_clamp_point(const subsector_t *sub, v2 point);

// clamp point to be inside of sector
v2 sector_clamp_point(const sector_t *sector, v2 point);

// project point onto nearest sector edge
v2 sector_project_onto_edge(const sector_t *sector, v2 point);

// true if sector intersect (or contains!) lines
bool sector_intersects_line(
    const sector_t *sector,
    v2 a,
    v2 b);

// true if sector intersects or contains box2i
bool sector_intersects_box2f(
    const sector_t *sector,
    box2f_t box);

// true if sector is entirely contained by AABB
bool sector_contained_by_box2f(
    const sector_t *sector,
    box2f_t box);

// get sides which form sector, n_sides must be >= sector->n_sides
// returns number of sides added
int sector_get_sides(
    const level_t *level,
    const sector_t *sector,
    DYNLIST(side_t*) *sides);

// recalculates fields after update to sides, etc.
void sector_recalculate(level_t *level, sector_t *sect);

// find subsector of point in sector, NULL if not found
subsector_t *sector_find_subsector(sector_t *sector, v2 point);

// adds a side to a sector
void sector_add_side(
    level_t *level,
    sector_t *sector,
    side_t *side);

// removes a side from a sector
void sector_remove_side(
    level_t *level,
    sector_t *sector,
    side_t *side);

// true if subsector contains point
bool subsector_contains_point(const subsector_t *sub, v2 point);

// compute point z range in sector
rangef_t sector_point_zs(const sector_t *sector, v2 point);

// get normal for specified plane
v3 sector_plane_normal(const sector_t *sector, plane_type_e plane);

// get (a, b, c, d) plane equation form for sector plane
v4 sector_plane_vec(const sector_t *sector, plane_type_e plane);

// compute vertex z from sector slope
rangef_t sector_vertex_zs(const sector_t *sector, vertex_t *vertex);

// clamp z at specific point in sector
f32 sector_clamp_z(const sector_t *sector, v2 point, f32 z);

// clamp z at specific point in sector, with a specified height
f32 sector_clamp_z_h(const sector_t *sector, v2 point, f32 z, f32 h);

// finds walls shared by two sectors
// NOTE: includes portal walls (sector a walls)
void sector_shared_walls(
    const sector_t *a, const sector_t *b, DYNLIST(wall_t*) *out);

// finds the closest point of sector a to sector b, and vice versa
// returns { closest on a, closest on b }
void sector_closest_points(
    const level_t *level,
    const sector_t *a,
    const sector_t *b,
    v2 ps[2]);
