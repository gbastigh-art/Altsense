#include "level/sector.h"
#include "ext/tess.h"
#include "fingers.h"
#include "level/block.h"
#include "level/decal.h"
#include "level/entity.h"
#include "level/level.h"
#include "level/lptr.h"
#include "level/particle.h"
#include "level/side.h"
#include "level/vertex.h"
#include "level/wall.h"
#include "gfx/renderer.h"
#include "util/hash.h"
#include "util/map.h"
#include "util/rand.h"
#include "game.h"

#define SUBSECTOR_ID_INVALID -1

// creates subsector on allocator
static subsector_t subsector_create(allocator_t *al) {
    subsector_t sub = { .id = SUBSECTOR_ID_INVALID };
    dynlist_init(sub.lines, al);
    dynlist_init(sub.tris, al);
    dynlist_init(sub.neighbors, al);
    return sub;
}

// convert triangle -> sub (on allocator)
static subsector_t sect_tri_to_sub(sect_tri_t *tri, allocator_t *al) {
    subsector_t s = subsector_create(al);
    *dynlist_push(s.lines) = (sect_line_t) { .a = tri->a, .b = tri->b };
    *dynlist_push(s.lines) = (sect_line_t) { .a = tri->b, .b = tri->c };
    *dynlist_push(s.lines) = (sect_line_t) { .a = tri->c, .b = tri->a };
    *dynlist_push(s.tris) = tri;
    return s;
}

// returns index of adjacent line in a if one is found, otherwise returns -1
static int subsectors_find_adjacent(
        const subsector_t *a,
        const subsector_t *b,
        int *other_index) {
    // adjacent if any one line segment is equal to another on the other
    // subsector
    dynlist_each(a->lines, it_a) {
        dynlist_each(b->lines, it_b) {
            if (sect_line_eq(it_a.el, it_b.el)) {
                if (other_index) { *other_index = it_b.i; }
                return it_a.i;
            }
        }
    }

    return -1;
}

static subsector_t combine_subsectors(
        const subsector_t *a,
        const subsector_t *b,
        allocator_t *al) {

    ASSERT(a != b);

    subsector_t s = subsector_create(al);

    // add lines in a not in b
    dynlist_each(a->lines, it_a) {
        bool same = false;

        dynlist_each(b->lines, it_b) {
            if (sect_line_eq(it_a.el, it_b.el)) {
                same = true;
                break;
            }
        }

        if (same) { continue; }

        *dynlist_push(s.lines) = *it_a.el;
    }

    // add lines in b not in a
    dynlist_each(b->lines, it_b) {
        bool same = false;

        dynlist_each(a->lines, it_a) {
            if (sect_line_eq(it_b.el, it_a.el)) {
                same = true;
                break;
            }
        }

        if (same) { continue; }

        *dynlist_push(s.lines) = *it_b.el;
    }

    ASSERT(dynlist_size(s.lines) >= 3);

    // combine all triangles
    dynlist_each(a->tris, it) { *dynlist_push(s.tris) = *it.el; }
    dynlist_each(b->tris, it) { *dynlist_push(s.tris) = *it.el; }

    return s;
}

static bool subsector_traverse_block_neighbors(
        level_t *level,
        block_t *block,
        v2i,
        subsector_t *sub) {
    ASSERT(sub->id != SUBSECTOR_ID_INVALID);

    // check adjacency with other subsectors in block
    dynlist_each(block->subsectors, it) {
        if (*it.el == sub->id) { continue; }

        subsector_t *other =
            blklist_ptr(subsector_t, &level->subsectors, *it.el);

        // check that we are not already neighbors
        bool found = false;
        dynlist_each(sub->neighbors, it_n) {
            if (it_n.el->sub == other) {
                found = true;
                break;
            }
        }

        if (found) {
            continue;
        }

        int line = -1, other_line = -1;
        if ((line = subsectors_find_adjacent(sub, other, &other_line)) != -1) {
            *dynlist_push(sub->neighbors) =
                (subsector_neighbor_t) {
                    .sub = other,
                    .line = &sub->lines[line],
                    .is_portal = false,
                };

            *dynlist_push(other->neighbors) =
                (subsector_neighbor_t) {
                    .sub = sub,
                    .line = &other->lines[other_line],
                    .is_portal = false,
                };
        }
    }

    return true;
}

static void subsector_add(level_t *level, sector_t *sector, subsector_t *from) {
    ASSERT(from->id == SUBSECTOR_ID_INVALID);

    subsector_t *s = blklist_add(subsector_t, &level->subsectors);
    *s = (subsector_t) {
        .id = blklist_index_of(&level->subsectors, s),
    };

    ASSERT(blklist_present(&level->subsectors, s->id));

    DYNLIST(sect_line_t) lines = from->lines;
    s->lines = dynlist_create(sect_line_t, &level->arena);

    // copy lines with correct right-side-normal winding
    *dynlist_push(s->lines) = dynlist_pop(lines);
    while (dynlist_size(lines) != 0) {
        dynlist_each(lines, it) {
            if (s->lines[dynlist_size(s->lines) - 1].vs[1] == it.el->vs[0]) {
                *dynlist_push(s->lines) = dynlist_remove_it(lines, it);
                break;
            }
        }
    }

    s->tris = dynlist_copy_onto(from->tris, &level->arena);
    s->neighbors = dynlist_copy_onto(from->neighbors, &level->arena);

    // add into sector
    s->parent = sector;
    llist_prepend(sector_node, &sector->subs, s);

    // calc min/max
    s->min = s->lines[0].a->pos;
    s->max = s->lines[0].b->pos;
    dynlist_each(s->lines, it) {
        s->min = v2_minv(s->min, it.el->a->pos);
        s->min = v2_minv(s->min, it.el->b->pos);
        s->max = v2_maxv(s->max, it.el->a->pos);
        s->max = v2_maxv(s->max, it.el->b->pos);
    }

    // compute center
    s->center = v2_of(0);
    dynlist_each(s->lines, it) {
        s->center = v2_add(s->center, it.el->a->pos);
        s->center = v2_add(s->center, it.el->b->pos);
    }
    s->center = v2_divs(s->center, dynlist_size(s->lines) * 2);

    // assign line sides
    dynlist_each(s->lines, it) {
        side_t *side = vertices_shared_side(level, it.el->a, it.el->b, sector);

        if (side) {
            it.el->side = side;
            it.el->side->subsector = s;
            it.el->side->sect_line = it.el;
        }
    }
}

static void subsector_destroy(level_t *level, subsector_t *s) {
    if (s->id == SUBSECTOR_ID_INVALID) {
        ASSERT_DEBUG(dynlist_header(s->lines)->allocator != &level->arena);
        ASSERT_DEBUG(dynlist_header(s->tris)->allocator != &level->arena);
        ASSERT_DEBUG(dynlist_header(s->neighbors)->allocator != &level->arena);
    }

    if (s->id != SUBSECTOR_ID_INVALID) {
        // remove this subsector from its neighbors
        dynlist_each(s->neighbors, it) {
            subsector_t *sub = it.el->sub;

            DYNLIST(subsector_neighbor_t) *ns = &sub->neighbors;

            bool found = false;
            dynlist_each(*ns, it_n) {
                if (it_n.el->sub == s) {
                    dynlist_remove_it(*ns, it_n);
                    found = true;
                    break;
                }
            }

            ASSERT(found);
        }

        // remove from blocks
        level_blocks_remove_subsector(level, s);

        // remove from sides
        dynlist_each(s->lines, it) {
            if (it.el->side) {
                it.el->side->subsector = NULL;
                it.el->side->sect_line = NULL;
            }
        }
    }

    dynlist_destroy(s->lines);
    dynlist_destroy(s->tris);
    dynlist_destroy(s->neighbors);

    if (s->id != SUBSECTOR_ID_INVALID) {
        blklist_remove(&level->subsectors, s->id);
    }
}

