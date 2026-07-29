#pragma once

#include "util/macros.h"
#include "util/types.h"
#include "util/mem.h"
#include "util/math/linalg.h"
#include "util/math/util.h"

// 2D line
typedef union line2 {
    struct { v2 a, b; };
    v2 points[2];
} line2f_t;

// 3D line
typedef union line3 {
    struct { v3 a, b; };
    v3 points[2];
} line3f_t;

typedef struct ray3 { v3 origin, dir; } ray3f_t; // 3D ray

// returns true if three points are colinear within the specified epsilon
M_INLINE bool points_are_colinear(v2 p0, v2 p1, v2 p2, f32 eps) {
    return
        fabsf(
            ((p1.y - p0.y) * (p2.x - p1.x))
                - ((p2.y - p1.y) * (p1.x - p0.x)))
            < eps;
}

// returns true if segments p1 -> p2 and p3 -> p4 are colinear within the
// specified epsilon
M_INLINE bool segments_are_colinear(
        v2 p1,
        v2 p2,
        v2 p3,
        v2 p4,
        f32 eps) {
    const f32
        p2x_m_p1x = p2.x - p1.x,
        p2y_m_p1y = p2.y - p1.y;

    // use same principle as colinear points, just precompute a bit
    return
        fabsf(
            ((p1.y - p3.y) * p2x_m_p1x)
                - (p2y_m_p1y * (p1.x - p3.x))) < eps
        && fabsf(
            ((p1.y - p4.y) * p2x_m_p1x)
                - (p2y_m_p1y * (p1.x - p4.x))) < eps;
}

// true if p is in triangle a, b, c
M_INLINE bool point_in_triangle(v2 p, v2 a, v2 b, v2 c) {
    const f32 d = ((b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y));
    if (fabsf(d) < 0.000001f) { return false; }

    const f32 x = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / d;
    if (x < 0) { return false; }

    const f32 y = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / d;
    if (y < 0) { return false; }

    const f32 z = 1 - x - y;
    return z > 0;
}

// get area of triangle formed by 3 lines
M_INLINE f32 triangle_area(v2 a, v2 b, v2 c) {
    // see https://math.stackexchange.com/questions/901819
    const m2 mat = {
        .m00 = (b.x - a.x), .m10 = (b.y - a.y),
        .m01 = (c.x - a.x), .m11 = (c.y - a.y),
    };

    return 0.5f * m2_det(mat);
}

// <0 right, 0 on, >0 left
M_INLINE f32 point_side(v2 p, v2 a, v2 b) {
    return -(((p.x - a.x) * (b.y - a.y)) - ((p.y - a.y) * (b.x - a.x)));
}

// get t on _a -> _b where _p is closest
M_INLINE f32 point_project_line_t(v2 p, v2 a, v2 b) {
    const f32 l2 = v2_norm2(v2_sub(a, b));
    if (l2 < 0.000001f) { return 0.0f; }
    const f32 t =
        v2_dot(
            (v2_of(p.x - a.x, p.y - a.y)),
            (v2_of(b.x - a.x, b.y - a.y))) / l2;
    return t;
}

// project point _p onto line segment _a -> _b
M_INLINE v2 point_project_segment(v2 p, v2 a, v2 b) {
    const v2 a_to_b = v2_sub(b, a);
    const f32 l2 = v2_norm2(a_to_b);
    if (l2 < 0.00000001f) { return a; }
    const v2 a_to_p = v2_sub(p, a);
    const f32 t = clamp(v2_dot(a_to_p, a_to_b) / l2, 0.0f, 1.0f);
    return v2_add(a, v2_scale(a_to_b, t));
}

// project point _p onto line _a -> _b
M_INLINE v2 point_project_line(v2 p, v2 a, v2 b) {
    const f32 l2 = v2_norm2(v2_sub(a, b));
    if (l2 < 0.00000001f) { return a; }
    const f32 t =
        v2_dot(
            (v2_of(p.x - a.x, p.y - a.y)),
            (v2_of(b.x - a.x, b.y - a.y ))) / l2;
    return v2_add(a, v2_scale(v2_sub(b, a), t));
}

// distance from p to a -> b
M_INLINE f32 point_to_segment(v2 p, v2 a, v2 b) {
    return v2_norm(v2_sub(p, point_project_segment(p, a, b)));
}

// distance^2 from p to a -> b
M_INLINE f32 point_to_segment2(v2 p, v2 a, v2 b) {
    return v2_norm2(v2_sub(p, point_project_segment(p, a, b)));
}

// gives right side normal of line
M_INLINE v2 line_right_normal(v2 a, v2 b) {
    return v2_normalize(v2_of(b.y - a.y, -(b.x - a.x)));
}

