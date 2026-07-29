#pragma once

#include "util/types.h"
#include "util/macros.h"
#include "util/math/util.h"

typedef union rangef {
    struct { f32 z0, z1; };
    struct { f32 min, max; };
    struct { f32 lo, hi; };
    f32 zs[2];
} rangef_t;

#define PRIrangef "c%.3f, %.3f)"
#define FMTrangef(r) '(', (r).z0, (r).z1

M_INLINE rangef_t rangef_from(f32 a, f32 b) {
    return (rangef_t) { .lo = min(a, b), .hi = max(a, b) };
}

// lerp in rangef by t
M_INLINE f32 rangef_lerp(rangef_t r, f32 t) {
    return lerp(r.z0, r.z1, t);
}

// clamp to be within rangef
M_INLINE f32 rangef_clamp(rangef_t r, f32 t) {
    return clamp(t, r.z0, r.z1);
}

// true if f is in range
M_INLINE bool rangef_contains(rangef_t r, f32 f) {
    return f >= r.z0 && f <= r.z1;
}

// get overlap of two ranges
M_INLINE bool rangef_overlap(rangef_t a, rangef_t b, rangef_t *overlap) {
    if (b.min > a.max || a.min > b.max) {
        return false;
    }

    if (overlap) {
        *overlap = (rangef_t) {
            .z0 = max(a.min, b.min),
            .z1 = min(a.max, b.max),
        };
    }

    return true;
}