static bool subsector_is_convex(const subsector_t *s) {
    // visted lines: set of sect_line_t*
    map_t visited;
    map_init(
        &visited,
        &g->frame_arena,
        sizeof(sect_line_t*),
        0,
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    // go to each non-visited line from the start and check that all inner
    // angles are <180 degrees (PI radians)
    const sect_line_t *start = &s->lines[0];
    const sect_line_t *line = start;
    while (line) {
        if (map_containsp(&visited, &line)) {
            return line == start;
        }

        map_insertpk(&visited, &line);

        f32 angle_min = TAU;
        const sect_line_t *next = NULL;

        for (int j = 0; j < dynlist_size(s->lines); j++) {
            const sect_line_t *other = &s->lines[j];

            // ignore current
            if (other == line) { continue; }

            // check only for lines where other start == line end
            if (!v2_eqv_eps(other->a->pos, line->b->pos)) {
                continue;
            }

            // find angle between this line and next line
            const f32 angle =
                angle_in_points_inner_cw(
                    line->a->pos,
                    other->a->pos,
                    other->b->pos);

            if (angle < angle_min) {
                next = other;
                angle_min = angle;
            }
        }

        if (angle_min > (PI + 0.000001f)) {
            return false;
        }

        if (!next) { return false; }
        line = next;
    }

    return true;
}

// combine triangles until a reasonable convex subset has been made (shapes)
static void convexify_tris(
    level_t *level,
    DYNLIST(sect_tri_t) *tris,
    DYNLIST(subsector_t) *out) {

    // queue to convexify
    DYNLIST(subsector_t) queue = dynlist_create(subsector_t, &g->frame_arena);

    // convert all triangles -> shapes
    dynlist_each(*tris, it) {
        *dynlist_push(queue) = sect_tri_to_sub(it.el, &g->frame_arena);
    }

    while (dynlist_size(queue) != 0) {
        subsector_t sub = dynlist_pop(queue);

        bool merged = false;

        // find adjacent shapes in the queue
        dynlist_each(queue, it) {
            const int line = subsectors_find_adjacent(&sub, it.el, NULL);
            if (line == -1) {
                continue;
            }

            // check if a merger would be convex
            subsector_t merger =
                combine_subsectors(&sub, it.el, &g->frame_arena);

            if (subsector_is_convex(&merger)) {
                subsector_destroy(level, &sub);
                subsector_destroy(level, it.el);

                dynlist_remove_it(queue, it);
                *dynlist_push(queue) = merger;

                // shape has been consumed
                merged = true;
                break;
            } else {
                subsector_destroy(level, &merger);
            }
        }

        // not possible to merge -> final sub!
        if (!merged) {
            *dynlist_push(*out) = sub;
        }
    }
}

f32 sect_tri_volume(
        const sect_tri_t *tri,
        f32 max_height) {
    if (max_height <= 0.0f) {
        return tri->approx_volume;
    } else {
        // 3D volume is triangle area * height
        return tri->area * min(max_height, tri->approx_height);
    }
}

f32 subsector_volume(const subsector_t *sub, f32 max_height) {
    f32 v = 0.0f;
    dynlist_each(sub->tris, it) {
        v += sect_tri_volume(*it.el, max_height);
    }
    return v;
}

f32 subsector_distance_via(
        const subsector_t *a,
        const subsector_t *b,
        const sect_line_t *line) {
    // ensure that line is on a
    ASSERT_DEBUG(
        line >= &a->lines[0]
        && line < &a->lines[dynlist_size(a->lines)]);

    if (!line->side
        || !line->side->is_disconnect
        || line->side->portal->subsector != b) {
        return v2_distance(a->center, b->center);
    }

    // go from a center -> line midpoint -> (portal) -> b center
    const v2
        mid_a = wall_midpoint(line->side->wall),
        mid_b = wall_midpoint(line->side->portal->wall);

    ASSERT_DEBUG(line->side->subsector == a);
    ASSERT_DEBUG(line->side->portal->subsector == b);

    return v2_distance(a->center, mid_a) + v2_distance(b->center, mid_b);
}

sectmat_data_t sector_get_mat(const sector_t *sect) {
    sectmat_data_t mat;
    const sector_t *like = sector_get_like(sect);
    if (like) {
        mat = like->mat;

        for (int i = 0; i < 2; i++) {
            if (sect->mat.overlays[i].index) {
                mat.overlays[i] = sect->mat.overlays[i];
                mat.overlay_alphas[i] = sect->mat.overlay_alphas[i];
            }
        }
    } else {
        mat = sect->mat;
    }

    return mat;
}

sector_t *sector_new(level_t *level, const sector_t *like) {
    ASSERT(level->sectors.data.size < MAX_SECTORS, "out of sectors");

    sector_t *sect = level_try_alloc(level, &level->sectors);
    dynlist_init(sect->tris, &level->arena);
    dynlist_init(sect->neighbors, &level->arena);
    dynlist_init(sect->particles, &level->arena);
    dynlist_init(sect->disconnected_portals, &level->arena);
    dynlist_init(sect->pvs_outgoing, &level->arena);

    if (like) {
        sector_copy_props(level, sect, like);
    }

    return sect;
}

void sector_delete(level_t *level, sector_t *sect) {
    LOG("deleting sector %d", sect->id);

    if (g_renderer->level == level) {
        renderer_free_sector(sect);
    }

    // find sectors "like" this sector - replace their like with another sector
    // like this one
    sector_t *first_like = NULL;
    level_each(sector_t, &level->sectors, it) {
        sector_t *like = sector_get_like(it.el);
        if (like != sect) { continue; }

        if (!first_like) {
            first_like = it.el;

            // this sector is like itself now
            sector_try_set_like(level, it.el, NULL);
        } else {
            // other sectors are like the first one
            sector_try_set_like(level, it.el, first_like);
        }
    }

    // mark so that sector is not deleted from side removal
    sect->lflags.mark = true; // TODO: replace with unique level flag

    // remove from blocks before min, max are recalculated
    level_blocks_remove_sector(level, sect);

    // unlink sector from all sides
    if (sect->n_sides > 0) {
        DYNLIST(side_t*) sides = dynlist_create(side_t*, &g->frame_arena);
        const int n_sides = sector_get_sides(level, sect, &sides);

        for (int i = 0; i < n_sides; i++) {
            sides[i]->lflags.do_not_recalc = true;
        }

        for (int i = 0; i < n_sides; i++) {
            sector_remove_side(level, sect, sides[i]);
        }

        for (int i = 0; i < n_sides; i++) {
            sides[i]->lflags.do_not_recalc = false;
            side_recalculate(level, sides[i]);
        }
    }

    // delete decals
    llist_each(node, &sect->decals, it) {
        decal_delete(level, it.el);
    }

    // unlink sector entities
    dlist_each(sector_node, &sect->entities, it) {
        dlist_remove(sector_node, &sect->entities, it.el);
        it.el->sector = NULL;
    }

    llist_each(sector_node, &sect->subs, it) {
        ASSERT(it.el->id != SUBSECTOR_ID_INVALID);
        subsector_destroy(level, it.el);
    }

    // remove from all sector neighbor lists
    level_each(sector_t, &level->sectors, it0) {
        dynlist_each(it0.el->neighbors, it1) {
            if (*it1.el == sect) {
                dynlist_remove_it(it0.el->neighbors, it1);
                break;
            }
        }
    }

    // need to recompute sector matrices
    sector_matrices_recompute(level);

    DYNLIST(sector_t*) neighbors = dynlist_copy(sect->neighbors);

    dynlist_destroy(sect->neighbors);
    dynlist_destroy(sect->tris);
    dynlist_destroy(sect->particles);
    dynlist_destroy(sect->disconnected_portals);
    dynlist_destroy(sect->pvs_outgoing);
    level_free(level, &level->sectors, sect);

    // force neighbors to recalc AFTER sector is entirely removed
    dynlist_each(neighbors, it) {
        sector_recalculate(level, *it.el);
    }
}

void sector_fixed_update(level_t *level, sector_t *sect) {
    dynlist_each(sect->particles, it) {
        if (it.el->delete || it.el->moved) {
            if (it.el->delete) {
                ASSERT_DEBUG(level->particle_counts[it.el->type] != 0);
                level->particle_counts[it.el->type]--;
            }

            dynlist_swap_remove_it(sect->particles, it);
        } else {
            particle_fixed_update(level, it.el);
        }
    }

    // do diffing for triggered sectors
    if (sect->diff_trigger_tick
        && dynlist_size(sect->neighbors) > 0) {

        if (ticks_since_tick(sect->diff_trigger_tick) <= 1) {
            sect->diff_start_z = sect->ceil.z;

            // find target height - lowest near sector minus 1/4 unit
            f32 z_target = sect->neighbors[0]->ceil.z;
            dynlist_each(sect->neighbors, it) {
                z_target = min(z_target, (*it.el)->ceil.z - 0.25f);
            }

            sect->diff_target_z = z_target;
        } else {
            // move towards target height at specified rate
            const f32 secs = secs_since_tick(sect->diff_trigger_tick);
            const f32 diff = fabsf(sect->diff_target_z - sect->diff_start_z);
            const f32 rate = max(sect->diff_rate, 0.25f);
            const f32 old = sect->ceil.z;
            sect->ceil.z =
                lerp(
                    sect->diff_start_z,
                    sect->diff_target_z,
                    satf((secs * rate) / diff));

            if (fabsf(old - sect->ceil.z) > 0.001f) {
                level_enqueue_recalc(level, lptr_from(sect));
            }
        }
    }
}

void sector_tick(level_t *level, sector_t *sector) {
    dynlist_each(sector->particles, it) {
        if (it.el->delete || it.el->moved) {
            if (it.el->delete) {
                ASSERT_DEBUG(level->particle_counts[it.el->type] != 0);
                level->particle_counts[it.el->type]--;
            }

            dynlist_swap_remove_it(sector->particles, it);
        } else {
            particle_tick(level, it.el);
        }
    }

    llist_each(node, &sector->decals, it) {
        decal_tick(level, it.el);
    }

    llist_each(sector_sides, &sector->sides, it) {
        llist_each(node, &it.el->decals, it_d) {
            decal_tick(level, it_d.el);
        }
    }

    // TODO: camera distance through portals
    // TODO TODO: maybe add a sector-min-distance matrix???
    // spawn liquid particles if we're close enough to the camera
    const sector_type_t *ptype = &SECTOR_TYPES[sector->type];
    if (ptype->is_liquid
        && ptype->liquid.particle_amount > 0.0f
        && ptype->liquid.particle_fn
        && v2_distance(v2_from(g->cam.pos), box2f_center(sector->bounds))
            < 24.0f) {
        // convert from particles/unit^2/second -> particles/tick
        const f32 ppt =
            ptype->liquid.particle_amount
                * sector->area
                * (1.0f / TICKS_PER_SECOND);

        const int
            ppt_now = ppt * g->tick,
            ppt_last = ppt * (g->tick - 1);

        for (int i = 0; i < max(ppt_now - ppt_last, 0); i++) {
            const v2 p = sector_rand_point(sector, &g->rand);
            const f32 z = sector_point_zs(sector, p).z0 + sector->liquid_offset;
            ptype->liquid.particle_fn(level, sector, v3_of(p, z));
        }
    }
}

int sector_vertices(
    const level_t *level,
    const sector_t *sector,
    DYNLIST(vertex_t*) *out) {
    const int size_start = dynlist_size(*out);

    BITMAP_DECL_STATIC(visited, MAX_VERTICES);
    bitmap_fill_n(&visited, false, level->vertices.data.capacity);

    llist_each(sector_sides, &sector->sides, it) {
        vertex_t *vs[2] = {
            it.el->wall->v0,
            it.el->wall->v1,
        };

        for (int i = 0; i < 2; i++) {
            if (bitmap_get(&visited, vs[i]->id)) { continue; }
            bitmap_set(&visited, vs[i]->id);
            *dynlist_push(*out) = vs[i];
        }
    }

    return dynlist_size(*out) - size_start;
}

bool sector_contains_vertex(
    const level_t *level,
    const sector_t *sector,
    const vertex_t *vertex) {
    llist_each(sector_sides, &sector->sides, it) {
        vertex_t *vs[2];
        side_get_vertices(it.el, vs);
        if (vertex == vs[0] || vertex == vs[1]) { return true; }
    }
    return false;
}

bool sector_contains_point(
        const sector_t *sector,
        v2 p) {
    if (p.x < sector->min.x
        || p.x > sector->max.x
        || p.y < sector->min.y
        || p.y > sector->max.y) {
        return false;
    }

    // find subsector
    // NOTE: this might look slower than doing a PIP/line intersection test,
    // but in practice the performacne is about the same since the point_side()
    // comparisons are so cheap.
    llist_each(sector_node, &sector->subs, it) {
        subsector_t *sub = it.el;
        ASSERT(sub->tris);
        ASSERT(sub->neighbors);
        ASSERT(sub->lines);
        if (p.x >= sub->min.x
            && p.x <= sub->max.x
            && p.y >= sub->min.y
            && p.y <= sub->max.y
            && subsector_contains_point(sub, p)) {
            return true;
        }
    }

    return false;
}

v2 sector_rand_point(const sector_t *sector, rand_t *rand) {
    if (dynlist_size(sector->tris) == 0) { return v2_of(0); }

    const sect_tri_t *tri = &sector->tris[0];

    // find random triangle distributed by area
    const f32 cutoff = rand_f32(rand, 0.0f, sector->area);
    f32 acc = 0.0f;

    dynlist_each(sector->tris, it) {
        acc += it.el->area;

        if (cutoff <= acc) {
            tri = it.el;
            break;
        }
    }

    ASSERT(tri);

    return rand_v2_triangle(rand, tri->a->pos, tri->b->pos, tri->c->pos);
}

v2 subsector_clamp_point(const subsector_t *sub, v2 point) {
#define SUBSECTOR_CLAMP_FUDGE 0.000001f

    // if point is inside (right side of all lines) this remains unmodified
    v2 p_best = point;
    f32 d_best = 1e10f;

    dynlist_each(sub->lines, it) {
        if (point_side(point, it.el->a->pos, it.el->b->pos) <= 0.0f) {
            // right side, ignore
            continue;
        }

        const v2 p = point_project_segment(point, it.el->a->pos, it.el->b->pos);
        const f32 d = v2_norm2(v2_sub(point, p));
        if (d < d_best) {
            d_best = d;
            p_best =
                v2_add(
                    p,
                    v2_scale(
                        line_right_normal(it.el->a->pos, it.el->b->pos),
                        SUBSECTOR_CLAMP_FUDGE));
        }
    }

    return p_best;
}

v2 sector_clamp_point(const sector_t *sector, v2 point) {
    // if not inside the sector, pick the best (closest) clamped point
    v2 p_best = point;
    f32 d_best = 1e10f;

    // check each subsector
    llist_each(sector_node, &sector->subs, it_s) {
        bool inside = true;

        // check each line
        dynlist_each(it_s.el->lines, it_l) {
            if (point_side(
                    point,
                    it_l.el->a->pos,
                    it_l.el->b->pos) < 0.0f) {
                // right side
                continue;
            }

            inside = false;
            const v2 p =
                point_project_segment(
                    point,
                    it_l.el->a->pos,
                    it_l.el->b->pos);
            const f32 d = v2_norm(v2_sub(p, point));
            if (d < d_best) {
                p_best = p;
                d_best = d;
            }
        }

        if (inside) {
            // contained entirely in this subsector, point is OK
            return point;
        }
    }

    // not inside any subsector, so return best clamped point
    return p_best;
}

v2 sector_project_onto_edge(const sector_t *sector, v2 point) {
    // project onto each side, pick nearest
    v2 p = point;
    f32 d = 1e10;

    llist_each(sector_sides, &sector->sides, it) {
        const v2 projected =
            point_project_segment(
                point,
                it.el->wall->v0->pos,
                it.el->wall->v1->pos);

        const f32 dist = v2_distance(point, projected);
        if (dist < d) {
            p = projected;
            d = dist;
        }
    }

    return p;
}

bool sector_intersects_line(
        const sector_t *sector,
        v2 a,
        v2 b) {
    if (sector_contains_point(sector, a)) { return true; }
    else if (sector_contains_point(sector, b)) { return true; }

    llist_each(sector_node, &sector->subs, it_s) {
        dynlist_each(it_s.el->lines, it_l) {
            if (intersect_segs(
                    a, b, it_l.el->a->pos, it_l.el->b->pos, NULL, NULL, NULL)) {
                return true;
            }
        }
    }

    return false;
}

bool sector_intersects_box2f(
    const sector_t *sector,
    box2f_t box) {
    if (!box2f_collides(box, box2f_mm(sector->min, sector->max))) {
        // rough failure check with large box
        return false;
    } else if (box2f_contains_other(box, box2f_mm(sector->min, sector->max))) {
        // rough contains check with large box
        return true;
    }

    // finer check with subsector boxes and finally triangles
    llist_each(sector_node, &sector->subs, it) {
        if (!box2f_collides(box, box2f_mm(it.el->min, it.el->max))) {
            continue;
        }

        dynlist_each(it.el->tris, it) {
            const sect_tri_t *tri = *it.el;
            if (box2f_vs_triangle(box, tri->a->pos, tri->b->pos, tri->c->pos)) {
                return true;
            }
        }
    }

    return false;
}

bool sector_contained_by_box2f(
    const sector_t *sector,
    box2f_t box) {
    dynlist_each(sector->tris, it) {
        if (!box2f_contains(box, it.el->a->pos)
            || !box2f_contains(box, it.el->b->pos)
            || !box2f_contains(box, it.el->c->pos)) {
            return false;
        }
    }
    return true;
}

int sector_get_sides(
    const level_t *level,
    const sector_t *sector,
    DYNLIST(side_t*) *sides) {
    const int offset = dynlist_size(*sides);
    dynlist_resize(*sides, offset + sector->n_sides);

    int i = 0;
    llist_each(sector_sides, &sector->sides, it) {
        (*sides)[offset + i] = it.el;
        i++;
    }

    ASSERT(i == sector->n_sides);
    return i;
}

void sector_delete_with_sides(level_t *level, sector_t *s) {
    sector_delete(level, s);

    // remove all sides for this sector (this will also update portals)
    DYNLIST(side_t*) sides = dynlist_create(side_t*, &g->frame_arena);
    sector_get_sides(level, s, &sides);

    dynlist_each(sides, it) {
        side_delete(level, *it.el);
    }
}

void sector_copy_props(level_t *level, sector_t *dst, const sector_t *src) {
    for (int i = 0; i < 2; i++) {
        dst->planes[i].z = src->planes[i].z;
        dst->planes[i].light = src->planes[i].light;
        dst->planes[i].slope = src->planes[i].slope;
    }
    // TODO: dst->funcdata = src->funcdata;
    dst->flags = src->flags;
    dst->mat = src->mat;
    sector_try_set_like(level, dst, src->like);
}

bool sector_try_set_like(
        const level_t *level,
        sector_t *sector,
        sector_t *like) {
    ASSERT_DEBUG(sector);

    // find "true" like
    while (like && like->like) {
        like = like->like;
    }

    if (like == sector) {
        // loop
        return false;
    }

    sector->like = like;
    sector->version++;
    return true;
}

sector_t *sector_get_like(const sector_t *sector) {
    sector_t *like = sector->like;
    while (like && like->like) {
        like = like->like;
    }
    return like;
}

void sector_traverse_neighbors(
        level_t *level,
        sector_t *sector,
        sector_traverse_neighbors_f callback,
        void *userdata) {
    BITMAP_DECL_STATIC(visited, MAX_SECTORS);
    bitmap_fill_n(&visited, 0, level->sectors.data.capacity);

    DYNLIST(sector_t*) queue = dynlist_create(sector_t*, &g->frame_arena);
    *dynlist_push(queue) = sector;

    while (dynlist_size(queue) != 0) {
        sector_t *s = dynlist_pop(queue);
        bitmap_set(&visited, s->id);

        if (!callback(level, s, userdata)) {
            return;
        }

        dynlist_each(s->neighbors, it) {
            if (!bitmap_get(&visited, (*it.el)->id)) {
                *dynlist_push(queue) = *it.el;
            }
        }
    }

    dynlist_destroy(queue);
}

// compute a hash of all elements which could potentially affect level PVSs
static hash_t compute_pvs_hash(level_t *level, sector_t *sector) {
    hash_t hash = 0x12345;

    llist_each(sector_sides, &sector->sides, it) {
        hash = hash_add_v2(hash, it.el->wall->v0->pos);
        hash = hash_add_v2(hash, it.el->wall->v1->pos);
        hash = hash_add_int(hash, it.el->portal ? it.el->portal->id : -1);
    }

    return hash;
}

// TODO: occasionally fails to do its job since the ordering of sides/neighbors
// can change - if unexpected sector retesselation becomes a perf issue,
// consider sorting sides/neighbors before computing the tess hash
//
// compute a hash of all elements used to tesselate sector geometry
static hash_t compute_tess_hash(level_t *level, sector_t *sector) {
    hash_t hash = 0x12345;

    const sectmat_data_t mat = sector_get_mat(sector);

    // render-affecting sector flags
    hash = hash_add_bool(hash, mat.flags & SCMF_SKY);

    hash = hash_add_int(hash, sector->type);
    hash = hash_add_f32(hash, sector->liquid_offset);

    for (int i = 0; i < 2; i++) {
        hash = hash_add_f32(hash, sector->planes[i].z);
        hash = hash_add_f32(hash, sector->planes[i].slope);
        hash = hash_add_ptr(hash, sector->planes[i].slope_side);
    }

    llist_each(sector_sides, &sector->sides, it) {
        const sidemat_data_t side_mat = side_get_mat(it.el);

        hash = hash_add_int(hash, side_mat.flags & (SDMF_SKY | SDMF_TRUE_COLOR));
        hash = hash_add_v2(hash, it.el->wall->v0->pos);
        hash = hash_add_v2(hash, it.el->wall->v1->pos);
        hash = hash_add_int(hash, it.el->portal ? it.el->portal->id : -1);

        if (it.el->is_disconnect && it.el->portal) {
            hash = hash_combine(hash, it.el->portal->seg_hash);
        }

        hash = hash_combine(hash, it.el->seg_hash);
    }

    dynlist_each(sector->neighbors, it) {
        hash = hash_add_int(hash, (*it.el)->id);
    }

    return hash;
}

static void sector_update_geometry(level_t *level, sector_t *sect) {
    level->version++;
    sect->version++;

    ASSERT_DEBUG(!sect->lflags.mark);

    const v2 old_min = sect->min, old_max = sect->max;
    v2 p_min = v2_of(1e10), p_max = v2_of(0);
    f32 z_min = 1e10f, z_max = -1e10f;
    int n_sides = 0;

    bool failed = false;

    // recalculate neighbors
    dynlist_resize(sect->neighbors, 0);

    // recalculate disconnected portal sides
    dynlist_resize(sect->disconnected_portals, 0);

    // recalcuate pvs outgoing sides
    dynlist_resize(sect->pvs_outgoing, 0);

    // validate sides, update bounds, update neighbors
    llist_each(sector_sides, &sect->sides, it) {
        side_t *side = it.el;
        ASSERT_DEBUG(side->sector == sect);
        ASSERT_DEBUG(!side->lflags.mark);

        wall_t *w = side->wall;
        const v2 a = w->v0->pos, b = w->v1->pos;

        if (!w) { WARN("wall-less side %d", w->id); return; }
        p_min.x = min(p_min.x, min(a.x, b.x));
        p_min.y = min(p_min.y, min(a.y, b.y));
        p_max.x = max(p_max.x, max(a.x, b.x));
        p_max.y = max(p_max.y, max(a.y, b.y));
        n_sides++;

        const rangef_t
            zs_a = sector_point_zs(sect, a),
            zs_b = sector_point_zs(sect, b),
            zs = { .z0 = min(zs_a.z0, zs_b.z0), .z1 = max(zs_a.z1, zs_b.z1) };

        z_min = min(zs.z0, z_min);
        z_max = max(zs.z1, z_max);

        if (side->portal && side->portal->sector) {
            if (side->is_disconnect) {
                *dynlist_push(sect->disconnected_portals) = side;
            } else if (side->portal->sector != sect) {
                // add to neighbors list if not a self-portal
                bool found = false;

                // ensure no duplicates
                dynlist_each(sect->neighbors, it) {
                    if (side->portal->sector == *it.el) {
                        found = true;
                    }
                }

                if (!found) {
                    *dynlist_push(sect->neighbors) = side->portal->sector;
                }

                // always add to PVS outgoing set
                *dynlist_push(sect->pvs_outgoing) = side;
            }
        }
    }

    sect->n_sides = n_sides;
    sect->min = p_min;
    sect->max = p_max;
    sect->bounds_3d = (box3f_t) {
        .min = v3_of(p_min, z_min),
        .max = v3_of(p_max, z_max)
    };

    if (n_sides == 0) {
        return;
    }

    // skip sector tesselation if geometry is exactly the same
    const hash_t tess_hash = compute_tess_hash(level, sect);
    if (tess_hash == sect->tess_hash) {
        goto tesselate_done;
    }
    sect->tess_hash = tess_hash;

    // if tess hash changed, check if PVS hash changed
    const hash_t pvs_hash = compute_pvs_hash(level, sect);
    if (pvs_hash != sect->pvs_hash) {
        sect->pvs_hash = pvs_hash;
        sector_matrices_recompute(level);
    }

    // all old blocks need to update, as well as all new ones
    level_traverse_block_area(
        level,
        sect->min,
        sect->max,
        level_traverse_blocks_bump_version,
        NULL,
        LTB_NONE);

    level_traverse_block_area(
        level,
        p_min,
        p_max,
        level_traverse_blocks_bump_version,
        NULL,
        LTB_NONE);

    DYNLIST(side_t*) sides =
        dynlist_create(side_t*, &g->frame_arena, sect->n_sides);
    DYNLIST(side_t*) sorted_sides =
        dynlist_create(side_t*, &g->frame_arena, sect->n_sides);

    // vertex to feed into libtess2
    // "data" is actual coordinate data (2D since we tesselate on XY plane)
    // "v" is associated map vertex
    typedef struct {
        f32 data[2];
        vertex_t *v;
    } tess_vertex_t;

    DYNLIST(tess_vertex_t) tess_vertices =
        dynlist_create(tess_vertex_t, &g->frame_arena, sect->n_sides * 2);

    // sort sides along traces
    int n_sorted = 0;
    sector_get_sides(level, sect, &sides);

    // re-tesselate
    dynlist_resize(sect->tris, 0);

    // remove all subsector data, zero list
    llist_each(sector_node, &sect->subs, it) {
        subsector_destroy(level, it.el);
    }

    llist_init(&sect->subs);

    TESSalloc tess_alloc = { 0 };
    tess_make_allocator(&tess_alloc, &g->frame_arena);

    TESStesselator *tess = tessNewTess(&tess_alloc);
    tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
    tessSetOption(tess, TESS_WINDING_POSITIVE, 1);

    int count = 0;

    while (n_sorted != sect->n_sides) {
        // find non-sorted (non-marked) side
        side_t *start = NULL;
        for (int i = 0; i < sect->n_sides; i++) {
            if (!sides[i]->lflags.mark) {
                start = sides[i];
                break;
            }
        }

        ASSERT(start);

        DYNLIST(side_t*) trace = dynlist_create(side_t*, &g->frame_arena);
        if (!level_trace_sides(level, start, &trace, NULL)) {
            WARN(
                "failure to construct coherent sector (trace starting from %d)",
                start->id);
            failed = true;
            goto done_sorting;
        }

        if (n_sorted + dynlist_size(trace) > sect->n_sides) {
            WARN(
                "too many traced sides for sector (have %d cannot add %d max %d)",
                n_sorted,
                dynlist_size(trace),
                sect->n_sides);
            failed = true;

            dynlist_each(trace, it) {
                level_push_dirty_sect_side(level, *it.el);
            }

            goto done_sorting;
        }

        dynlist_each(trace, it) {
            if ((*it.el)->sector != sect) {
                level_update_side_sector(level, *it.el);
                WARN("got non-sector side in trace?");
                failed = true;
                goto done_sorting;
            }
        }

        // insert trace into sorted sides in order while marking AND inserting
        // as contour
        const int contour_start = count;
        dynlist_each(trace, it) {
            side_t *side = *it.el;

            if (side->sector != sect) {
                // move side sector
                sector_t *cur_sect = side->sector;
                cur_sect->lflags.do_not_recalc = true;
                sector_remove_side(level, cur_sect, side);
                cur_sect->lflags.do_not_recalc = false;

                sect->lflags.do_not_recalc = true;
                sector_add_side(level, sect, side);
                sect->lflags.do_not_recalc = false;
            }
            ASSERT(side->sector == sect);

            if (side->lflags.mark) {
                WARN("traced into already found side %d", side->id);
                failed = true;
                goto done_sorting;
            }

            side->lflags.mark = true;
            *dynlist_push(sorted_sides) = side;
            n_sorted++;

            // insert into contour
            vertex_t *vs[2];
            side_get_vertices(side, vs);

            tess_vertices[count] = (tess_vertex_t) {
                .data = {
                    vs[0]->pos.x,
                    vs[0]->pos.y,
                },
                .v = vs[0]
            };
            count++;
        }

        // add all new vertices as contour
        tessAddContour(
            tess,
            2,
            &tess_vertices[contour_start],
            sizeof(tess_vertex_t),
            count - contour_start);
    }

done_sorting:
    if (failed) {
        // unmark all
        for (int i = 0; i < sect->n_sides; i++) {
            sides[i]->lflags.mark = false;
        }

        WARN("failed to tesselate sector %d", sect->id);
        return;
    }

    // insert in reverse order, as we are prepending
    llist_init(&sect->sides);
    int m = 0;
    for (int i = n_sorted - 1; i >= 0; i--) {
        llist_init_node(&sorted_sides[i]->sector_sides);
        llist_prepend(sector_sides, &sect->sides, sorted_sides[i]);
        sorted_sides[i]->lflags.mark = false;
        m++;
    }

    ASSERT(m == sect->n_sides);

    // sides are now sorted and tesselated
    const int tess_result =
        tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS, 3, 2, NULL);
    ASSERT(
        tess_result,
        "got bad tess for sector %d (%d)",
        sect->id,
        tess_result);

    // get tris from tesselation, compute sector area
    sect->area = 0.0f;

    const int *tess_vert_indices = tessGetVertexIndices(tess);
    const int *tess_elems = tessGetElements(tess);
    const int n_tess_elems = tessGetElementCount(tess);

    for (int i = 0; i < n_tess_elems; i++) {
        const int tv_indices[3] = {
            tess_vert_indices[tess_elems[i * 3 + 0]],
            tess_vert_indices[tess_elems[i * 3 + 1]],
            tess_vert_indices[tess_elems[i * 3 + 2]],
        };

        // check that indices are sane
        bool failed = false;
        for (int j = 0; j < 3; j++) {
            if (tv_indices[j] == TESS_UNDEF
                || tv_indices[j] < 0
                || tv_indices[j] >= sect->n_sides * 2) {
                WARN("unable to mesh sector %d", sect->id);
                failed = true;
                break;
            }
        }

        if (failed) {
            dynlist_resize(sect->tris, 0);
            sect->area = 0.0f;
            break;
        }

        sect_tri_t tri = {
            .vs = {
                tess_vertices[tv_indices[0]].v,
                tess_vertices[tv_indices[1]].v,
                tess_vertices[tv_indices[2]].v,
            },
        };

        tri.area = triangle_area(tri.c->pos, tri.b->pos, tri.a->pos);

        *dynlist_push(sect->tris) = tri;
    }

    tessDeleteTess(tess);

    if (n_sides == 0) {
        WARN("no sides?");
        sect->n_sides = 0;
        sect->min = v2_of(NAN);
        sect->max = v2_of(NAN);
        return;
    }

    // convexify
    DYNLIST(subsector_t) subs = dynlist_create(subsector_t, &g->frame_arena);
    convexify_tris(level, &sect->tris, &subs);

    // assign subsectors IDs, add to sector, etc.
    dynlist_each(subs, it) {
        subsector_add(level, sect, it.el);
    }

    // compute triangle zs, volume
    dynlist_each(sect->tris, it) {
        // get rangefs at each triangle point
        it.el->zs[0] = sector_point_zs(sect, it.el->vs[0]->pos),
        it.el->zs[1] = sector_point_zs(sect, it.el->vs[1]->pos),
        it.el->zs[2] = sector_point_zs(sect, it.el->vs[2]->pos);

        // average height across triangle
        it.el->approx_height =
            ((it.el->zs[0].z1 + it.el->zs[1].z1 + it.el->zs[2].z1) / 3.0f)
                - ((it.el->zs[0].z0 + it.el->zs[1].z0 + it.el->zs[2].z0) / 3.0f);

        it.el->approx_volume = it.el->area * it.el->approx_height;
    }

    // compute subsector volume
    llist_each(sector_node, &sect->subs, it_s) {
        it_s.el->volume = 0.0f;
        dynlist_each(it_s.el->tris, it_t) {
            it_s.el->volume += (*it_t.el)->approx_volume;
        }
    }

    bool did_reset_blocks = false;

    const bool bounds_change =
        !v2_eqv_eps(old_min, sect->min)
        || !v2_eqv_eps(old_max, sect->max);

    if (bounds_change) {
        // optimization opportunity:
        // this can be done more effeciently, check if current and previous
        // bounds are entirely contained within existing boundaries
        // recalculate overall level bounds on change
        const box2f_t old_level_bounds = level->bounds;
        level->bounds.min = v2_of(MAX_COORD);
        level->bounds.max = v2_of(0);
        level_each(sector_t, &level->sectors, it) {
            level->bounds.min = v2_minv(level->bounds.min, it.el->min);
            level->bounds.max = v2_maxv(level->bounds.max, it.el->max);
        }

        if (!v2_eqv_eps(level->bounds.min, old_level_bounds.min)
            || !v2_eqv_eps(level->bounds.max, old_level_bounds.max)) {
            blocks_reset(level);
            did_reset_blocks = true;
        }
    }

    if (!did_reset_blocks) {
        if (!v2_eqv_eps(sect->min, old_min)
            || !v2_eqv_eps(sect->max, old_max)) {
            level_update_sector_blocks(level, sect);
        }

        // assign subsectors into block map
        llist_each(sector_node, &sect->subs, it) {
            level_set_subsector_blocks(level, it.el);
        }
    }

    // calculate subsector neighbors
    llist_each(sector_node, &sect->subs, it) {
        // find neighbors
        level_traverse_block_area(
            level,
            v2_sub(it.el->min, v2_of(0.1f)),
            v2_add(it.el->max, v2_of(0.1f)),
            (traverse_blocks_f) subsector_traverse_block_neighbors,
            it.el,
            LTB_NONE);

        // find portal neighbors
        dynlist_each(it.el->lines, it_l) {
            const side_t *side = it_l.el->side;
            if (side
                && side->is_disconnect
                && side->portal
                && side->portal->subsector) {
                // do not add if already present
                bool found = false;
                dynlist_each(it.el->neighbors, it_n) {
                    if (it_n.el->sub == side->portal->subsector) {
                        found = true;
                        break;
                    }
                }

                if (found) {
                    continue;
                }

                // add a portal neighbor
                *dynlist_push(it.el->neighbors) =
                    (subsector_neighbor_t) {
                        .sub = side->portal->subsector,
                        .line = it_l.el,
                        .is_portal = true,
                    };

                *dynlist_push(side->portal->subsector->neighbors) =
                    (subsector_neighbor_t) {
                        .sub = it.el,
                        .line = side->portal->sect_line,
                        .is_portal = true,
                    };
            }
        }
    }

    llist_each(sector_node, &sect->subs, it_s) {
        ASSERT_DEBUG(!it_s.el->entities.head);
    }

    // update entity subsectors
    dlist_each(sector_node, &sect->entities, it_e) {
        it_e.el->subsector = NULL;
        dlist_init_node(&it_e.el->subsector_node);

        llist_each(sector_node, &sect->subs, it_s) {
            if (subsector_contains_point(it_s.el, it_e.el->pos)) {
                it_e.el->subsector = it_s.el;
                dlist_prepend(subsector_node, &it_s.el->entities, it_e.el);
                break;
            }
        }

        if (!it_e.el->subsector) {
            WARN("entity %d missing subsector", it_e.el->id);
        }
    }

tesselate_done:;
    // update sides
    llist_each(sector_sides, &sect->sides, it) {
        side_recalculate(level, it.el);

        if (it.el->portal) {
            side_recalculate(level, it.el->portal);
        }
    }

    dynlist_each(sect->pvs_outgoing, it) {
        if (!(*it.el)->sect_line) {
            WARN(
                "removing side %d from sector %d pvs_outgoing - no sect line?",
                (*it.el)->id,
                sect->id);
            dynlist_remove_it(sect->pvs_outgoing, it);
        }
    }
}