// see: https://en.wikipedia.org/wiki/Line–line_intersection
// intersect two infinite lines
bool intersect_lines(
    v2 a0, v2 a1, v2 b0, v2 b1, v2 *hit);

// intersect two line segments, returns false if no intersection exists
bool intersect_segs(
    v2 a0, v2 a1, v2 b0, v2 b1, v2 *hit, f32 *ta, f32 *tb);

// intersect line segment s0 -> s1 with box defined by b0, b1
bool intersect_seg_box(
    v2 s0, v2 s1, v2 b0, v2 b1, v2 *hit);

// intersect segment a -> b with plane p
bool intersect_seg_plane(v3 a, v3 b, v4 p, v3 *hit, f32 *t);

// intersect ray r with plane p
bool intersect_ray_plane(ray3f_t r, v4 p, v3 *hit, f32 *t);

// intersect two planes to get a vector (point / direction)
bool intersect_planes(v4 a, v4 b, v3 *point, v3 *dir);

// compute point of intersection of 3 planes
v3 intersect_3_planes(v4 a, v4 b, v4 c);

// TODO: rewrite to return intersection points
// intersect circles (p0, r0) and (p1, r1)
bool intersect_circle_circle(v2 p0, f32 r0, v2 p1, f32 r1);

// sweep dynamic circle [pr]1 along v against static circle [pr]0
// [pr]0 are static, [pr]1 are dynamic along v
bool sweep_circle_circle(
    v2 p0, f32 r0, v2 p1, f32 r1, v2 v, f32 *t, v2 *resolved);

// sweep circle at p with radius r along vector d, collide line segment a -> b
bool sweep_circle_line_segment(
    v2 p,
    f32 r,
    v2 d,
    v2 a,
    v2 b,
    f32 *t_circle,
    f32 *t_segment,
    v2 *resolved);

// intersect circle (p, r) with segment (s0, s1)
// returns number of intersection points (up to 2) and resolved circle pos
int intersect_circle_seg(
    v2 p,
    f32 r,
    v2 s0,
    v2 s1,
    f32 *ts,
    v2 *p_resolved);

// intersect line with sphere, returns number of intersections
int intersect_line_sphere(
    v3 a, v3 b, v3 center, f32 r, v3 ps[2]);

typedef struct circle_triangle_intersect {
    bool circle_contains;
    bool triangle_contains;
    int n;
    v2 hits[6];
} circle_triangle_intersect_t;

// intersect circle (p, r) with triangle (a, b, c)
// returns up to 6 intersection points
bool intersect_circle_triangle(
    v2 p, f32 r, v2 a, v2 b, v2 c,
    circle_triangle_intersect_t *out);

// intersect two triangles (a, b, c) and (p, q, r)
bool intersect_triangle_triangle(
    v2 a, v2 b, v2 c, v2 p, v2 q, v2 r);

// returns true if ts makes a hole inside of vs
bool polygon_is_hole(
    const line2f_t *vs,
    int n_vs,
    const line2f_t *ts,
    int n_ts);

// check if point p is in polygon specified by n lines in vs
bool polygon_contains_point(v2 p, const v2 lines[][2], int n);

// get the line (p_l0, p_l1) between the closest points of the two line segments
line2f_t segments_closest_line(line2f_t l0, line2f_t l1);

// computes a convex hull from the specified points
// n_points must be >= 3
// returned lines have right side normal pointing towards convex center
void convex_hull_from_points(
        allocator_t *tmp,
        const v2 *points,
        int n_points,
        DYNLIST(line2f_t) *out);

// project point onto closest polygon edge
// assumes lines make up a convex (right side pointing inwards) polygon
v2 convex_poly_project_onto_edge(line2f_t *lines, int n, v2 p);

#ifdef UTIL_IMPL

#include "util/assert.h"
#include "util/dynlist.h"

bool intersect_lines(
    v2 a0, v2 a1, v2 b0, v2 b1, v2 *hit) {
    const f32 d =
        ((a0.x - a1.x) * (b0.y - b1.y)) - ((a0.y - a1.y) * (b0.x - b1.x));

    // check for colinearity
    if (fabsf(d) < 0.000001f) { return false; }

    const f32
        xl0 = v2_cross(a0, a1),
        xl1 = v2_cross(b0, b1);

    if (hit) {
        *hit = v2_of(
            ((xl0 * (b0.x - b1.x)) - ((a0.x - a1.x) * xl1)) / d,
            ((xl0 * (b0.y - b1.y)) - ((a0.y - a1.y) * xl1)) / d
        );
    }

    return true;
}

