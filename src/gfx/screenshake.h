#pragma once

#include "util/math.h"

// screenshake descriptor
typedef struct {
    // start tick (DO NOT MANUALLY SPECIFY!)
    i64 start_ns;

    // screenshake direction (DO NOT MANUALLY SPECIFY!)
    v3 d;

    // duration of screen shake, in seconds
    f32 duration;

    // power of screenshake
    f32 strength;

    // true if screenshake has location in 3D space (pos != 0)
    // DO NOT MANUALLY SPECIFY
    bool is_3d;

    // pos in 3D space for camera-relative screenshakes
    v3 pos;

    // distance at which to apply 3D shaek
    f32 dist;

    // optional - modifier to rate at which shake is sampled
    f32 rate;

    // optional - affects sampling of random directions on all 3 axes
    // (sign should be positive)
    v3 amplitude;
} screenshake_t;

void screenshake_add(const screenshake_t *desc);

v3 screenshake_sample_all(v3 cam_pos);

void screenshake_clear();