void sector_recalculate(level_t *level, sector_t *sector) {
    if (sector->lflags.do_not_recalc) { return; }
    sector->lflags.recalc_enqueued = false;

    const hash_t old_tess_hash = sector->tess_hash;

    // update our geometry
    sector_update_geometry(level, sector);

    if (sector->n_sides == 0) {
        LOG("sector %d has no sides, deleting", sector->id);
        level_enqueue_delete(level, lptr_from(sector));
        return;
    }

    // update our neighbors geometry if any tesselation info changed
    if (sector->tess_hash != old_tess_hash) {
        dynlist_each(sector->neighbors, it) {
            sector_update_geometry(level, *it.el);
        }

        dynlist_each(sector->disconnected_portals, it) {
            if ((*it.el)->sector) {
                sector_update_geometry(level, (*it.el)->sector);
            }
        }
    }
}

subsector_t *sector_find_subsector(sector_t *sector, v2 point) {
    llist_each(sector_node, &sector->subs, it) {
        dynlist_each(it.el->lines, it_l) {
            // must be right or on (<= 0) to be inside
            if (point_side(point, it_l.el->a->pos, it_l.el->b->pos) > 0) {
                goto next_sub;
            }
        }

        return it.el;
next_sub:
    }

    return NULL;
}

void sector_add_side(
    level_t *level,
    sector_t *sector,
    side_t *side) {
    ASSERT(!side->sector);

    llist_prepend(sector_sides, &sector->sides, side);

    side->sector = sector;
    sector->n_sides++;

    side_recalculate(level, side);
    sector_recalculate(level, sector);
}

