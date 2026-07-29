#pragma once

#include "defs.h"

// get angle between two portal sides
// rotate directions by +angle to go from entry to exit
f32 portal_angle(
    level_t *level,
    const side_t *entry,
    const side_t *exit);

// transform point as travelling between two portals
v2 portal_transform(
    level_t *level,
    const side_t *entry,
    const side_t *exit,
    v2 p);

// transform 3D point as travelling between two portals
v3 portal_transform_3d(
    level_t *level,
    const side_t *entry,
    const side_t *exit,
    v3 p);

// get z difference between two portals at the specified contact point
f32 portal_relative_z(
    level_t *level,
    const side_t *entry,
    const side_t *exit_,
    v2 point);

// get z difference between two portals, not taking slopes into account
f32 portal_relative_z_floor(
    level_t *level,
    const side_t *entry,
    const side_t *exit_);
