#include "level/portal.h"
#include "level/level_types.h"
#include "level/side.h"
#include "level/wall.h"

f32 portal_angle(
        level_t *level,
        const side_t *entry,
        const side_t *exit) {
    const v2
        na = side_normal(entry),
        nb = side_normal(exit);

    // compute signed angle between -nb, na
    return atan2f(-nb.y, -nb.x) - atan2f(na.y, na.x);
}

v2 portal_transform(
        level_t *level,
        const side_t *entry,
        const side_t *exit,
        v2 p) {
    if (entry->wall == exit->wall) {
        // nothing to transform
        return p;
    }

    const f32 angle = portal_angle(level, entry, exit);
    const v2 ms[2] = {
        wall_midpoint(entry->wall),
        wall_midpoint(exit->wall),
    };

    // undo translation relative to portal, rotate by difference
    const v2 v =
        v2_rotate(
            v2_sub(p, ms[0]),
            angle);

    // translate relative to exit portal
    return v2_add(v, ms[1]);
}

v3 portal_transform_3d(
        level_t *level,
        const side_t *entry,
        const side_t *exit,
        v3 p) {
    const f32 z_diff = portal_relative_z_floor(level, entry, exit);
    return
        v3_of(
            portal_transform(level, entry, exit, v2_from(p)),
            p.z + z_diff);
}

f32 portal_relative_z(
        level_t *level,
        const side_t *entry,
        const side_t *exit_,
        v2 point) {
    const side_segments_t
        segs_entry = side_get_segments(level, entry),
        segs_exit = side_get_segments(level, exit_);

    ASSERT(segs_entry.middle.present);
    ASSERT(segs_exit.middle.present);

    vertex_t *vs_entry[2];
    side_get_vertices(entry, vs_entry);

    vertex_t *vs_exit[2];
    side_get_vertices(exit_, vs_exit);

    const v2 proj =
        point_project_segment(point, vs_entry[0]->pos, vs_entry[1]->pos);

    const f32 u =
        v2_norm(v2_sub(proj, vs_entry[0]->pos))
            / entry->wall->len;

    // use 1 - u for exit as it is opposite entry side
    return
        lerp(segs_exit.middle.zbl, segs_exit.middle.zbr, 1.0f - u)
            - lerp(segs_entry.middle.zbl, segs_entry.middle.zbr, u);
}

f32 portal_relative_z_floor(
        level_t *level,
        const side_t *entry,
        const side_t *exit_) {
    return exit_->sector->floor.z - entry->sector->floor.z;
}