void sector_remove_side(
        level_t *level,
        sector_t *sector,
        side_t *side) {
    ASSERT(side->sector == sector);

    for (int i = 0; i < 2; i++) {
        if (sector->planes[i].slope_side == side) {
            sector->planes[i].slope_side = NULL;
        }
    }

    llist_remove(sector_sides, &sector->sides, side);

    side->sector = NULL;
    side->subsector = NULL;

    if (side->sect_line) {
        side->sect_line->side = NULL;
    }

    sector->n_sides--;

    // sectors are marked during deletion
    if (!sector->lflags.mark) {
        if (sector->n_sides == 0) {
            sector_delete(level, sector);
        } else {
            sector_recalculate(level, sector);
        }
    }

    side_recalculate(level, side);
}

bool subsector_contains_point(const subsector_t *sub, v2 point) {
    // subsector contains if right side of all lines
    dynlist_each(sub->lines, it) {
        if (point_side(point, it.el->a->pos, it.el->b->pos) > 0) {
            return false;
        }
    }

    return true;
}

v3 sector_plane_normal(
        const sector_t *sector,
        plane_type_e plane) {
    const plane_t *pl = &sector->planes[plane];
    if (!pl->slope_side
        || !pl->slope_side->wall
        || !pl->slope_side->sector) {
        return plane == PLANE_TYPE_CEIL ? v3_of(0, 0, -1) : v3_of(0, 0, 1);
    } else if (llist_empty(&sector->subs)) {
        return v3_of(1, 0, 0);
    }

    // convex subsectors always contain their center, use point to get normal
    const subsector_t *first = sector->subs.head;
    v3 p =
        v3_of(
            v2_lerp(first->min, first->max, 0.5f),
            0.0f);

    // form a small basis around the point with +x and +y
    v3
        q = v3_of(v2_add(v2_from(p), v2_of(0.1f, 0.0f)), 0.0f),
        r = v3_of(v2_add(v2_from(p), v2_of(0.0f, 0.1f)), 0.0f);

    v3 *vs[3] = { &p, &q, &r };
    for (int i = 0; i < 3; i++) {
        vs[i]->z = sector_point_zs(sector, v2_from(*vs[i])).zs[plane];
    }

    if (plane == PLANE_TYPE_CEIL) {
        swap(q, r);
    }

    return
        v3_normalize(
            v3_cross(v3_sub(q, p), v3_sub(r, p)));
}

