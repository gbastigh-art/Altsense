#include "level/decal.h"
#include "gfx/tex_atlas.h"
#include "level/level_types.h"
#include "level/level.h"
#include "level/sector.h"
#include "level/side.h"
#include "game.h"
#include "trace.h"

decal_t *decal_new(level_t *level, const decal_t *defaults) {
    decal_t *d = level_try_alloc(level, &level->decals);

    if (!d) {
        if (defaults
            && (DECAL_TYPES[defaults->type].age_removable)
            && level->decals_by_type[defaults->type].head) {
            // TODO: this is maybe bad because it can invalidate level pointers
            // mid-frame?
            // remove an earlier decal of the same type and reallocate at the
            // same spot
            decal_t *oldest =
                level->decals_by_type[defaults->type].head;
            const int index = oldest->handle.i;
            decal_delete(level, oldest);
            d = level_try_alloc_at(level, &level->decals, index);

            if (!d) {
                return NULL;
            }
        } else {
            return NULL;
        }
    }

    if (defaults) {
        level_fields_t backup = d->level_fields;
        *d = *defaults;
        d->level_fields = backup;

        if (DECAL_TYPES[d->type].tex && !d->tex.index) {
            d->tex = tex_atlas_lookup(DECAL_TYPES[d->type].tex);
        }
    }

    if (d->ticks == 0) {
        // not initialized by defaults
        d->ticks = -1;
    }

    if (d->type != DECAL_TYPE_PLACEHOLDER) {
        // add on end of level list of decals of this type
        dlist_append(
            level_by_type_node,
            &level->decals_by_type[d->type],
            d);
    }

    d->spawn_tick = g->tick;
    return d;
}

void decal_delete(level_t *level, decal_t *d) {
    if (d->type != DECAL_TYPE_PLACEHOLDER) {
        // drop from level list
        dlist_remove(
            level_by_type_node,
            &level->decals_by_type[d->type],
            d);
    }

    if (d->is_on_side) {
        llist_remove(node, &d->side.ptr->decals, d);
        level_enqueue_recalc(level, lptr_from(d->side.ptr));
    } else {
        llist_remove(node, &d->sector.ptr->decals, d);
        level_enqueue_recalc(level, lptr_from(d->sector.ptr));
    }

    level_free(level, &level->decals, d);
}

void decal_set_type(level_t *level, decal_t *d, decal_type_e type) {
    if (d->type == type) {
        return;
    }

    if (d->type != DECAL_TYPE_PLACEHOLDER) {
        dlist_remove(
            level_by_type_node,
            &level->decals_by_type[d->type],
            d);
    }

    d->type = type;

    if (d->type != DECAL_TYPE_PLACEHOLDER) {
        dlist_append(
            level_by_type_node,
            &level->decals_by_type[d->type],
            d);
    }
}

decal_t *decal_new_on_trace_hit(
        level_t *level,
        const trace_hit_t *hit,
        f32 z,
        const decal_t *defaults) {
    if (hit->type == LT_SIDE) {
        decal_t *decal = decal_new(level, defaults);
        if (!decal) { return NULL; }

        decal_set_side(level, decal, hit->side.ptr);
        decal->side.offsets =
            v2_of(hit->side.x, z - hit->side.ptr->sector->floor.z);
        decal_recalculate(level, decal);
        return decal;
    } else if (hit->type == LT_SECTOR) {
        decal_t *decal = decal_new(g->level, defaults);
        if (!decal) { return NULL; }

        decal_set_sector(
            g->level, decal, hit->sector.ptr, hit->sector.plane);
        decal->sector.pos = hit->swept_pos;
        decal_recalculate(level, decal);
        return decal;
    }

    return NULL;
}

void decal_tick(level_t *level, decal_t *decal) {
    if (decal->ticks != -1 && --decal->ticks <= 0) {
        level_enqueue_delete(level, lptr_from(decal));
    }
}

void decal_recalculate(level_t *level, decal_t *decal) {
    if (decal->lflags.do_not_recalc) { return; }
    decal->lflags.recalc_enqueued = false;

    level->version++;
    decal->version++;

    // clamp
    if (decal->is_on_side) {
        if (decal->side.ptr->sector) {
            const f32 x =
                clamp(
                    decal->side.offsets.x,
                    0.0f,
                    decal->side.ptr->wall->len);

            const rangef_t zs =
                side_z_bounds_for_u(
                    level,
                    decal->side.ptr,
                    x / decal->side.ptr->wall->len);

            const f32 floor = decal->side.ptr->sector->floor.z;
            decal->side.offsets =
                v2_of(
                    x,
                    clamp(
                        decal->side.offsets.y,
                        zs.z0 - floor,
                        zs.z1 - floor));

            // snap to pixel
            decal->side.offsets =
                v2_divs(
                    v2_floor(v2_scale(decal->side.offsets, PX_PER_UNIT)),
                    PX_PER_UNIT);

            // enqueue delete if decal is on bad segment or portal
            side_segment_t seg = decal_side_segment(level, decal);
            if (!seg.present || seg.portal) {
                level_enqueue_delete(level, lptr_from(decal));
            }
        }

        decal->side.seg_hash = decal->side.ptr->seg_hash;
        decal->side.ptr->version++;
    } else {
        decal->sector.pos =
            sector_clamp_point(decal->sector.ptr, decal->sector.pos);

        // snap to pixel
        decal->sector.pos =
            v2_divs(
                v2_floor(v2_scale(decal->sector.pos, PX_PER_UNIT)),
                PX_PER_UNIT);

        decal->sector.ptr->version++;
    }
}

