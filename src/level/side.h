#pragma once

#include "util/math.h"
#include "defs.h"

// create new side optionally like another
side_t *side_new(level_t *level, const side_t *like);

// remove side
void side_delete(level_t *level, side_t *side);

sidemat_data_t side_get_mat(const side_t *side);

// copy properties of src onto dst
void side_copy_props(level_t *level, side_t *dst, const side_t *src);

// try to set "side->like" to "like"
bool side_try_set_like(const level_t *level, side_t *side, side_t *like);

// get definitive "like" side
// can be NULL if !side->like
side_t *side_get_like(const side_t *side);

// recalculate side fields
void side_recalculate(level_t *level, side_t *side);

// true if side is right (front) for its wall
bool side_is_right(const side_t *side);

// get index of side on wall
// returns -1 if side is not on wall
int side_index(const side_t *side);

// get side (possibly NULL) opposite of side
side_t *side_other(const side_t *side);

// get vertices for the wall side such that the side is on the right side of the
// line formed by the vertices
void side_get_vertices(
    const side_t *side,
    vertex_t *vs[2]);

// gets point which is slightly off of the side wall in the direction of the
// side normal
v2 side_normal_point(const side_t *side);

// get wall normal corresponding to side
v2 side_normal(const side_t *side);

// get plane of side
v4 side_plane_vec(const side_t *side);

// convert a list of sides [(v0.x, v0.y) -> (v1.x, v1.y) ...] into a list of the
// lines of each side in order
void sides_to_lines(
        side_t **sides,
        int n_sides,
        DYNLIST(line2f_t) *lines);

// get visible segments for side
side_segments_t side_get_segments(const level_t *level, const side_t *side);

// get side segment for this side at z_world
// returns false if there is no such segment
bool side_get_z_segment(
   const level_t *level,
   const side_t *side,
   v2 point,
   f32 z,
   side_segment_t *seg);

// TODO: doc
bool side_get_offset_segment(
        const level_t *level,
        const side_t *side,
        v2 offsets,
        side_segment_t *seg);

// like side_get_z_segment, but tests for an overlapping range
// returns range with "most overlap" if there are multiple
bool side_get_z_range_segment(
    const level_t *level,
    const side_t *side,
    v2 point,
    f32 z_min,
    f32 z_max,
    side_segment_t *seg);

// get u 0..1 value of point on side
f32 side_point_u(const side_t *side, v2 point);

// convert u in 0..1 to point on this side
v2 side_u_to_point(const side_t *side, f32 u);

// convert x in 0..side length to point on this side
v2 side_x_to_point(const side_t *side, f32 x);

// get side segment z-range at specific "u" (0..1) value
rangef_t side_segment_zs_at(const side_segment_t *seg, f32 u);

// get z bounds for this side
void side_z_bounds(
    const level_t *level,
    const side_t *side,
    f32 *zbl,
    f32 *zbr,
    f32 *ztl,
    f32 *ztr);

// get z bounds for side u point
rangef_t side_z_bounds_for_u(
    const level_t *level, const side_t *side, f32 u);

// compute "side relative" coordinates for 3D world coordinates
// if not on side already, coordinates are clamped
// these are the coordinates that decals use
v2 side_coords_to_relative(const side_t *side, v3 coords);