v4 sector_plane_vec(
    const sector_t *sector,
    plane_type_e plane) {
    if (llist_empty(&sector->subs)) { return v4_of(0); }

    // use point inside of convex subsector as 3D point on plane
    const subsector_t *first = sector->subs.head;
    const v2 p = v2_lerp(first->min, first->max, 0.5f);

    const v3 n = sector_plane_normal(sector, plane);

    // get 3D point on plane
    const v3 q = v3_of(p, sector_point_zs(sector, p).zs[plane]);

    // solve for d (w) using ax + by + cz + d = 0
    return v4_of(n, -v3_dot(n, q));
}

rangef_t sector_point_zs(const sector_t *sector, v2 point) {
    rangef_t res = { 0 };
    const f32 bases[2] = { sector->floor.z, sector->ceil.z };

    for (int i = 0; i < 2; i++) {
        side_t *slope_side = sector->planes[i].slope_side;

        if (!slope_side
            || !slope_side->wall
            || !slope_side->sector) {
            res.zs[i] = bases[i];
            continue;
        }

        vertex_t *slope_vs[2];
        side_get_vertices(slope_side, slope_vs);

        if (fabsf(sector->planes[i].slope) < 0.0000001f) {
            res.zs[i] = bases[i];
        } else {
            // find distance to side
            const v2 proj =
                point_project_line(
                    point,
                    slope_vs[0]->pos,
                    slope_vs[1]->pos);

            const f32 dist = v2_distance(point, proj);

            // tan(theta) = dy/dx
            // dx is dist
            // dx * tan(theta) = dy
            res.zs[i] = bases[i] + (dist * tanf(sector->planes[i].slope));
        }
    }

    return res;
}