v3 decal_worldpos(const decal_t *decal) {
    if (decal->is_on_side) {
        vertex_t *vertices[2];
        side_get_vertices(decal->side.ptr, vertices);
        return
            v3_of(
                v2_lerp(
                    vertices[0]->pos,
                    vertices[1]->pos,
                    decal->side.offsets.x / decal->side.ptr->wall->len),
                decal->side.ptr->sector->floor.z + decal->side.offsets.y);
    } else {
        return
            v3_of(
                decal->sector.pos,
                decal->sector.ptr->planes[decal->sector.plane].z);
    }
}

v4 decal_surface_plane(const level_t *level, const decal_t *decal) {
    if (decal->is_on_side) {
        return side_plane_vec(decal->side.ptr);
    } else {
        return sector_plane_vec(decal->sector.ptr, decal->sector.plane);
    }
}

static void decal_detach(level_t *level, decal_t *decal) {
    if (decal->is_on_side) {
        if (decal->side.ptr) {
            llist_remove(node, &decal->side.ptr->decals, decal);
            side_recalculate(level, decal->side.ptr);
        }
    } else {
        if (decal->sector.ptr) {
            llist_remove(node, &decal->sector.ptr->decals, decal);
            sector_recalculate(level, decal->sector.ptr);
        }
    }

    decal->side = (typeof(decal->side)) { 0 };
    decal->sector = (typeof(decal->sector)) { 0 };
}

void decal_set_side(
        level_t *level,
        decal_t *decal,
        side_t *side) {
    if (decal->is_on_side && decal->side.ptr == side) { return; }

    decal_detach(level, decal);

    decal->side.ptr = side;
    llist_prepend(node, &side->decals, decal);
    decal->is_on_side = true;
    side_recalculate(level, decal->side.ptr);
}

void decal_set_sector(
        level_t *level,
        decal_t *decal,
        sector_t *sector,
        plane_type_e plane) {
    if (!decal->is_on_side
        && decal->sector.ptr == sector
        && decal->sector.plane == plane) {
        return;
    }

    decal_detach(level, decal);

    decal->sector.ptr = sector;
    llist_prepend(node, &sector->decals, decal);
    decal->sector.plane = plane;
    decal->is_on_side = false;
    sector_recalculate(level, decal->sector.ptr);
}

box2f_t decal_bounds(const level_t *level, const decal_t *decal) {
    const tex_atlas_entry_t *entry = tex_atlas_entry_by_id(decal->tex);

    const v2 size =
        v2_divs(box2f_size(box2f_from(entry->box_px)), PX_PER_UNIT);

    if (decal->is_on_side) {
        return
            box2f_mm(
                v2_of(
                    decal->side.offsets.x - (size.x / 2.0f),
                    decal->side.offsets.y - (size.y / 2.0f)),
                v2_of(
                    decal->side.offsets.x + (size.x / 2.0f),
                    decal->side.offsets.y + (size.y / 2.0f)));
    } else {
        return
            box2f_mm(
                v2_sub(decal->sector.pos, v2_divs(size, 2.0f)),
                v2_add(decal->sector.pos, v2_divs(size, 2.0f)));
    }
}

v3 decal_center(const level_t *level, const decal_t *decal) {
    const v2 c = box2f_center(decal_bounds(level, decal));
    if (decal->is_on_side) {
        vertex_t *vs[2];
        side_get_vertices(decal->side.ptr, vs);
        return
            v3_of(
                v2_lerp(
                    vs[0]->pos, vs[1]->pos, c.x / decal->side.ptr->wall->len),
                decal->side.ptr->sector->floor.z + c.y);
    } else {
        return
            v3_of(
                c,
                sector_point_zs(decal->sector.ptr, c)
                    .zs[decal->sector.plane]);
    }
}

light_desc_t decal_light(const level_t *level, const decal_t *decal) {
    light_desc_t l = { 0 };

    switch (decal->type) {
    case DECAL_TYPE_HOLE:
        const f32 power =
            satf(1.0 - ticks_since_tick(decal->spawn_tick) / 15.0f);
        l = (light_desc_t) {
            .params = {
                .attenuation = 0.8,
                .power = 1.5f * power,
                .color = v3_scale(v3_of(1, 1, 0.25f), power),
                .c1 = 4.0f,
                .c2 = 15.0f,
                .ambient = 0.0f,
                .flags = LIGHT_FLAG_NO_SHADOWS | LIGHT_FLAG_IGNORE_NEAR,
            }
        };
        break;
    default: break;
    }

    if (!v3_eqv_eps(l.params.color, v3_of(0))) {
        l.id = LIGHT_ID_FROM(LIGHT_TYPE_DECAL, decal->id);
    }

    if (v3_eqv_eps(l.pos, v3_of(0))) {
        const v3 normal = v3_from(decal_surface_plane(level, decal));
        l.pos =
            v3_add(
                decal_center(level, decal),
                v3_scale(normal, 0.01f));
    }

    return l;
}

side_segment_t decal_side_segment(const level_t *level, const decal_t *decal) {
    if (!decal->is_on_side) { return (side_segment_t) { 0 }; }
    side_segment_t seg;
    side_get_offset_segment(level, decal->side.ptr, decal->side.offsets, &seg);
    return seg;
}

decal_type_t DECAL_TYPES[DECAL_TYPE_COUNT] = {
    [DECAL_TYPE_PLACEHOLDER] = { },
    [DECAL_TYPE_HOLE] = {
        .has_light = true,
    },
    [DECAL_TYPE_GORE] = {
        .age_removable = true,
    },
    [DECAL_TYPE_BLOOD] = {
        .tex = "notex_64x16",
        .age_removable = true,
        .clamped_to_surface = true,
    },
};
