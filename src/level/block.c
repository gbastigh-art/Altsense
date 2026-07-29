#include "level/block.h"
#include "level/level_types.h"
#include "level/level.h"
#include "level/entity.h"
#include "level/sector.h"
#include "level/side.h"

#define BLOCK_FUDGE 0.01f

void level_blocks_remove_sector(level_t *level, sector_t *sect) {
    if (!level->blocks.arr) { return; }

    const v2i
        bmin = sect->block_bounds.min,
        bmax = sect->block_bounds.max;

    for (int by = bmin.y; by <= bmax.y; by++) {
        for (int bx = bmin.x; bx <= bmax.x; bx++) {
            block_t *block = level_get_block(level, v2i_of(bx, by));

            if (!block) {
                WARN("removing sector from bad block %d, %d", bx, by);
                continue;
            }

            dynlist_each(block->sectors, it) {
                if (*it.el == sect) {
                    dynlist_remove_it(block->sectors, it);
                    break;
                }
            }
        }
    }
}

void level_blocks_update_entity(level_t *level, entity_t *ent) {
// uncomment to debug entity blocks
// #define DEBUG_ENTITY_BLOCKS

#ifdef DEBUG_ENTITY_BLOCKS
    // VERIFY current blocks
    for (int i = 0; i < ent->blocks.n; i++) {
        int n = 0;
        for (int j = 0; j < ent->blocks.n; j++) {
            if (ent->blocks.arr[i] == ent->blocks.arr[j]) { n++; }
        }

        ASSERT(
            n == 1,
            "entity %d has block %p more than once",
            ent->index, ent->blocks.arr[i]);
    }

    for (int i = 0; i < ent->blocks.n; i++) {
        int n = 0;
        dynlist_each(ent->blocks.arr[i]->entities, it) {
            if (*it.el == ent) { n++; }
        }

        ASSERT(
            n == 1,
            "found %d duplicated (%d) in block %p",
            ent->index, n, ent->blocks.arr[i]);
    }
#endif

    struct {
        block_t *arr[4];
        bool keep[4];
        int n;
    } old = {
        .n = ent->blocks.n,
        .keep = { 0 },
        .arr = { 0 },
    };

    memcpy(
        old.arr,
        ent->blocks.arr,
        ent->blocks.n * sizeof(ent->blocks.arr[0]) /* NOLINT */);

    // find new blocks
    ent->blocks.n = 0;

    const entity_bounds_t bounds = entity_bounds(level, ent);

    v2 ps[4];
    box2f_points(box2f_ch(ent->pos, v2_of(bounds.radius)), ps);

    for (int i = 0; i < 4; i++) {
        block_t *block = level_get_block(level, level_pos_to_block(ps[i]));

        if (!block) {
            goto next_point;
        }

        // check if we already have this block
        for (int j = 0; j < ent->blocks.n; j++) {
            if (ent->blocks.arr[j] == block) {
                goto next_point;
            }
        }

        bool is_new = true;

        // check if this is a new block for this entity
        for (int j = 0; j < old.n; j++) {
            if (old.arr[j] == block) {
                old.keep[j] = true;
                is_new = false;
                break;
            }
        }

        // if the block is new: add to list
        if (is_new) {
            *dynlist_push(block->entities) = ent;
        }

        ent->blocks.arr[ent->blocks.n++] = block;

next_point:
    }

    // remove from non-kept blocks
    for (int i = 0; i < old.n; i++) {
        if (!old.keep[i]) {
            bool found = false;
            dynlist_each(old.arr[i]->entities, it) {
                if (*it.el == ent) {
                    dynlist_remove_it(old.arr[i]->entities, it);
                    found = true;
                    break;
                }
            }

            ASSERT(found);
        }
    }

#ifdef DEBUG_ENTITY_BLOCKS
    const v2i offset = level->blocks.offset, size = level->blocks.size;
    for (int by = offset.y; by < offset.y + size.y; by++) {
        for (int bx = offset.x; bx < offset.x + size.x; bx++) {
            block_t *block = level_get_block(level, v2i_of(bx, by));

            bool found = false;
            dynlist_each(block->entities, it) {
                if (*it.el == ent) {
                    found = true;
                }
                break;
            }

            if (found) {
                int n_ent = 0;
                for (int i = 0; i < ent->blocks.n; i++) {
                    if (block == ent->blocks.arr[i]) {
                        n_ent++;
                    }
                }

                ASSERT(n_ent == 1, "here!");
            }
        }
    }

    LOG("entity %d is in %d blocks", ent->index, ent->blocks.n);
#endif

    // updated
    ent->update_blocks = false;
}