rangef_t sector_vertex_zs(const sector_t *sector, vertex_t *vertex) {
    return sector_point_zs(sector, vertex->pos);
}

f32 sector_clamp_z(const sector_t *sector, v2 point, f32 z) {
    const rangef_t zr = sector_point_zs(sector, point);
    return clamp(z, zr.z0, zr.z1);
}

f32 sector_clamp_z_h(const sector_t *sector, v2 point, f32 z, f32 h) {
    const rangef_t zr = sector_point_zs(sector, point);
    return clamp(z, zr.z0, zr.z1 - h);
}

void sector_shared_walls(
    const sector_t *a, const sector_t *b, DYNLIST(wall_t*) *out) {
    llist_each(sector_sides, &a->sides, it) {
        if (it.el->portal && it.el->portal->sector == b) {
            *dynlist_push(*out) = it.el->wall;
        }
    }
}

void sector_closest_points(
    const level_t *level,
    const sector_t *a,
    const sector_t *b,
    v2 ps[2]) {
    if (!a->sides.head || !b->sides.head) {
        WARN(
            "attempt to sector_closest_points with a bad sector %d / %d",
            a->id,
            b->id);
        ps[0] = v2_of(NAN);
        ps[1] = v2_of(NAN);
        return;
    } else if (a == b) {
        ps[0] = wall_midpoint(a->sides.head->wall);
        ps[1] = ps[0];
        return;
    }

    line2f_t closest;
    f32 closest_len2 = 1e10f;

    line2f_t la, lb;
    llist_each(sector_sides, &a->sides,  it_a) {
        la = (line2f_t) {
            .a = it_a.el->wall->v0->pos,
            .b = it_a.el->wall->v1->pos,
        };
        llist_each(sector_sides, &b->sides, it_b) {
            lb = (line2f_t) {
                .a = it_b.el->wall->v0->pos,
                .b = it_b.el->wall->v1->pos,
            };

            const line2f_t l = segments_closest_line(la, lb);
            const f32 len2 = v2_distance2(l.a, l.b);
            if (len2 < closest_len2) {
                closest_len2 = len2;
                closest = l;
            }
        }
    }

    ps[0] = closest.a;
    ps[1] = closest.b;
}