bool intersect_segs(
        v2 a0,
        v2 a1,
        v2 b0,
        v2 b1,
        v2 *hit,
        f32 *ta,
        f32 *tb) {
    const f32 d =
        ((a0.x - a1.x) * (b0.y - b1.y)) - ((a0.y - a1.y) * (b0.x - b1.x));

    // check for colinearity
    if (M_UNLIKELY(fabsf(d) < 0.00001f)) { return false; }

    const f32 inv_d = 1.0f / d;
    const f32 a0x_sub_b0x = a0.x - b0.x;
    const f32 a0y_sub_b0y = a0.y - b0.y;
    const f32 t =
        (((a0x_sub_b0x) * (b0.y - b1.y)) - ((a0y_sub_b0y) * (b0.x - b1.x)))
            * inv_d;

    if (t < 0 || t > 1) { return false; }

    const f32 u =
        (((a0x_sub_b0x) * (a0.y - a1.y)) - ((a0y_sub_b0y) * (a0.x - a1.x)))
            * inv_d;

    if (u < 0 || u > 1) { return false; }

    if (hit) {
        *hit =
            v2_of(
                a0.x + (t * (a1.x - a0.x)),
                a0.y + (t * (a1.y - a0.y)));
    }

    if (ta) { *ta = t; }
    if (tb) { *tb = u; }

    return true;
}

bool intersect_seg_box(
    v2 s0, v2 s1, v2 b0, v2 b1, v2 *hit) {
    if (b0.x > b1.x) { swap(b0.x, b1.x); }
    if (b0.y > b1.y) { swap(b0.y, b1.y); }

    v2 r;
    if (intersect_segs(
            s0, s1,
            (v2_of(b0.x, b0.y)), (v2_of(b0.x, b1.y)),
            &r, NULL, NULL)) {
        if (hit) { *hit = r; }
        return true;
    } else if (
        intersect_segs(
            s0, s1,
            (v2_of(b0.x, b1.y)), (v2_of(b1.x, b1.y)),
            &r, NULL, NULL)) {
        if (hit) { *hit = r; }
        return true;
    } else if (
        intersect_segs(
            s0, s1,
            (v2_of(b1.x, b1.y)), (v2_of(b1.x, b0.y)),
            &r, NULL, NULL)) {
        if (hit) { *hit = r; }
        return true;
    } else if (
        intersect_segs(
            s0, s1,
            (v2_of(b1.x, b0.y)), (v2_of(b0.x, b0.y)),
            &r, NULL, NULL)) {
        if (hit) { *hit = r; }
        return true;
    }

    return false;
}

bool intersect_seg_plane(v3 a, v3 b, v4 p, v3 *hit, f32 *t) {
    const f32 sa = plane_classify(p, a), sb = plane_classify(p, b);

    if (sign(sa) == sign(sb)) {
        return false;
    }

    if (hit || t) {
        const v3
            c = plane_coplanar_point(p),
            r = v3_normalize(v3_sub(b, a));
        const f32 u =
            (v3_dot(v3_from(p), c) - v3_dot(v3_from(p), a))
                / v3_dot(v3_from(p), r);
        const v3 h = v3_add(a, v3_scale(r, u));
        if (hit) { *hit = h; }
        if (t) { *t = v3_norm(v3_sub(h, a)) / v3_norm(v3_sub(b, a)); }
    }

    return true;
}

bool intersect_ray_plane(ray3f_t r, v4 p, v3 *hit, f32 *t) {
    const v3 n = v3_from(p);
    const f32 denom = v3_dot(n, r.dir);
    if (fabsf(denom) < 0.00001f) {
        return false;
    }

    const v3 c = plane_coplanar_point(p);
    const f32 u = v3_dot(v3_sub(c, r.origin), n) / denom;
    if (u < 0) { return false; }

    if (t) { *t = u; }
    if (hit) { *hit = v3_add(r.origin, v3_scale(r.dir, u)); }
    return true;
}

bool intersect_planes(v4 a, v4 b, v3 *point, v3 *dir) {
    const v3 d = v3_cross(v3_from(a), v3_from(b));

    const f32 det = v3_norm2(d);
    if (det < 0.000001f) {
        // parallel
        return false;
    }

    if (dir) {
        *dir = d;
    }

    if (point) {
        *point =
            v3_divs(
                v3_add(
                    v3_scale(v3_cross(d, v3_from(b)), a.w),
                    v3_scale(v3_cross(v3_from(a), d), b.w)),
                det);
    }

    return true;
}

