#include "level/side.h"
#include "level/block.h"
#include "level/level_types.h"
#include "level/level.h"
#include "level/sector.h"
#include "level/wall.h"
#include "level/decal.h"
#include "util/hash.h"

side_t *side_new(level_t *level, const side_t *like) {
    side_t *side = level_try_alloc(level, &level->sides);

    if (like) {
        side_copy_props(level, side, like);
    }

    return side;
}

void side_delete(level_t *level, side_t *side) {
    if (side->sector) {
        sector_remove_side(level, side->sector, side);
    }

    // remove portals to this side
    // optimization opportunity: don't traverse all sides to do this
    level_each(side_t, &level->sides, it) {
        side_t *other = it.el;
        if (other->portal == side) {
            other->portal = NULL;
            side_recalculate(level, other);
        }
    }

    // remove from wall
    if (side->wall) {
        wall_set_side(level, side->wall, side_index(side), NULL);
    }

    // remove all decals
    llist_each(node, &side->decals, it) {
        decal_delete(level, it.el);
    }

    // find sides "like" this side - replace their like with another side like
    // this one
    side_t *first_like = NULL;
    level_each(side_t, &level->sides, it) {
        side_t *like = side_get_like(it.el);
        if (like != side) { continue; }

        if (!first_like) {
            first_like = it.el;

            // this side is like itself now
            side_try_set_like(level, it.el, NULL);
        } else {
            // other sides are like the first one
            side_try_set_like(level, it.el, first_like);
        }
    }

    level_free(level, &level->sides, side);
}

sidemat_data_t side_get_mat(const side_t *side) {
    sidemat_data_t mat;

    const side_t *like = side_get_like(side);
    if (like) {
        mat = like->mat;

        if (side->mat.tex_overlay.index) {
            mat.tex_overlay = side->mat.tex_overlay;
            mat.overlay_alpha = side->mat.overlay_alpha;
        }
    } else {
        mat = side->mat;
    }

    return mat;
}

void side_copy_props(level_t *level, side_t *dst, const side_t *src) {
    dst->mat = src->mat;
    dst->light = src->light;
    side_try_set_like(level, dst, src->like);
}

bool side_try_set_like(const level_t *level, side_t *side, side_t *like) {
    ASSERT_DEBUG(side);

    // find "true" like
    while (like && like->like) {
        like = like->like;
    }

    if (like == side) {
        // loop
        return false;
    }

    side->like = like;
    side->version++;
    return true;
}

side_t *side_get_like(const side_t *side) {
    side_t *like = side->like;
    while (like && like->like) {
        like = like->like;
    }
    return like;
}

static hash_t compute_seg_hash(const level_t *level, side_t *side) {
    hash_t hash = 0x12345;

    const side_segments_t segs = side_get_segments(level, side);

    for (int i = 0; i < ARRLEN(segs.arr); i++) {
        hash = hash_add_bool(hash, segs.arr[i].present);

        if (segs.arr[i].present) {
            hash = hash_add_bool(hash, segs.arr[i].portal);
            hash = hash_add_bool(hash, segs.arr[i].mesh);
            hash = hash_add_f32(hash, segs.arr[i].zbl);
            hash = hash_add_f32(hash, segs.arr[i].zbr);
            hash = hash_add_f32(hash, segs.arr[i].ztl);
            hash = hash_add_f32(hash, segs.arr[i].ztr);
        }
    }

    return hash;
}

void side_recalculate(level_t *level, side_t *side) {
    if (side->lflags.do_not_recalc) { return; }
    side->lflags.recalc_enqueued = false;

    level->version++;
    side->version++;

    bool on_wall = false;
    for (int i = 0; i < 2; i++) {
        on_wall |= side->wall->sides[i] == side;
    }

    ASSERT(on_wall, "side references wall but wall does not have side");

    side->is_disconnect = side->portal && side->portal != side_other(side);
    side->has_mid = side->portal && side->mat.tex_mid.index != 0;

    const hash_t old_seg_hash = side->seg_hash;
    side->seg_hash = compute_seg_hash(level, side);

    if (old_seg_hash != side->seg_hash) {
        // enqueue for sector update if side segs changed
        level_push_dirty_sect_side(level, side);
    }

    if (!old_seg_hash || old_seg_hash != side->seg_hash) {
        // need to update block data since seg hashes changed
        level_traverse_blocks(
            level,
            side->wall->v0->pos,
            side->wall->v1->pos,
            level_traverse_blocks_bump_version,
            NULL,
            LTB_NONE);

        // need to recalculate decals
        llist_each(node, &side->decals, it) {
            decal_recalculate(level, it.el);
        }
    }

    // TODO: decals actually need to be adjusted to maintain their z
    // seg hash changed in game mode - remove decals
    /* if (g->mode == GAMEMODE_GAME && old_seg_hash != 0) { */
    /*     llist_each(node, &side->decals, it) { */
    /*         if (it.el->side.seg_hash != 0 */
    /*             && side->seg_hash != it.el->side.seg_hash) { */
    /*             *dynlist_push(level->delete_ptrs) = lptr_from(it.el); */
    /*         } */
    /*     } */
    /* } */
}