static void do_infinite_fall(level_t *level, sector_t *sector, entity_t *ent) {
    const v2_diff_t diff = v2_diff(ent->pos, box2f_center(sector->bounds));

    ent->vel =
        v2_add(
            ent->vel,
            v2_scale(
                diff.dir,
                150.0f * diff.dist * g->time.fixed.dt_scaled));

    const f32 target_vel_z = -20.0f;

    if (ent->vel_z > target_vel_z) {
        ent->vel_z += (10.0f * target_vel_z) * g->time.fixed.dt_scaled;
    }

    const f32 height = entity_height(level, ent);

    if (ent->z < sector->floor.z + height + 1.0f) {
        ent->z = sector->floor.z + height + 5.0f;
        ent->vel_z = clamp_mag(ent->vel_z, target_vel_z);
    }
}

static void entry_juice_inside(
        level_t *level,
        sector_t *sector,
        entity_t *ent) {
    if (ent->itype == ENTITY_TYPE_PLAYER) {
        const bool below_surface =
            g->cam.pos.z < sector->floor.z + sector->liquid_offset;

        g_fingers->mode =
            below_surface
                && secs_since_tick(ent->last_liquid_teleport_tick) >= 1.0f ?
            FINGERS_MODE_EDIT
            : FINGERS_MODE_SHOOT;

        do_infinite_fall(level, sector, ent);
    } else {
        ent->health = 0.0f;
    }
}