void level_blocks_remove_entity(level_t *level, entity_t *ent) {
    if (!level->blocks.arr) { return; }

    for (int i = 0; i < ent->blocks.n; i++) {
        bool found = false;
        dynlist_each(ent->blocks.arr[i]->entities, it) {
            if (*it.el == ent) {
                dynlist_remove_it(ent->blocks.arr[i]->entities, it);
                found = true;
                break;
            }
        }

        ASSERT(found);
    }

    ent->blocks.n = 0;
}

void blocks_reset(level_t *level) {
    // dump used memory
    if (allocator_valid(&level->blocks.arena)) {
        heap_allocator_destroy(&level->blocks.arena);
    }

    heap_allocator_init(&level->blocks.arena, &level->arena, NULL);

    // TODO: clean up, these are not precise and rely on floating point rounding
    // now
    const box2f_t bounds =
        box2f_mm(
            v2_maxv(
                v2_sub(level->bounds.min, v2_of(BLOCK_FUDGE)),
                v2_of(0)),
            v2_maxv(
                v2_add(level->bounds.max, v2_of(BLOCK_FUDGE)),
                v2_of(0)));

    level->blocks.size =
        v2i_of(
            max((bounds.max.x - bounds.min.x) / BLOCK_SIZE, 1) + 2,
            max((bounds.max.y - bounds.min.y) / BLOCK_SIZE, 1) + 2);
    level->blocks.offset =
        v2i_of(
            max(bounds.min.x / BLOCK_SIZE, 0),
            max(bounds.min.y / BLOCK_SIZE, 0));

    ASSERT(
        level->blocks.size.x * level->blocks.size.y <= MAX_BLOCKS,
        "out of blocks");

    level->blocks.arr =
        mem_alloc(
            &level->blocks.arena,
            level->blocks.size.x * level->blocks.size.y * sizeof(block_t));

    const v2i offset = level->blocks.offset, size = level->blocks.size;
    for (int by = offset.y; by < offset.y + size.y; by++) {
        for (int bx = offset.x; bx < offset.x + size.x; bx++) {
            *level_get_block(level, v2i_of(bx, by)) = (block_t) {
                .index = by * size.x + bx,
                .version = 0,
                .sectors = dynlist_create(sector_t*, &level->blocks.arena),
                .walls = dynlist_create(wall_t*, &level->blocks.arena),
                .subsectors = dynlist_create(int, &level->blocks.arena),
                .entities = dynlist_create(entity_t*, &level->blocks.arena),
            };
        }
    }

    level_each(sector_t, &level->sectors, it) {
        it.el->block_bounds = box2i_mm(v2i_of(0), v2i_of(0));

        level_update_sector_blocks(level, it.el);

        llist_each(sector_node, &it.el->subs, it_s) {
            level_set_subsector_blocks(level, it_s.el);
        }
    }

    level_each(wall_t, &level->walls, it) {
        level_update_wall_blocks(level, it.el, NULL, NULL);
    }

    // force entities to update blocks
    level_each(entity_t, &level->entities, it) {
        it.el->blocks.n = 0;
        it.el->update_blocks = true;
    }
}

bool level_traverse_blocks_bump_version(
        level_t *level,
        block_t *block,
        v2i,
        void*) {
    block->version++;
    level->blocks.version++;
    return true;
}

void level_traverse_blocks(
    level_t *level,
    v2 from,
    v2 to,
    traverse_blocks_f callback,
    void *userdata,
    int flags) {
    if (!level->blocks.arr) { return; }

    bool add_axes[2] = { false, false };

    if (flags & LTB_BORDER_BOTH) {
        for (int i = 0; i < 2; i++) {
            add_axes[i] =
                (fabsf(to.raw[i] - from.raw[i]) < 0.0000001f)
                    && fmodf(from.raw[i], BLOCK_SIZE) < 0.0000001f;
        }
    }

    // DDA: see lodev.org/cgtutor/raycasting.html
    const v2 dir = v2_normalize(v2_sub(to, from));

    // compute where BLOCK_SIZE == 1
    from = v2_divs(from, BLOCK_SIZE);
    to = v2_divs(to, BLOCK_SIZE);

    v2i
        bpos = v2i_from_v(from),
        bstop = v2i_from_v(to);

    const v2 delta_dist =
        v2_of(
            fabsf(dir.x) <= 0.0000001f ? 1e30 : fabsf(BLOCK_SIZE / dir.x),
            fabsf(dir.y) <= 0.0000001f ? 1e30 : fabsf(BLOCK_SIZE / dir.y));

    v2 side_dist =
        v2_of(
            delta_dist.x
                * (dir.x < 0 ? (from.x - bpos.x) : (bpos.x + 1.0f - from.x)),
            delta_dist.y
                * (dir.y < 0 ? (from.y - bpos.y) : (bpos.y + 1.0f - from.y)));

    const v2i bstep = v2i_of(dir.x < 0 ? -1 : 1, dir.y < 0 ? -1 : 1);

    while (true) {
        block_t *block = level_get_block(level, bpos);

        if (!block) {
            // enable debug outputs if something is wack with collision
            /* WARN("cut of blockmap range at %d, %d", bpos.x, bpos.y); */
            /* dumptrace(stdout); */
            return;
        }

        if (!callback(level, block, bpos, userdata)) {
            return;
        }

        if (flags & LTB_BORDER_BOTH) {
            for (int i = 0; i < 2; i++) {
                if (!add_axes[i]) { continue; }

                const v2i bpos_aa =
                    v2i_add(
                        bpos,
                        ((v2i[]) { v2i_of(-1, 0), v2i_of(0, -1) })[i]);

                block_t *block_aa = level_get_block(level, bpos_aa);

                if (!block_aa) { continue; }

                if (!callback(level, block_aa, bpos_aa, userdata)) {
                    return;
                }
            }
        }

        if (bpos.x == bstop.x && bpos.y == bstop.y) {
            break;
        }

        if (side_dist.x < side_dist.y) {
            side_dist.x += delta_dist.x;
            bpos.x += bstep.x;
        } else {
            side_dist.y += delta_dist.y;
            bpos.y += bstep.y;
        }
    }
}