v3 intersect_3_planes(v4 a, v4 b, v4 c) {
    const v3
        n1 = v3_from(a),
        n2 = v3_from(b),
        n3 = v3_from(c),
        x1 = plane_coplanar_point(a),
        x2 = plane_coplanar_point(b),
        x3 = plane_coplanar_point(c),
        f1 = v3_scale(v3_cross(n2, n3), v3_dot(x1, n1)),
        f2 = v3_scale(v3_cross(n3, n1), v3_dot(x2, n2)),
        f3 = v3_scale(v3_cross(n1, n2), v3_dot(x3, n3));

    m3 m;
    m.col[0] = n1;
    m.col[1] = n2;
    m.col[2] = n3;
    const f32 det = m3_det(m);

    const v3 sum = v3_add(f1, v3_add(f2, f3));
    return v3_divs(sum, det);
}

bool intersect_circle_circle(v2 p0, f32 r0, v2 p1, f32 r1) {
    // check if distance between centers is less than cumulative radius
    return v2_distance2(p0, p1) <= powf(r0 + r1, 2.0f);
}

bool sweep_circle_circle(
        v2 p0,
        f32 r0,
        v2 p1,
        f32 r1,
        v2 v,
        f32 *t,
        v2 *resolved) {

    const v2
        a = p1,
        b = v2_add(p1, v),
        d = point_project_segment(p0, a, b);

    const f32
        total_radius = r0 + r1,
        total_radius2 = total_radius * total_radius;

    if (v2_norm2(v2_sub(p0, d)) > total_radius2) {
        return false;
    }

    if (!t && !resolved) { return true; }

    // TODO: 1.0f - t ????? something is deeply wrong here
    const f32 t_hit =
        1.0f - (v2_norm(v2_sub(d, a)) / v2_norm(v2_sub(b, a)));

    const v2 p0_to_p1 = v2_sub(p1, p0);
    if (v2_norm(p0_to_p1) < total_radius) {
        // inside
        if (t) {
            *t = 0.0f;
        }

        if (resolved) {
            // circles are inside of each other, move p1 back such that distance
            // is exactly r0 + r1
            const v2 d = v2_normalize(p0_to_p1);
            *resolved = v2_add(p0, v2_scale(d, total_radius));
        }
    } else {
        if (t) {
            *t = t_hit;
        }

        if (resolved) {
            *resolved = v2_lerp(a, b, t_hit);
        }
    }

    return true;
}

bool sweep_circle_line_segment(
        v2 p,
        f32 r,
        v2 d,
        v2 a,
        v2 b,
        f32 *t_circle,
        f32 *t_segment,
        v2 *resolved) {
    const f32 t = satf(point_project_line_t(p, a, b));
    const v2 closest = v2_lerp(a, b, t);

    const v2 closest_to_p = v2_sub(p, closest);
    const f32 dist_sqr = v2_norm2(closest_to_p);

    const v2 normal = v2_normalize(v2_sub(closest, p));

    // circle is already touching line
    if (dist_sqr < r * r) {
        *t_circle = 0.0f;
        *t_segment = t;
        *resolved = v2_add(closest, v2_scale(normal, -r));
        return true;
    }

    // circle pos at end
    const v2 q = v2_add(p, d);

    // TODO: sort of a hack?
    // when the circle moves from p_circle to p_circle_end, does it come within
    // radius of the segment?
    const line2f_t ab_pq_seg =
        segments_closest_line(
            (line2f_t) { .a = a, .b = b },
            (line2f_t) { .a = p, .b = q });

    const f32 len_pq_ab_seg2 = v2_norm2(v2_sub(ab_pq_seg.b, ab_pq_seg.a));

    if (len_pq_ab_seg2 < 0.000001f) {
        // lines intersect directly at some point - find circle center "x" at
        // that point where distance to (a -> b) == r and circle center is
        // between p and q
        const v2
            contact_point = ab_pq_seg.b,
            dir_circle = v2_normalize(d),
            x = v2_add(contact_point, v2_scale(dir_circle, -r));

        *t_segment = v2_norm(v2_sub(contact_point, a)) / v2_norm(v2_sub(b, a));
        *t_circle = v2_norm(v2_sub(x, p)) / v2_norm(d);
        *resolved = x;
        return true;
    }

    // check for direct line vs. line collision
    if (intersect_segs(
            a,
            b,
            p,
            q,
            NULL,
            NULL,
            NULL)) {
        const f32
            dot_start = v2_dot(v2_sub(a, p), d),
            dot_end = v2_dot(v2_sub(b, p), d);

        v2 endpoint;
        f32 ts;

        if (dot_start < 0.0f) {
            if (dot_end < 0.0f) {
                return false;
            }

            ts = 1.0f;
            endpoint = b;
        } else if (dot_end < 0.0f) {
            ts = 0.0f;
            endpoint = a;
        } else {
            endpoint = dot_start < dot_end ? a : b;
            ts = dot_start < dot_end ? 0.0f : 1.0f;
        }

        // collide with the endpoint, a 0-radius circle
        if (intersect_circle_circle(p, r, endpoint, 0.0f)) {
            *t_circle = 0.0f;
            *t_segment = ts;
            *resolved = p;
            return true;
        }

        return false;
    }

    // circle has swept collision with line, but it may stop directly on the it
    const v2 v_to_line = v2_proj(d, normal);

    const f32
        t_to_line = v2_norm(v_to_line),
        needed_t_to_line = sqrtf(dist_sqr) - r;

    // check if the actual travel (t_to_line) is less than the necessary travel
    // to touch
    if (t_to_line < needed_t_to_line) {
        return false;
    }

    // circle intersects line, not necessarily segment
    const f32
        t_hit = needed_t_to_line / t_to_line,
        t_end = point_project_line_t(q, a, b);

    // find distance along line segment at the point of intersection
    const f32 t_int = lerp(t, t_end, t_hit);

    // point is within segment bounds and circle is moving towards line
    if (t_int >= 0.0f
        && t_int <= 1.0f
        && v2_dot(v_to_line, v2_sub(closest, p)) >= 0.0f) {
        *t_circle = t_hit;
        *t_segment = t_int;
        *resolved = v2_add(p, v2_scale(d, t_hit));
        return true;
    }

    // may have hit an endpoint
    const v2 endpoint = t > 1.0f ? b : a;

    if (intersect_circle_circle(p, r, endpoint, 0.0f)) {
        *t_circle = t_hit;
        *t_segment = clamp(t, 0.0f, 1.0f);
        *resolved = v2_add(p, v2_scale(d, t_hit));
        return true;
    }

    return false;
}

