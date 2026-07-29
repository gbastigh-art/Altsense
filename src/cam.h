#pragma once

#include "util/types.h"

typedef struct frustum_3d frustum_3d_t;

// returns linear combination of camera dir, right, and up vectors
v3 cam_dir_right_up(f32 cd, f32 cr, f32 cu);

// convert world position "pos" to clamped screen pixel with specified frustum
v2 world_pos_to_clamped_pixel(
        const m4 *view_proj,
        const frustum_3d_t *frustum,
        v2i size,
        v3 pos);

// convert world position to pixel, returns false if world position is not on
// screen
bool world_pos_to_pixel(
        const m4 *view_proj,
        const frustum_3d_t *frustum,
        v2i size,
        v3 pos,
        v2 *out);

// get near plane corners in world space
// 4 points, clockwise form bottom left
void cam_near_plane_world_space(const m4 *inv_view_proj, v3 *points);

// convert pixel location -> world space/on near plane
v3 cam_pixel_to_near_plane_world_space(
        const m4 *inv_view_proj,
        v2 pixel,
        v2i size);

// get directional ray from screen pixel
ray3f_t cam_ray_from_pixel(const m4 *inv_view_proj, v3 pos, v2 pixel, v2i size);