void level_traverse_block_area(
        level_t *level,
        v2 mi,
        v2 ma,
        traverse_blocks_f callback,
        void *userdata,
        int flags) {
    if (!level->blocks.arr) { return; }

    const v2i
        bmin = level_pos_to_block_clamped(level, mi),
        bmax = level_pos_to_block_clamped(level, ma);

    for (int by = bmin.y; by <= bmax.y; by++) {
        for (int bx = bmin.x; bx <= bmax.x; bx++) {
            block_t *block = level_get_block(level, v2i_of(bx, by));
            if (!block) { WARN("no block? %d %d", bx, by); continue; }

            if (!callback(level, block, v2i_of(bx, by), userdata)) {
                return;
            }
        }
    }
}

static bool update_wall_blocks_traverse_remove(
    level_t *level,
    block_t *block,
    v2i,
    wall_t *w) {
    dynlist_each(block->walls, it) {
        if (*it.el == w) {
            dynlist_remove_it(block->walls, it);
            return true;
        }
    }

    WARN("wall not found in block");
    return true;
}

static bool update_wall_blocks_traverse_add(
    level_t *level,
    block_t *block,
    v2i block_pos,
    wall_t *w) {
    *dynlist_push(block->walls) = w;
    return true;
}

void level_update_wall_blocks(
    level_t *level,
    wall_t *wall,
    const v2 *old_v0,
    const v2 *old_v1) {
    if (old_v0 && old_v1) {
        level_traverse_blocks(
            level,
            *old_v0,
            *old_v1,
            (traverse_blocks_f) update_wall_blocks_traverse_remove,
            wall,
            LTB_BORDER_BOTH);
    }

    level_traverse_blocks(
        level,
        wall->v0->pos,
        wall->v1->pos,
        (traverse_blocks_f) update_wall_blocks_traverse_add,
        wall,
        LTB_BORDER_BOTH);
}

void level_blocks_remove_wall(level_t *level, wall_t *wall) {
    if (!level->blocks.arr) { return; }

    level_traverse_blocks(
        level,
        wall->v0->pos,
        wall->v1->pos,
        (traverse_blocks_f) update_wall_blocks_traverse_remove,
        wall,
        LTB_BORDER_BOTH);
}

void level_update_sector_blocks(
    level_t *level,
    sector_t *sector) {
    // remove from old blocks
    if (!v2i_eqv(sector->block_bounds.min, v2i_of(0))
        || !v2i_eqv(sector->block_bounds.max, v2i_of(0))) {
        const v2i
            bmin = sector->block_bounds.min,
            bmax = sector->block_bounds.max;

        for (int by = bmin.y; by <= bmax.y; by++) {
            for (int bx = bmin.x; bx <= bmax.x; bx++) {
                block_t *block = level_get_block(level, v2i_of(bx, by));
                if (!block) { continue; }

                dynlist_each(block->sectors, it) {
                    if (*it.el == sector) {
                        dynlist_remove_it(block->sectors, it);
                        return;
                    }
                }
            }
        }
    }

    // add to new blocks
    const v2i
        bmin = level_pos_to_block(sector->min),
        bmax = level_pos_to_block(sector->max);

    sector->block_bounds = box2i_mm(bmin, bmax);

    for (int by = bmin.y; by <= bmax.y; by++) {
        for (int bx = bmin.x; bx <= bmax.x; bx++) {
            block_t *block = level_get_block(level, v2i_of(bx, by));
            if (!block) { WARN("out of bounds?"); continue; }

            *dynlist_push(block->sectors) = sector;
        }
    }
}