int side_index(const side_t *side) {
    return side->wall ? (side->wall->sides[0] == side ? 0 : 1) : -1;
}

side_t *side_other(const side_t *side) {
    if (!side->wall) {
        return NULL;
    }

    return
        side->wall->sides[0] == side ?
            side->wall->sides[1]
            : side->wall->sides[0];
}

v2 side_normal_point(
    const side_t *side) {
    if (!side->wall) { return v2_of(0); }
    const f32 s = side_is_right(side) ? 1 : -1;
    const v2 midpoint = wall_midpoint(side->wall);
    return
        v2_of(
            midpoint.x + 0.01f * s * side->wall->normal.x,
            midpoint.y + 0.01f * s * side->wall->normal.y);
}

v2 side_normal(const side_t *side) {
    if (!side->wall) { return v2_of(0); }
    const f32 s = side_is_right(side) ? 1 : -1;
    return
        v2_of(
            s * side->wall->normal.x,
            s * side->wall->normal.y);
}

v4 side_plane_vec(const side_t *side) {
    if (!side->sector) { return v4_of(0); }

    // get point on plane
    const v3 p =
        v3_of(
            wall_midpoint(side->wall),
            (side->sector->ceil.z + side->sector->floor.z) / 2.0f);

    // get normal, Z is always 0
    const v3 n = v3_of(side_normal(side), 0);

    // solve ax + by + cz + d = 0 for d, d = -(ax + by + cz)
    return v4_of(n, -v3_dot(n, p));
}

void sides_to_lines(
    side_t **sides,
    int n_sides,
    DYNLIST(line2f_t) *lines) {
    for (int i = 0; i < n_sides; i++) {
        vertex_t *verts[2];
        side_get_vertices(sides[i], verts);
        *dynlist_push(*lines) = (line2f_t) {
            .a = verts[0]->pos,
            .b = verts[1]->pos,
        };
    }
}

bool side_is_right(const side_t *side) {
    return side && side->wall && side == side->wall->sides[0];
}

void side_get_vertices(
    const side_t *side,
    vertex_t *vs[2]) {
    if (!side->wall) {
        vs[0] = NULL;
        vs[1] = NULL;
    } else if (side_is_right(side)) {
        vs[0] = side->wall->v0;
        vs[1] = side->wall->v1;
    } else {
        vs[0] = side->wall->v1;
        vs[1] = side->wall->v0;
    }
}