static void entry_juice_on_enter(
        level_t *level,
        sector_t *sector,
        entity_t *ent) {
    if (ent->itype != ENTITY_TYPE_PLAYER) { return; }
    g_fingers->mode = FINGERS_MODE_EDIT;
    renderer_add_tint(
        &(screen_tint_t) {
            .duration = 1.0f,
            .fade = true,
            .color = v4_of(1.6f, 0.3f, 0.2f, 1.0f),
        });
}

static void entry_juice_on_exit(
        level_t *level,
        sector_t *sector,
        entity_t *ent) {
    if (ent->itype != ENTITY_TYPE_PLAYER) { return; }
    g_fingers->mode = FINGERS_MODE_SHOOT;
}

static void exit_juice_inside(
        level_t *level,
        sector_t *sector,
        entity_t *ent) {
    if (ent->itype == ENTITY_TYPE_PLAYER) {
        do_infinite_fall(level, sector, ent);
    } else {
        ent->health = 0.0f;
    }
}

static void exit_juice_on_enter(
        level_t *level,
        sector_t *sector,
        entity_t *ent) {
    if (ent->itype != ENTITY_TYPE_PLAYER) { return; }
    renderer_add_tint(
        &(screen_tint_t) {
            .duration = 1.0f,
            .fade = true,
            .color = v4_of(1.6f, 0.3f, 0.2f, 1.0f),
        });
}

static void hub_juice_on_enter(
        level_t *level,
        sector_t *sector,
        entity_t *ent) {
    if (ent->itype != ENTITY_TYPE_PLAYER) { return; }
    renderer_add_tint(
        &(screen_tint_t) {
            .duration = 1.0f,
            .fade = true,
            .color = v4_of(1.6f, 0.3f, 0.2f, 1.0f),
        });

    if (str_is_empty(sector->teleport_exit_level)) {
        WARN("no teleport_exit_level");
        if (secs_since_tick(ent->last_liquid_teleport_tick) >= 1.0f) {
            entity_do_liquid_exit(level, ent);
        }
    } else {
        // teleport to level
        strbuf_setf(&g->teleport_level, "%s", sector->teleport_exit_level);
    }
}

sector_type_t SECTOR_TYPES[SECTOR_TYPE_COUNT] = {
    [SECTOR_TYPE_PAIN_JUICE] = {
        .is_liquid = true,
        .liquid = {
            .tex = "p_liquidb",
            .hsv = v3_const(0.5f, 0.0f, 0.0f),
            .tint = v4_const(0.15f, 0.7f, 0.15f, 0.75f),
            .extra_bloom = v4_const(0.15f, 0.5f, 0.15f, 0.1f),
            .damage_ticks = 0.75f * TICKS_PER_SECOND,
            .damage_per_hit = 5.0f,
            .viscosity = 0.0f,
            .has_light = true,
            .light = {
                .attenuation = 1.0f,
                .z_attenuation = 1.25f,
                .power = -0.1f,
                .color = v3_const(0.2f, 0.7f, 0.2f),
                .c1 = 3.0f,
                .c2 = 20.0f,
                .ambient = 0.0f,
            },
            .particle_amount = 0.25f,
            /* .particle_fn = pain_juice_particle */
        },
    },
    [SECTOR_TYPE_ENTRY_JUICE] = {
        .is_liquid = true,
        .liquid = {
            .no_move = true,
            .tex = "p_liquidb",
            .hsv = v3_const(0.0f, -1.0f, 0.1f),
            .tint = v4_const(0.9f, 0.4f, 0.1f, 0.2f),
            .extra_bloom = v4_const(0.2f, 0.2f, 0.2f, 0.15f),
            .viscosity = 0.0f,
            .has_light = true,
            .light = {
                .attenuation = 5.0f,
                .z_attenuation = 10.0f,
                .power = -0.5f,
                .color = v3_const(1.0f),
                .c1 = 1.0f,
                .c2 = 10.0f,
                .ambient = 0.25f,
            },
            .particle_amount = 0.25f,
            /* .particle_fn = pain_juice_particle */
            .inside_fn = entry_juice_inside,
            .on_enter_fn = entry_juice_on_enter,
            .on_exit_fn = entry_juice_on_exit,
        },
    },
    [SECTOR_TYPE_ROOM_ENTRY_JUICE] = {
        .is_liquid = true,
        .liquid = {
            .no_move = true,
            .tex = "p_liquidb",
            .hsv = v3_const(0.0f, -1.0f, 0.1f),
            .tint = v4_const(0.9f, 0.4f, 0.1f, 0.2f),
            .extra_bloom = v4_const(0.2f, 0.2f, 0.2f, 0.15f),
            .viscosity = 0.0f,
            .has_light = true,
            .light = {
                .attenuation = 5.0f,
                .z_attenuation = 10.0f,
                .power = -0.5f,
                .color = v3_const(1.0f),
                .c1 = 1.0f,
                .c2 = 10.0f,
                .ambient = 0.25f,
            },
            .particle_amount = 0.25f,
            .on_exit_fn = entry_juice_on_exit,
        },
    },
    [SECTOR_TYPE_EXIT_JUICE] = {
        .is_liquid = true,
        .liquid = {
            .no_move = true,
            .tex = "p_liquidb",
            .hsv = v3_const(0.4f, 0.4f, -0.2f),
            .tint = v4_const(0.2f, 0.9f, 0.1f, 0.2f),
            .extra_bloom = v4_const(0.5f, 0.1f, 0.1f, 0.15f),
            .viscosity = 0.0f,
            .has_light = true,
            .light = {
                .attenuation = 5.0f,
                .z_attenuation = 10.0f,
                .power = -0.35f,
                .color = v3_const(0.3f, 1.0f, 0.2f),
                .c1 = 1.0f,
                .c2 = 10.0f,
                .ambient = 0.25f,
            },
            .particle_amount = 0.25f,
            .inside_fn = exit_juice_inside,
            .on_enter_fn = exit_juice_on_enter,
        },
    },
    [SECTOR_TYPE_HUB_JUICE] = {
        .is_liquid = true,
        .liquid = {
            .no_move = true,
            .use_custom_hsv = true,
            .tex = "p_liquidb",
            .hsv = v3_const(0.0f, -1.0f, 0.1f),
            .tint = v4_const(0.9f, 0.4f, 0.1f, 0.2f),
            .extra_bloom = v4_const(0.2f, 0.2f, 0.2f, 0.15f),
            .viscosity = 0.0f,
            .has_light = true,
            .light = {
                .attenuation = 5.0f,
                .z_attenuation = 10.0f,
                .power = -0.35f,
                .color = v3_const(1.0f),
                .c1 = 1.0f,
                .c2 = 10.0f,
                .ambient = 0.5f,
            },
            .particle_amount = 0.25f,
            .on_enter_fn = hub_juice_on_enter,
        },
    },
    [SECTOR_TYPE_DOOR] = {
        .is_door = true,
    },
};