static bool set_subsector_blocks_traverse(
        level_t *level,
        block_t *block,
        v2i pos,
        subsector_t *sub) {
    *dynlist_push(block->subsectors) = sub->id;
    return true;
}

void level_set_subsector_blocks(level_t *level, subsector_t *sub) {
    level_traverse_block_area(
        level,
        sub->min,
        sub->max,
        (traverse_blocks_f) set_subsector_blocks_traverse,
        sub,
        LTB_NONE);
}

static bool remove_subsector_blocks_traverse(
        level_t *level,
        block_t *block,
        v2i,
        subsector_t *sub) {
    dynlist_each(block->subsectors, it) {
        if (*it.el == sub->id) {
            dynlist_remove_it(block->subsectors, it);
            break;
        }
    }

    return true;
}

void level_blocks_remove_subsector(
        level_t *level,
        subsector_t *sub) {
    level_traverse_block_area(
        level,
        sub->min,
        sub->max,
        (traverse_blocks_f) remove_subsector_blocks_traverse,
        sub,
        LTB_NONE);
}

void level_blocks_for_sector(
        level_t *level,
        sector_t *sector,
        DYNLIST(block_t*) *out) {
    v2i mi = level_pos_to_block_clamped(level, sector->min);
    v2i ma = level_pos_to_block_clamped(level, sector->max);

    mi = v2i_sub(mi, level->blocks.offset);
    ma = v2i_sub(ma, level->blocks.offset);

    for (int y = mi.y; y <= ma.y; y++) {
        for (int x = mi.x; x <= ma.x; x++) {
            *dynlist_push(*out) =
                &level->blocks.arr[(y * level->blocks.size.x) + x];
        }
    }
}

static block_walls_indices_t update_blocks_buffer(
        level_t *level,
        const block_t *block,
        int *n_walls) {
    const int offset = *n_walls;

    dynlist_each(block->walls, it) {
        if (*n_walls >= MAX_WALLS) {
            WARN(
                "too many walls on block %d, max is %d",
                block->index,
                MAX_WALLS);
            break;
        }

        const wall_t *wall = *it.el;

        if (!wall->sides[0] && !wall->sides[1]) { continue; }

        block_wall_data_t *p = &g_renderer->blocks.data[*n_walls];
        *p = (block_wall_data_t) { 0 };

        p->a = wall->v0->pos;
        p->b = wall->v1->pos;

        // optimization opportunity:
        // add flags to transmit if mids are the same for both sides
        for (int i = 0; i < 2; i++) {
            p->sects[i] =
                wall->sides[i] && wall->sides[i]->sector ?
                    wall->sides[i]->sector->id : -1;
            p->sides[i] =
                wall->sides[i] ? wall->sides[i]->id : -1;

            const side_t *side = wall->sides[i];

            if (!side) { continue; }

            if (side->portal) {
                const side_segments_t segs =
                    side_get_segments(level, side);

                if (segs.middle.present) {
                    p->zbls[i] = segs.middle.zbl;
                    p->zbrs[i] = segs.middle.zbr;
                    p->ztls[i] = segs.middle.ztl;
                    p->ztrs[i] = segs.middle.ztr;
                }
            }

            if (side->sector) {
                p->floors[i] = sector_plane_vec(side->sector, PLANE_TYPE_FLOOR);
                p->ceils[i] = sector_plane_vec(side->sector, PLANE_TYPE_CEIL);
            }
        }

        (*n_walls)++;
    }

    return (block_walls_indices_t) {
        .offset = offset,
        .count = *n_walls - offset,
    };
}

void blocks_update(level_t *level) {
    // only update on version diff
    if (g_renderer->blocks.version == level->blocks.version) {
        return;
    }

    g_renderer->blocks.version = level->blocks.version;
    g_renderer->blocks.dirty = true;

    int n_walls = 0;

    const v2i offset = level->blocks.offset, size = level->blocks.size;
    for (int y = 0; y < size.y; y++) {
        for (int x = 0; x < size.x; x++) {
            const block_t *block =
                level_get_block(level, v2i_add(offset, v2i_of(x, y)));

            g_renderer->blocks.indices[(y * size.x) + x] =
                update_blocks_buffer(level, block, &n_walls);
        }
    }

    if (n_walls > MAX_WALLS) {
        WARN("too many walls! max is %d", MAX_WALLS);
        n_walls = MAX_WALLS;
    }

    g_renderer->blocks.n_walls = n_walls;
    g_renderer->blocks.n_blocks = level->blocks.size.x * level->blocks.size.y;
 }