side_segments_t side_get_segments(const level_t *level, const side_t *side) {
    if (!side->sector) { return (side_segments_t) { 0 }; }

    vertex_t *vs[2];
    side_get_vertices(side, vs);

    if (!side->portal) {
        // only wall
        const rangef_t
            wzs_l = sector_point_zs(side->sector, vs[0]->pos),
            wzs_r = sector_point_zs(side->sector, vs[1]->pos);

        return (side_segments_t) {
            .wall = {
                .present = true,
                .mesh = true,
                .solid = true,
                .index = SIDE_SEGMENT_WALL,
                .zbl = wzs_l.z0,
                .zbr = wzs_r.z0,
                .ztl = wzs_l.z1,
                .ztr = wzs_r.z1,
            },
        };
    }

    if (!side->portal->sector) {
        // no segments, bad portal
        return (side_segments_t) { 0 };
    }

    side_segments_t segs = { 0 };

    const rangef_t
        zs_l = sector_point_zs(side->sector, vs[0]->pos),
        zs_r = sector_point_zs(side->sector, vs[1]->pos);

    f32 zbl = zs_l.z0, zbr = zs_r.z0, ztl = zs_l.z1, ztr = zs_r.z1;

    const side_t *port = side->portal;
    const sector_t *sect = side->sector, *port_sect = port->sector;

    vertex_t *pvs[2];
    side_get_vertices(port, pvs);

    // NOTE: swapped since we're getting Zs relative to THIS side (opposite)
    const rangef_t
        pzs_l = sector_point_zs(port->sector, pvs[1]->pos),
        pzs_r = sector_point_zs(port->sector, pvs[0]->pos);

    f32 pzbl = pzs_l.z0, pzbr = pzs_r.z0, pztl = pzs_l.z1, pztr = pzs_r.z1;

    // move according to relative Z distance, if there is one
    f32 rz = side->is_disconnect ? sect->floor.z - port_sect->floor.z : 0.0f;

    if (side->is_disconnect) {
        pzbl += rz;
        pzbr += rz;
        pztl += rz;
        pztr += rz;
    }

    // get middle Zs
    f32 mzbl, mzbr, mztl, mztr;

    if (side->is_disconnect) {
        mzbl = max(zbl, pzbl);
        mzbr = max(zbr, pzbr);
        mztl = mzbl + (min(pztl, ztl) - max(pzbl, zbl));
        mztr = mzbr + (min(pztr, ztr) - max(pzbr, zbr));
    } else {
        mzbl = pzbl;
        mzbr = pzbr;
        mztl = pztl;
        mztr = pztr;
    }

    // bottom segment
    if (mzbl > zbl || mzbr > zbr) {
        // check if mzbl..mzbr and zbl..zbr overlap
        // for example
        //   zbl \
        //        \
        // mzbl -\ \
        //       \-\\
        //          -\  <------------- contact point
        //            \-\
        //             \ -\
        //              \  -\
        //           zbr \   -\ mzbr
        // in this case, we only want a "bottom" in the triangle
        // between the contact point, zbr, and mzbr
        f32 bzbl = zbl, bzbr = zbr, bztl = mzbl, bztr = mzbr;

        f32 ta, tb;
        if (intersect_segs(
                v2_of(0, zbl), v2_of(1, zbr),
                v2_of(0, mzbl), v2_of(1, mzbr),
                NULL, &ta, &tb)
            && ta != 0.0f && tb != 0.0f
            && ta != 1.0f && tb != 1.0f) {
            if (zbl > mzbl) {
                bzbl = mzbl;
                bztl = mzbl;
            } else {
                bzbr = mzbr;
                bztr = mzbr;
            }
        }

        segs.bottom =
            (side_segment_t) {
                .present = true,
                .mesh = true,
                .solid = true,
                .index = SIDE_SEGMENT_BOTTOM,
                .zbl = bzbl,
                .zbr = bzbr,
                .ztl = bztl,
                .ztr = bztr,
            };
    }

    if (mztl < ztl || mztr < ztr) {
        // check for same overlap issue as described with bottom segment
        f32 tzbl = mztl, tzbr = mztr, tztl = ztl, tztr = ztr;

        f32 ta, tb;
        if (intersect_segs(
                v2_of(0, mztl), v2_of(1, mztr),
                v2_of(0, ztl), v2_of(1, ztr),
                NULL, &ta, &tb)
            && ta != 0.0f && tb != 0.0f
            && ta != 1.0f && tb != 1.0f) {
            if (ztl < mztl) {
                tzbl = mztl;
                tztl = mztl;
            } else {
                tzbr = mztr;
                tztr = mztr;
            }
        }

        segs.top =
            (side_segment_t) {
                .present = true,
                .mesh = true,
                .solid = true,
                .index = SIDE_SEGMENT_TOP,
                .zbl = tzbl,
                .zbr = tzbr,
                .ztl = tztl,
                .ztr = tztr,
            };
    }

    // no middle segment if there is no overlap
    if ((zbl <= pztl && pzbl <= ztl) || (zbr <= pztr && pzbr <= ztr)) {
        const bool
            has_mid_tex = side->mat.tex_mid.index != 0,
            use_mid = side->mat.flags & SDMF_MID;

        segs.middle =
            (side_segment_t) {
                .present = true,
                .portal = true,
                .solid = use_mid && has_mid_tex,
                .mesh = (use_mid && has_mid_tex) || side->is_disconnect,
                .index = SIDE_SEGMENT_MIDDLE,
                .zbl = mzbl,
                .zbr = mzbr,
                .ztl = mztl,
                .ztr = mztr,
            };
    }

    // eliminate tiny segments
    for (int i = 0; i < ARRLEN(segs.arr); i++) {
        if (segs.arr[i].present
            && fabsf(segs.arr[i].ztl - segs.arr[i].zbl) < 0.03f
            && fabsf(segs.arr[i].ztr - segs.arr[i].zbr) < 0.03f) {
            segs.arr[i].present = false;
        }
    }

    return segs;
}

bool side_get_z_segment(
        const level_t *level,
        const side_t *side,
        v2 point,
        f32 z,
        side_segment_t *seg) {
    const side_segments_t segs = side_get_segments(level, side);

    const f32 u = side_point_u(side, point);

    for (int i = 0; i < SIDE_SEGMENT_COUNT; i++) {
        if (segs.arr[i].present
            && z >= lerp(segs.arr[i].zbl, segs.arr[i].zbr, u)
            && z <= lerp(segs.arr[i].ztl, segs.arr[i].ztr, u)) {
            *seg = segs.arr[i];
            return true;
        }
    }

    return false;
}