int intersect_circle_seg(
        v2 p,
        f32 r,
        v2 s0,
        v2 s1,
        f32 *ts,
        v2 *p_resolved) {
    int n = 0;

    const f32 r2 = r * r;

    if (v2_distance2(s0, p) < r2) {
        if (ts) { ts[n] = 0.0f; }
        n++;
    }

    if (v2_distance2(s1, p) < r2) {
        if (ts) { ts[n] = 1.0f; }
        n++;
    }

    // see stackoverflow.com/questions/1073336
    const v2
        d = v2_of(s1.x - s0.x, s1.y - s0.y),
        f = v2_of(s0.x - p.x, s0.y - p.y);

    const f32
        a = v2_dot(d, d),
        b = 2.0f * v2_dot(f, d),
        c = v2_dot(f, f) - (r * r),
        q2 = (b * b) - (4 * a * c);

    if (q2 < 0) {
        // discriminant^2 < 0, no intersection
        return 0;
    }

    const f32 q = sqrtf(q2);
    const f32 inv_2a = 1.0f / (2.0f * a);
    const f32 t0 = (-b - q) * inv_2a;
    const f32 t1 = (-b + q) * inv_2a;

    if (t0 >= 0 && t0 <= 1) {
        ASSERT_DEBUG(n < 2);
        if (ts) { ts[n] = t0; }
        n++;
    }

    if (t1 >= 0 && t1 <= 1) {
        ASSERT_DEBUG(n < 2);
        if (ts) { ts[n] = t1; }
        n++;
    }

    if (n == 2 && ts && ts[0] > ts[1]) {
        swap(ts[0], ts[1]);
    }

    if (n != 0 && p_resolved) {
        // resolve circle
        const v2 q = point_project_segment(p, s0, s1);
        const v2 q_to_p = v2_sub(p, q);
        const v2 d_q_to_p = v2_normalize(q_to_p);

        *p_resolved =
            v2_add(
                p,
                v2_scale(
                    d_q_to_p,
                    r - v2_norm(q_to_p)));
    }

    return n;
}

int intersect_line_sphere(
    v3 a, v3 b, v3 center, f32 r, v3 ps[2]) {
    const v3 c = center, p = a, v = v3_sub(b, a);

    const f32
        A = v3_dot(v, v),
        B = 2.0 * (p.x * v.x + p.y * v.y + p.z * v.z - v.x * c.x - v.y * c.y - v.z * c.z),
        C = p.x * p.x - 2 * p.x * c.x + c.x * c.x + p.y * p.y - 2 * p.y * c.y + c.y * c.y +
               p.z * p.z - 2 * p.z * c.z + c.z * c.z -  r * r;

    // discriminant
    const f32 dsc = B * B - 4 * A * C;

    if (dsc < 0) {
        return 0;
    }

    const f32
        sqrt_dsc = sqrtf(dsc),
        inv_two_a = 1.0f / (2.0f * A);

    const f32 t0 = (-B - sqrt_dsc) * inv_two_a;
    const v3 p0 = v3_lerp(a, b, t0);

    if (fabsf(dsc) < 0.000001f) {
        if (ps) { ps[0] = p0; ps[1] = v3_of(0); }
        return 1;
    }

    const f32 t1 = (-B + sqrt_dsc) * inv_two_a;
    const v3 p1 = v3_lerp(a, b, t1);

    if (ps) { ps[0] = p0; ps[1] = p1; }
    return 2;
}