bool side_get_offset_segment(
        const level_t *level,
        const side_t *side,
        v2 offsets,
        side_segment_t *seg) {
    if (!side->sector) { return false; }
    const side_segments_t segs = side_get_segments(level, side);

    const f32 u = offsets.x / side->wall->len;
    const f32 z_true = offsets.y + side->sector->floor.z;

    for (int i = 0; i < SIDE_SEGMENT_COUNT; i++) {
        if (segs.arr[i].present
            && z_true >= lerp(segs.arr[i].zbl, segs.arr[i].zbr, u)
            && z_true <= lerp(segs.arr[i].ztl, segs.arr[i].ztr, u)) {
            *seg = segs.arr[i];
            return true;
        }
    }

    return false;
}

bool side_get_z_range_segment(
        const level_t *level,
        const side_t *side,
        v2 point,
        f32 z_min,
        f32 z_max,
        side_segment_t *seg) {
    const side_segments_t segs = side_get_segments(level, side);

    const f32 u = side_point_u(side, point);

    side_segment_t result = { 0 };
    f32 overlap = 0.0f;

    for (int i = 0; i < ARRLEN(segs.arr); i++) {
        if (!segs.arr[i].present) { continue; }

        const f32
            z0 = lerp(segs.arr[i].zbl, segs.arr[i].zbr, u),
            z1 = lerp(segs.arr[i].ztl, segs.arr[i].ztr, u);

        if (z_min <= z1 && z_max >= z0) {
            const f32 seg_overlap = max(0.0f, min(z_max, z1) - max(z_min, z0));
            if (seg_overlap > overlap) {
                result = segs.arr[i];
                overlap = seg_overlap;
            }
        }
    }

    *seg = result;
    return result.present;
}

f32 side_point_u(const side_t *side, v2 point) {
    vertex_t *vs[2];
    side_get_vertices(side, vs);
    return
        clamp(
            v2_norm(
                v2_sub(
                    point_project_segment(point, vs[0]->pos, vs[1]->pos),
                    vs[0]->pos))
            / side->wall->len,
        0.0f, 1.0f);
}

v2 side_u_to_point(const side_t *side, f32 u) {
    vertex_t *vs[2];
    side_get_vertices(side, vs);
    return v2_lerp(vs[0]->pos, vs[1]->pos, u);
}

v2 side_x_to_point(const side_t *side, f32 x) {
    return side_u_to_point(side, x / side->wall->len);
}

rangef_t side_segment_zs_at(const side_segment_t *seg, f32 u) {
    ASSERT(seg->present);
    return (rangef_t) {
        .z0 = lerp(seg->zbl, seg->zbr, u),
        .z1 = lerp(seg->ztl, seg->ztr, u),
    };
}

void side_z_bounds(
        const level_t *level,
        const side_t *side,
        f32 *zbl,
        f32 *zbr,
        f32 *ztl,
        f32 *ztr) {
    const side_segments_t segs = side_get_segments(level, side);

    f32 _zbl = 1e10, _zbr = 1e0, _ztl = -1e10, _ztr = -1e10;

    for (int i = 0; i < ARRLEN(segs.arr); i++) {
        if (segs.arr[i].present) {
            _zbl = min(segs.arr[i].zbl, _zbl);
            _zbr = min(segs.arr[i].zbr, _zbr);
            _ztl = max(segs.arr[i].ztl, _ztl);
            _ztr = max(segs.arr[i].ztr, _ztr);
        }
    }

    if (zbl) { *zbl = _zbl; }
    if (zbr) { *zbr = _zbr; }
    if (ztl) { *ztl = _ztl; }
    if (ztr) { *ztr = _ztr; }
}

rangef_t side_z_bounds_for_u(
        const level_t *level,
        const side_t *side,
        f32 u) {
    f32 zbl, zbr, ztl, ztr;
    side_z_bounds(level, side, &zbl, &zbr, &ztl, &ztr);
    return (rangef_t) {
        .z0 = lerp(zbl, zbr, u),
        .z1 = lerp(ztl, ztr, u),
    };
}

v2 side_coords_to_relative(const side_t *side, v3 coords) {
    v2 coords_xy = v2_from(coords);
    coords_xy =
        point_project_segment(
            coords_xy, side->wall->v0->pos, side->wall->v1->pos);

    vertex_t *vs[2];
    side_get_vertices(side, vs);

    return
        v2_of(
            v2_distance(coords_xy, vs[0]->pos),
            side->sector ?
                coords.z - side->sector->floor.z
                : coords.z);
}