bool intersect_circle_triangle(
    v2 p, f32 r, v2 a, v2 b, v2 c,
    circle_triangle_intersect_t *out) {
    if (out) { *out = (circle_triangle_intersect_t) { 0 }; }

    const v2 points[3] = { a, b, c };

    const f32 r_squared = r * r;

    // check if all points are inside of circle
    if (v2_distance2(p, a) <= r_squared
        && v2_distance2(p, b) <= r_squared
        && v2_distance2(p, c) <= r_squared) {
        if (out) { out->circle_contains = true; }
        return true;
    }

    // check if circle is contained in triangle by projecting circle center
    // onto each triangle side, if the distance is greater than the radius then
    // the triangle contains the circle
    bool triangle_contains = true;
    for (int i = 0; i < 3 && triangle_contains; i++) {
        if (point_to_segment(p, points[i], points[(i + 1) % 3]) > r) {
            triangle_contains = false;
        }
    }

    if (triangle_contains) {
        if (out) { out->triangle_contains = true; }
        return true;
    }

    // check for intersections between triangle lines and circle
    int hits = 0;
    for (int i = 0; i < 3; i++) {
        const v2 p0 = points[i], p1 = points[(i + 1) % 3];

        f32 ts[2];
        const int n =
            intersect_circle_seg(
                p,
                r,
                p0,
                p1,
                ts,
                NULL);

        if (n == 0) {
            continue;
        } else if (!out) {
            // quick return: there is an intersection but we don't need out info
            return true;
        } else {
            // track points into output
            for (int j = 0; j < n; j++) {
                out->hits[hits++] = v2_lerp(p0, p1, ts[j]);
            }
        }
    }

    if (out) { out->n = hits; }

    return hits > 0;
}

static bool vertices_contains(v2 needle, v2 *haystack, int n) {
    for (int i = 0; i < n; i++) {
        if (v2_eqv_eps(needle, haystack[i])) { return true; }
    }
    return false;
}

bool polygon_is_hole(
    const line2f_t *vs,
    int n_vs,
    const line2f_t *ts,
    int n_ts) {
    // ts form an inner hole iff the points directly next to them are all inside
    // of the polygon formed by { vs, ts }

    for (int i = 0; i < n_ts; i++) {
        const v2 p0 = ts[i].a, p1 = ts[i].b;

        // compute q, point on normal of p0 -> p1 (right side normal!)
        const v2
            normal = v2_normalize(v2_of(p1.y - p0.y, -(p1.x - p0.x))),
            midpoint = v2_lerp(p0, p1, 0.5f),
            q =
                v2_of(
                    midpoint.x + 0.01f * normal.x,
                    midpoint.y + 0.01f * normal.y);

        // TODO: not for big polygons
        // compute a point such that q->u is a point outside of the polygon
        const v2 u = v2_of(q.x - 1e6f, q.y);

        // hit vertices
        v2 hits[n_vs + n_ts];

        // do a point-in-polygon test with q -> u
        int n_hits = 0;

        // test ts
        for (int j = 0; j < n_ts; j++) {
            v2 hit;

            if (segments_are_colinear(q, u, ts[j].a, ts[j].b, 0.01f)) {
                if (!vertices_contains(ts[j].a, hits, n_hits)) {
                    hits[n_hits++] = ts[j].a;
                }

                if (!vertices_contains(ts[j].b, hits, n_hits)) {
                    hits[n_hits++] = ts[j].b;
                }
            } else if (
                intersect_segs(q, u, ts[j].a, ts[j].b, &hit, NULL, NULL)) {
                if (!vertices_contains(hit, hits, n_hits)) {
                    hits[n_hits++] = hit;
                }
            }
        }

        // test vs
        for (int j = 0; j < n_vs; j++) {
            v2 hit;
            if (segments_are_colinear(q, u, vs[j].a, vs[j].b, 0.01f)) {
                if (!vertices_contains(vs[j].a, hits, n_hits)) {
                    hits[n_hits++] = vs[j].a;
                }

                if (!vertices_contains(vs[j].b, hits, n_hits)) {
                    hits[n_hits++] = vs[j].b;
                }
            } else if (
                intersect_segs(q, u, vs[j].a, vs[j].b, &hit, NULL, NULL)) {
                if (!vertices_contains(hit, hits, n_hits)) {
                    hits[n_hits++] = hit;
                }
            }
        }

        // even number of intersections -> outside of polygon, polygon cannot
        // be hole
        if (n_hits % 2 == 0) {
            return false;
        }
    }

    return true;
}

bool polygon_contains_point(v2 p, const v2 lines[][2], int n) {
    // TODO: not for big polygons
    // compute a point such that p -> u is a point outside of the polygon
    const v2 q = v2_of(p.x - 1e6f, p.y);

    int n_hits = 0;
    v2 hits[n * 2];

    for (int i = 0; i < n; i++) {
        v2 hit;
        if (segments_are_colinear(p, q, lines[i][0], lines[i][1], 0.01f)) {
            for (int j = 0; j < 2; j++) {
                if (!vertices_contains(lines[i][j], hits, n_hits)) {
                    hits[n_hits++] = lines[i][j];
                }
            }
        } else if (
            intersect_segs(p, q, lines[i][0], lines[i][1], &hit, NULL, NULL)) {
            if (!vertices_contains(hit, hits, n_hits)) {
                hits[n_hits++] = hit;
            }
        }
    }

    // odd number of hits -> inside polygon
    return n_hits % 2 != 0;
}

#define itt_orient(p0, p1, p2) (-point_side(p0, p2, p1))

M_INLINE bool itt_test_vertex(
    v2 a, v2 b, v2 c, v2 p, v2 q, v2 r) {
    if (itt_orient(r, p, b) >= 0.0f) {
        if (itt_orient(r, q, b) <= 0.0f) {
            if (itt_orient(a, p, b) > 0.0f) {
                return itt_orient(a, q, b) <= 0.0f;
            } else {
                return
                    itt_orient(a, p, c) >= 0.0f
                    && itt_orient(b, c, p) >= 0.0f;
            }
        } else {
            return
                itt_orient(a, q, b) <= 0.0f
                && itt_orient(r, q, c) <= 0.0f
                && itt_orient(b, c, q) >= 0.0f;
        }
    } else {
        if (itt_orient(r, p, c) >= 0.0f) {
            if (itt_orient(b, c, r) >= 0.0f) {
                return itt_orient(a, p, c) >= 0.0f;
            } else {
                return
                    itt_orient(b, c, q) >= 0.0f
                    && itt_orient(r, c, q) >= 0.0f;
            }
        } else {
            return false;
        }
    }
}

M_INLINE bool itt_test_edge(
    v2 a, v2 b, v2 c, v2 p, v2 q, v2 r) {
    if (itt_orient(r, p, b) >= 0.0f) {
        if (itt_orient(a, p, b) >= 0.0f) {
            if (itt_orient(a, b, r) >= 0.0f) {
                return 1;
            } else {
                return 0;
            }
        } else {
            if (itt_orient(b, c, p) >= 0.0f) {
                if (itt_orient(c, a, p) >= 0.0f) {
                    return 1;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }
        }
    } else {
        if (itt_orient(r, p, c) >= 0.0f) {
            if (itt_orient(a, p, c) >= 0.0f) {
                if (itt_orient(a, c, r) >= 0.0f) {
                    return 1;
                } else {
                    if (itt_orient(b, c, r) >= 0.0f) {
                        return 1;
                    } else {
                        return 0;
                    }
                }
            } else {
                return 0;
            }
        } else {
            return 0;
        }
    }
}

bool intersect_triangle_triangle(
    v2 a, v2 b, v2 c, v2 p, v2 q, v2 r) {
    if (itt_orient(p,q,a) >= 0.0f) {
        if (itt_orient(q,r,a) >= 0.0f) {
            if (itt_orient(r,p,a) >= 0.0f) {
                return true;
            } else {
                return itt_test_edge(a,b,c,p,q,r);
            }
        } else {
            if (itt_orient(r,p,a) >= 0.0f)  {
                return itt_test_edge(a,b,c,r,p,q);
            } else {
                return itt_test_vertex(a,b,c,p,q,r);
            }
        }
    } else {
        if (itt_orient(q,r,a) >= 0.0f) {
            if (itt_orient(r,p,a) >= 0.0f)  {
                return itt_test_edge(a,b,c,q,r,p);
            } else {
                return itt_test_vertex(a,b,c,q,r,p);
            }
        } else return itt_test_vertex(a,b,c,r,p,q);
    }
}

line2f_t segments_closest_line(line2f_t l0, line2f_t l1) {
    // TODO: clean this up
    // from math.stackexchange.com/questions/846054
    const v2
        P1 = l0.a,
        P2 = l1.a,
        V1 = v2_sub(l0.b, l0.a),
        V2 = v2_sub(l1.b, l1.a),
        V21 = v2_sub(P2, P1);

    const f32
        v22 = v2_dot(V2, V2),
        v11 = v2_dot(V1, V1),
        v21 = v2_dot(V2, V1),
        v21_1 = v2_dot(V21, V1),
        v21_2 = v2_dot(V21, V2),
        denom = (v21 * v21) - (v22 * v11);

    f32 s, t;
    if (fabsf(denom) < 0.000001f) {
        s = 0.0f;
        t = ((v11 * s) - v21_1) / v21;
    } else {
        s = ((v21_2 * v21) - (v22 * v21_1)) / denom;
        t = ((-v21_1 * v21) + (v11 * v21_2)) / denom;
    }

    s = satf(s);
    t = satf(t);

    return (line2f_t) {
        .a = v2_add(P1, v2_scale(V1, s)),
        .b = v2_add(P2, v2_scale(V2, t)),
    };
}

// z-value of cross product of ab and ac
// result >0 -> ccw, <0 -> cw, ==0 -> colinear
static f32 ccw_cross(v2 a, v2 b, v2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static int cmp_v2_lexicographic(const void *a, const void *b, void*) {
    const v2 pa = *(v2*) a, pb = *(v2*) b;
    const bool lt = pa.x < pb.x || (fabsf(pa.x - pb.x) < 0.0000001f && pa.y < pb.y);
    return lt ? -1 : 1;
}

void convex_hull_from_points(
        allocator_t *tmp,
        const v2 *points_in,
        int n_points_in,
        DYNLIST(line2f_t) *out) {
    ASSERT(n_points_in >= 3);

    dynlist_resize(*out, 0);

    if (n_points_in == 3) {
        const v2 a = points_in[0], b = points_in[1], c = points_in[2];
        const v2 center = v2_divs(v2_add(a, v2_add(b, c)), 3);

        *dynlist_push(*out) = (line2f_t) { .a = a, .b = b };
        *dynlist_push(*out) = (line2f_t) { .a = b, .b = c };
        *dynlist_push(*out) = (line2f_t) { .a = c, .b = a };

        // fix orientation so center is on right side
        dynlist_each(*out, it) {
            if (point_side(center, it.el->a, it.el->b) > 0) {
                swap(it.el->a, it.el->b);
            }
        }

        return;
    }

    // copy and sort points
    DYNLIST(v2) points = dynlist_create(v2, tmp);
    dynlist_resize(points, n_points_in);
    memcpy(points, points_in, n_points_in * sizeof(points[0]));
    dynlist_sort(points, cmp_v2_lexicographic, NULL);

    int n = dynlist_size(points);
    int k = 0;

    DYNLIST(v2) hull = dynlist_create(v2, tmp, n * 2);
    dynlist_resize(hull, n * 2);

    // lower hull
    for (int i = 0; i < n; i++) {
        while (k >= 2
               && ccw_cross(hull[k - 2], hull[k - 1], points[i]) <= 0) {
            k--;
        }

        hull[k++] = points[i];
    }

    // upper hull
    for (int i = n - 1, t = k + 1; i >= 0; i--) {
        while (k >= t
               && ccw_cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0) {
            k--;
        }

        hull[k++] = points[i - 1];
    }

    dynlist_resize(hull, k - 1);

    // make sure hull starts/ends with same point
    if (!v2_eqv(hull[0], hull[dynlist_size(hull) - 1])) {
        *dynlist_push(hull) = hull[0];
    }

    // create hull and merge colinear lines
    for (int i = 0, n = dynlist_size(hull) - 1; i < n; ) {
        int j = i + 1;
        while (
            j + 1 < n
            && points_are_colinear(
                hull[i],
                hull[j],
                hull[j + 1],
                0.001f)) {
            j++;
        }

        // NOTE: points are swapped so line right side points inwards
        *dynlist_push(*out) = (line2f_t) {
            .a = hull[j],
            .b = hull[i],
        };
        i = j;
    }

    dynlist_destroy(points);
    dynlist_destroy(hull);
}

v2 convex_poly_project_onto_edge(line2f_t *lines, int n, v2 p) {
    ASSERT_DEBUG(n >= 3);

    bool inside = true;

    v2 closest = lines[0].a;
    f32 d2_closest = 1e10f;

    for (int i = 0; i < n; i++) {
        if (point_side(p, lines[i].a, lines[i].b) > 0) {
            inside = false;

            const v2 proj = point_project_segment(p, lines[i].a, lines[i].b);
            const f32 d2 = v2_distance2(p, proj);
            if (d2 < d2_closest) {
                closest = proj;
                d2_closest = d2;
            }
        }
    }

    return inside ? p : closest;
}

#endif // ifdef UTIL_IMPL
