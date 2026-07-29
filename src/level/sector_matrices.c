#include "level/sector_matrices.h"
#include "level/level.h"
#include "level/level_types.h"
#include "level/lptr.h"
#include "level/sector.h"
#include "game.h"
#include "util/any.h"
#include "util/kvstore.h"
#include "util/time.h"
#include "util/fixlist.h"

static void matrix_prepare_update(level_t *level, sector_matrix_t *matrix) {
    matrix->new_n = round_up_to_mult(level->sectors.data.capacity, 8);

    // allocate and zero a new matrix
    const int n_bytes = matrix->new_n * BITMAP_SIZE_TO_BYTES(matrix->new_n);

    if (matrix->new_matrix) {
        mem_free(&level->arena, matrix->new_matrix);
    }

    matrix->new_matrix = mem_calloc(&level->arena, n_bytes);

    const int temp_n = max(matrix->n, matrix->new_n);
    if (!matrix->temp_matrix || temp_n != matrix->temp_n) {
        matrix->temp_n = temp_n;

        if (matrix->temp_matrix) {
            mem_free(&level->arena, matrix->temp_matrix);
        }

        matrix->temp_matrix =
            mem_calloc(&level->arena, temp_n * BITMAP_SIZE_TO_BYTES(temp_n));
    }
}

static void matrix_finalize_update(level_t *level, sector_matrix_t *matrix) {
    // done calculating, use the new matrix!
    if (matrix->matrix) {
        mem_free(&level->arena, matrix->matrix);
    }

    matrix->matrix = matrix->new_matrix;
    matrix->new_matrix = NULL;
    matrix->n = matrix->new_n;
}

static void matrix_stop_update(level_t *level, sector_matrix_t *matrix) {
    if (matrix->new_matrix) {
        mem_free(&level->arena, matrix->new_matrix);
        matrix->new_matrix = NULL;
    }
}

static bitmap_t matrix_raw_bits(u8 *matrix, int n, int index) {
    return (bitmap_t) {
        .allocator = NULL,
        .size = n,
        .bits = &matrix[index * BITMAP_SIZE_TO_BYTES(n)]
    };
}

static bitmap_t matrix_bits(
    const level_t *level,
    const sector_matrix_t *matrix,
    int i) {
    BITMAP_DECL_STATIC(empty, MAX_SECTORS);

    if (level->matrices.last_calc && matrix->new_matrix) {
        if (i >= matrix->temp_n) {
            // invisible if entirely out of bounds
            return matrix_raw_bits(matrix->temp_matrix, matrix->temp_n, 0);
        }

        // used OR'd matrix and new_matrix stored into temp_matrix
        bitmap_t
            bits_temp = matrix_raw_bits(matrix->temp_matrix, matrix->temp_n, i),
            bits_old =
                i >= matrix->n ?
                    empty
                    : matrix_raw_bits(matrix->matrix, matrix->n, i),
            bits_new =
                i >= matrix->new_n ?
                    empty
                    : matrix_raw_bits(matrix->new_matrix, matrix->new_n, i);

        bitmap_fill(&bits_temp, 0);
        bitmap_copy_n(&bits_temp, &bits_old, min(bits_temp.size, bits_old.size));
        bitmap_or(&bits_temp, &bits_new);

        return bits_temp;
    }

    // return empty bits if out of range
    if (i >= matrix->n) {
        return empty;
    }

    return matrix_raw_bits(matrix->matrix, matrix->n, i);
}

void sector_matrices_recompute(level_t *level) {
    level->matrices.dirty = true;

    // reset/stop calculation if it is currently in progress
    level->matrices.last_dirty = false;

    // stop current calculation
    for (int i = 0; i < ARRLEN(level->matrices.arr); i++) {
        matrix_stop_update(level, &level->matrices.arr[i]);
    }
}

// #define DO_DEBUG_SPLITTERS

#ifdef DO_DEBUG_SPLITTERS
#define DEBUG_SPLITTERS(...) LOG(__VA_ARGS__)
#else
#define DEBUG_SPLITTERS(...)
#endif // ifdef DO_DEBUG_SPLITTERS

// calculate a line A -> B that splits source and pass such that source is
// entirely on the left side of the line and pass is entirely on the right
static void calculate_splitter(
        line2f_t source,
        line2f_t pass,
        v2 *a,
        v2 *b) {
    // find potential splitter between source and pass
    // experimentally, ordering source a -> pass b and source b -> pass a we can
    // (and are more likely) to find a splitter earlier in the process. can give
    // up to 25% time savings some spots(!)
    const line2f_t ls[4] = {
        { .a = source.a, .b = pass.b, },
        { .a = source.b, .b = pass.a, },
        { .a = source.a, .b = pass.a, },
        { .a = source.b, .b = pass.b, },
    };

#ifdef DO_DEBUG_SPLITTERS
    bool found = false;
#endif

    for (int i = 0; i < 4; i++) {
        int gss, gsp;

        // does this line put source and pass on different sides?
        gss = sign(point_side(source.a, ls[i].a, ls[i].b));
        if (!gss) { gss = sign(point_side(source.b, ls[i].a, ls[i].b)); }

        gsp = sign(point_side(pass.a, ls[i].a, ls[i].b));
        if (!gsp) { gsp = sign(point_side(pass.b, ls[i].a, ls[i].b)); }


        if (gss != gsp) {
            *a = ls[i].a;
            *b = ls[i].b;

            // swap such that "pass" is negative (right side)
            if (gsp > 0) {
                swap(*a, *b);
            }

#ifdef DO_DEBUG_SPLITTERS
            found = true;
#endif
            break;
        }
    }

#ifdef DO_DEBUG_SPLITTERS
    if (!found) {
        DEBUG_SPLITTERS(
            "could not split:\n"
            "  %" PRIv2 " -> %" PRIv2 "\n"
            "  %" PRIv2 " -> %" PRIv2,
            FMTv2(source.a),
            FMTv2(source.a),
            FMTv2(pass.a),
            FMTv2(pass.b));
    }
#endif // ifdef DO_DEBUG_SPLITTERS
}

#define DEBUG_PVS_SECTOR 79
// #define DO_DEBUG_PVS

#ifdef DO_DEBUG_PVS
#define DEBUG_PVS(...) if (sector->id == DEBUG_PVS_SECTOR) { LOG(__VA_ARGS__); }
#else
#define DEBUG_PVS(...)
#endif // ifdef DO_DEBUG_PVS

typedef struct pvs_queue_entry {
    int depth;

    // sector to test outgoing portals of
    sector_t *sect;

    // origin line (pointing TOWARDS origin sector)
    line2f_t source;

    // line through which "sub" was entered
    // (pointing TOWARDS !!! origin sector)
    line2f_t through;

#ifdef DO_DEBUG_PVS
    sect_line_t *source_line, *through_line;
#endif // ifdef DO_DEBUG_PVS

    // list of sector ids visited already for this queue entry
    bitmap_t *visited;
} pvs_queue_entry_t;

// optimization opportunity: we can vastly simplify PVSing for some sectors if
// we assume that if a sector is visible, then all of the sectors that are holes
// to the sector (and holes to those sectors, etc.) are also visible - so we
// don't need to check their outgoing sides for visibility and can trivially
// mark them visible if the outer/containing sector is visible.
//
// maybe we can even go so far as to say that this should cover sectors which
// aren't necessarily holes but are entirely contained within another sector?
// fx. a sector that has all walls as a hole except one, like an indent into
// a "containing" sector.
static void compute_pvs_update_for_sector(level_t *level, sector_t *sector) {
    if (llist_empty(&sector->subs)) { return; }

    const nstime_t start = time_ns();

    bitmap_t bits =
        matrix_raw_bits(
            level->matrices.pvs.new_matrix,
            level->matrices.pvs.new_n,
            sector->id);

    // fast pvs -> pvs == reachable
    if (g->visopt & VISOPT_FAST_PVS) { return; }

    // don't zero-init, other sectors might have set our bits already

    // sectors are always visible from themselves
    bitmap_set(&bits, sector->id);

    const int n_sectors = level->sectors.data.max_index + 1;

#define line2_from_sect_line(_s) \
    ((line2f_t) { .a = (_s)->a->pos, .b = (_s)->b->pos })

    // check if sector is visible via source through "through"
    static FIXLIST(pvs_queue_entry_t, 4096) queue;
    queue.n = 0;

    // add initial entries
    llist_each(sector_sides, &sector->sides, it) {
        if (!it.el->portal
            || !it.el->portal->sector
            || it.el->is_disconnect
            || !it.el->sect_line) { continue; }

        if (v2_eqv_eps(it.el->sect_line->a->pos, it.el->sect_line->b->pos)) {
            // skip, source is point
            continue;
        }

        pvs_queue_entry_t *entry = fixlist_push(queue);
        entry->depth = 1;
        entry->sect = it.el->portal->sector;
        entry->source = line2_from_sect_line(it.el->sect_line);
        entry->through = line2_from_sect_line(it.el->sect_line);
#ifdef DO_DEBUG_PVS
        entry->source_line = it.el->sect_line;
        entry->through_line = it.el->sect_line;
#endif // ifdef DO_DEBUG_PVS
        entry->visited = bitmap_create_zero(&g->frame_arena, n_sectors);
        bitmap_set(entry->visited, sector->id);
        bitmap_set(entry->visited, it.el->portal->sector->id);

        DEBUG_PVS("adding line for side %d to initial entries", it.el->id);
    }

    while (queue.n != 0) {
        // pop from end of queue
        pvs_queue_entry_t entry = fixlist_pop(queue);

        // this sector is visible
        bitmap_set(&bits, entry.sect->id);

        // lazily calculate splitter as needed
        bool have_splitter = false;
        v2 a = v2_of(0), b = v2_of(0);

        // check neighbors for visibility
        dynlist_each(entry.sect->pvs_outgoing, it) {
            const side_t *side = *it.el;

            // skip if already visited
            if (bitmap_get(entry.visited, side->portal->sector->id)) {
                continue;
            }

            line2f_t line = line2_from_sect_line(side->sect_line);

            // check if line is entirely behind our source (side < 0/right)
            // remember, source lines point *TOWARDS* the origin sector!
            if (point_side(line.a, entry.source.a, entry.source.b) <= 0.0f
                && point_side(line.b, entry.source.a, entry.source.b) <= 0.0f) {
                DEBUG_PVS(
                    "skipping side %d on %d -> %d, both points are behind source",
                    it.el->id,
                    entry.source_line->side->id,
                    entry.through_line->side->id);
                continue;
            }

            // colinear segments are by definition not visible from each other
            if (segments_are_colinear(
                    entry.source.a,
                    entry.source.b,
                    line.a,
                    line.b,
                    0.0001f)) {
                DEBUG_PVS(
                    "skipping side %d on %d -> %d, source and side are colinear",
                    it.el->id,
                    entry.source_line->side->id,
                    entry.through_line->side->id);
                continue;
            } else if (segments_are_colinear(
                    entry.through.a,
                    entry.through.b,
                    line.a,
                    line.b,
                    0.0001f)) {
                DEBUG_PVS(
                    "skipping side %d on %d -> %d, through and side are colinear",
                    it.el->id,
                    entry.source_line->side->id,
                    entry.through_line->side->id);
                continue;
            }

            // if early depth (< 2) always enqueue
            if (entry.depth > 2) {
                if (!have_splitter) {
                    calculate_splitter(entry.source, entry.through, &a, &b);
                    have_splitter = true;
                }

                // check if the outgoing line to this neighbor is visible via
                // line is visible iff either of its points lie to the right
                // of the splitter a -> b (same side as pass)
                // additionally, clip the through if there are intersections
                const line2f_t l = line;
                v2 hit;
                f32 t;
                int behind = 0;
                if (point_side(l.a, a, b) > 0) {
                    behind++;
                    if (intersect_segs(l.a, l.b, a, b, &hit, &t, NULL)) {
                        line.a = hit;
                    }
                }

                if (point_side(l.b, a, b) > 0) {
                    behind++;
                    if (behind == 1
                        && intersect_segs(l.a, l.b, a, b, &hit, &t, NULL)) {
                        line.b = hit;
                    }
                }

                if (behind == 2) {
                    DEBUG_PVS(
                        "skipping side %d on %d -> %d, both points are behind splitter",
                        it.el->id,
                        entry.source_line->side->id,
                        entry.through_line->side->id);
                    continue;
                }
            }

            if (v2_eqv_eps(line.a, line.b)) {
                DEBUG_PVS(
                    "skipping %d -> %d, through is point",
                    entry.source_line->side->id,
                    entry.through_line->side->id);
                continue;
            }

            // if so, enqueue it
            pvs_queue_entry_t *new_ent = fixlist_push(queue);
            *new_ent = entry;
            new_ent->depth = entry.depth + 1;
            new_ent->sect = side->portal->sector;
            new_ent->source = entry.source;
            new_ent->through = line;
#ifdef DO_DEBUG_PVS
            new_ent->source_line = entry.source_line;
            new_ent->through_line = it.el->sect_line;
#endif // ifdef DO_DEBUG_PVS
            new_ent->visited = bitmap_create(&g->frame_arena, n_sectors);
            bitmap_copy(new_ent->visited, entry.visited);
            bitmap_set(new_ent->visited, side->portal->sector->id);

            DEBUG_PVS(
                "enqueuing side %d on %d -> %d",
                it.el->id,
                entry.source_line->side->id,
                entry.through_line->side->id);
        }
    }

#undef line2_from_sect_line

    // set corresponding bits in other sectors
    int i = -1;
    while ((i = bitmap_find(&bits, i + 1, true)) != -1) {
        bitmap_t other =
            matrix_raw_bits(
                level->matrices.pvs.matrix,
                level->matrices.pvs.n,
                i);
        bitmap_set(&other, sector->id);
    }

    if (sector->id == DEBUG_PVS_SECTOR) {
        LOG("sector %d took %.3f ms", sector->id, ns_to_ms(time_ns() - start));
    }
}

// traverse neighbors sector and stop traversal when a non-visible sector it hit
// our final PVS is (pvs UNION pvs-reachable set)
static void finalize_pvs_near_reachable_for_sector(
        level_t *level,
        sector_t *sector) {
    bitmap_t
        pvs =
            matrix_raw_bits(
                level->matrices.pvs.new_matrix,
                level->matrices.pvs.new_n,
                sector->id),
        reachable =
            matrix_raw_bits(
                level->matrices.reachable.new_matrix,
                level->matrices.reachable.new_n,
                sector->id),
        near =
            matrix_raw_bits(
                level->matrices.near.new_matrix,
                level->matrices.near.new_n,
                sector->id);

    BITMAP_DECL_STATIC(visited, MAX_SECTORS);
    bitmap_fill_n(&visited, 0, level->sectors.data.capacity);
    bitmap_set(&visited, sector->id);

    DYNLIST(sector_t*) queue = dynlist_create(sector_t*, &g->frame_arena);

    if (!(g->visopt & VISOPT_FAST_PVS)) {
        // TODO: this can breaks one-sided walls - fix, if we end up using them
        // enforce PVS agreement: for this sector to see another, its PVS must
        // say that it can see this sector
        for (int i = 0; i < level->matrices.pvs.new_n; i++) {
            if (!bitmap_get(&pvs, i)) { continue; }

            bitmap_t other_pvs =
                matrix_raw_bits(
                    level->matrices.pvs.new_matrix,
                    level->matrices.pvs.new_n,
                    i);

            if (!bitmap_get(&other_pvs, sector->id)) {
                bitmap_clr(&pvs, i);
            }
        }

        // enforce that sectors in the PVS are contiguous: try to reach each
        // sector in the computed PVS only via sector neighbors, AND this with
        // the computed PVS
        *dynlist_push(queue) = sector;

        while (dynlist_size(queue) != 0) {
            sector_t *sector = dynlist_pop(queue);

            dynlist_each(sector->neighbors, it) {
                sector_t *neighbor = *it.el;

                // skip if already seen or rejected by PVS
                if (bitmap_get(&visited, neighbor->id)
                    || !bitmap_get(&pvs, neighbor->id)) {
                    continue;
                }

                bitmap_set(&visited, neighbor->id);
                *dynlist_push(queue) = neighbor;
            }
        }

        // pvs &= visited (PVS via reachable)
        bitmap_and(&pvs, &visited);
    }

    // compute the reachable AND near sets for this sector
    bitmap_fill_n(&visited, 0, level->sectors.data.capacity);

    bitmap_fill(&near, 0);
    bitmap_fill(&reachable, 0);

    *dynlist_push(queue) = sector;

    while (dynlist_size(queue) != 0) {
        sector_t *other = dynlist_pop(queue);

        // mark sector as reachable
        bitmap_set(&reachable, other->id);

        // check if sector is in "near" set by finding nearest point between
        // two sectors
        v2 closest[2];
        sector_closest_points(level, sector, other, closest);

        if (v2_distance(closest[0], closest[1]) < SECTOR_NEAR_THRESHOLD) {
            // other is in "near" set of sector
            bitmap_set(&near, other->id);
        }

        // enqueue unvisited neighbors
        dynlist_each(other->neighbors, it) {
            sector_t *neighbor = *it.el;

            // skip if already visited
            if (bitmap_get(&visited, neighbor->id)) {
                continue;
            }

            bitmap_set(&visited, neighbor->id);
            *dynlist_push(queue) = neighbor;
        }
    }

    // fast pvs? pvs == reachable
    if (g->visopt & VISOPT_FAST_PVS) {
        bitmap_copy(&pvs, &reachable);
    }
}

// computes an exhaustive visible set for the specified sector, a PVS which
// includes everything potentially visible via recursive portals
static void compute_evs_for_sector(
        level_t *level,
        sector_t *sector) {
    const int n = round_up_to_mult(level->sectors.data.capacity, 8);
    BITMAP_DECL_STATIC(visited, MAX_SECTORS);
    bitmap_fill_n(&visited, false, n);

    bitmap_t evs =
        matrix_raw_bits(
            level->matrices.evs.new_matrix,
            level->matrices.evs.new_n,
            sector->id);

    DYNLIST(sector_t*) queue = dynlist_create(sector_t*, &g->frame_arena);
    *dynlist_push(queue) = sector;

    while (dynlist_size(queue) != 0) {
        sector_t *s = dynlist_pop(queue);

        if (bitmap_get(&visited, s->id)) { continue; }

        // sector has been visited
        bitmap_set(&visited, s->id);

        const bitmap_t pvs = sector_matrix_bits(level, &level->matrices.pvs, s);

        // evs |= pvs
        bitmap_or(&evs, &pvs);

        // get all visible sector portals, add to queue if not visited
        int i = -1;
        while (
            (i = bitmap_find(&pvs, i + 1, true)) != -1
                && i < n) {
            sector_t *other;
            const genlist_handle_t idx =
                genlist_handle_of_index(&level->sectors, i);

            if (!genlist_present(&level->sectors, idx)) {
                continue;
            } else {
                other = genlist_try_ptr(sector_t, &level->sectors, idx);
            }

            if (!other) { continue; }

            llist_each(sector_sides, &other->sides, it) {
                if (!it.el->is_disconnect) { continue; }
                ASSERT_DEBUG(it.el->portal);

                sector_t *portal_sect = it.el->portal->sector;
                if (!portal_sect) { continue; }

                if (bitmap_get(&visited, portal_sect->id)) { continue; }

                // need to visit portal sector
                *dynlist_push(queue) = portal_sect;
            }
        }

        // all PVS sectors have been visibted
        bitmap_or(&visited, &pvs);
    }
}

static void finalize_evs_for_sector(
    level_t *level,
    sector_t *sector) {
    bitmap_t evs =
        matrix_raw_bits(
            level->matrices.evs.new_matrix,
            level->matrices.evs.new_n,
            sector->id);

    // enforce EVS agreement
    for (int i = 0; i < evs.size; i++) {
        if (!bitmap_get(&evs, i)) { continue; }

        bitmap_t other_evs =
            matrix_raw_bits(
                level->matrices.evs.new_matrix,
                level->matrices.evs.new_n,
                i);

        if (!bitmap_get(&other_evs, sector->id)) {
            bitmap_clr(&evs, i);
        }
    }
}

void sector_matrices_update(level_t *level) {
    if (!level->matrices.pvs_queue) {
        dynlist_init(level->matrices.pvs_queue, &level->arena);
        dynlist_init(level->matrices.pvs_finalize_queue, &level->arena);
        dynlist_init(level->matrices.evs_queue, &level->arena);
        dynlist_init(level->matrices.evs_finalize_queue, &level->arena);
    }

    // create and zero-init matrices which do not already exist
    for (uint i = 0; i < ARRLEN(level->matrices.arr); i++) {
        if (level->matrices.arr[i].matrix) { continue; }

        // force recompute
        sector_matrices_recompute(level);

        const int n = round_up_to_mult(level->sectors.data.capacity, 8);
        level->matrices.arr[i] = (sector_matrix_t) {
            .n = n,
            .matrix = mem_calloc(&level->arena, BITMAP_SIZE_TO_BYTES(n) * n),
        };
    }

    if (level->matrices.dirty && !level->matrices.last_dirty) {
        // start a new visibility calculation
        for (uint i = 0; i < ARRLEN(level->matrices.arr); i++) {
            matrix_prepare_update(level, &level->matrices.arr[i]);
        }

        dynlist_resize(level->matrices.pvs_queue, 0);
        dynlist_resize(level->matrices.pvs_finalize_queue, 0);
        dynlist_resize(level->matrices.evs_queue, 0);
        dynlist_resize(level->matrices.evs_finalize_queue, 0);

        // enqueue all sectors for PVSing
        level_each(sector_t, &level->sectors, it) {
            *dynlist_push(level->matrices.pvs_queue) = lptr_from(it.el);
        }

        level->matrices.progress = 0;
    }

    struct {
        DYNLIST(lptr_t) *queue;
        void (*func)(level_t*, sector_t*);
    } queue_func_pairs[4] = {
        {
            &level->matrices.pvs_queue,
            compute_pvs_update_for_sector,
        },
        {
            &level->matrices.pvs_finalize_queue,
            finalize_pvs_near_reachable_for_sector,
        },
        {
            &level->matrices.evs_queue,
            compute_evs_for_sector,
        },
        {
            &level->matrices.evs_finalize_queue,
            finalize_evs_for_sector,
        },
    };

    // cap total ms used per frame on updating sector matrices
    i64 total_ns = 0;

    // set to true if anything is updated this frame
    bool did_calc = false;

    for (int i = 0; i < ARRLEN(queue_func_pairs); i++) {
        DYNLIST(lptr_t) *queue = queue_func_pairs[i].queue;

        DYNLIST(lptr_t) *next_queue =
            i == (ARRLEN(queue_func_pairs) - 1) ?
                NULL
                : queue_func_pairs[i + 1].queue;

        void (*func)(level_t*, sector_t*) = queue_func_pairs[i].func;

        while (dynlist_size(*queue) != 0) {
            did_calc = true;

            if (ns_to_ms(total_ns) > MAX_SECTOR_MATRIX_MS_PER_FRAME) {
                break;
            }

            const i64 start = time_ns();

            sector_t *s = lptr_sector(level, dynlist_pop(*queue));

            if (s) {
                func(level, s);

                if (next_queue) {
                    *dynlist_push(*next_queue) = lptr_from(s);
                }

                level->matrices.progress++;
            }

            total_ns += time_ns() - start;
        }
    }

    if (!did_calc && level->matrices.last_calc) {
        for (uint i = 0; i < ARRLEN(level->matrices.arr); i++) {
            matrix_finalize_update(level, &level->matrices.arr[i]);
        }
    }

    level->matrices.last_calc = did_calc;
    level->matrices.last_dirty = level->matrices.dirty;
    level->matrices.dirty = false;
}

bitmap_t sector_matrix_bits(
        const level_t *level,
        const sector_matrix_t *mtx,
        const sector_t *s) {
    return matrix_bits(level, mtx, s->id);
}

int sector_matrix_get(
        const level_t *level,
        const sector_matrix_t *mtx,
        const sector_t *s,
        DYNLIST(sector_t*) *out,
        int flags) {
    BITMAP_DECL_STATIC(bits, MAX_SECTORS);
    sector_matrix_get_as_bitmap(level, mtx, s, &bits, flags);

    // convert to DYNLIST
    int n = 0;
    int i = -1;
    while (
        (i = bitmap_find(&bits, i + 1, true)) != -1
            && i < level->sectors.data.capacity) {
        sector_t *ptr =
            genlist_try_ptr_from_index(sector_t, &level->sectors, i);

        if (ptr) {
            *dynlist_push(*out) = ptr;
            n++;
        } else {
            WARN("trying to get deleted sector %d", i);
        }
    }

    return n;
}

void sector_matrix_get_as_bitmap(
        const level_t *level,
        const sector_matrix_t *mtx,
        const sector_t *s,
        bitmap_t *out,
        int flags) {
    const int n = level->sectors.data.max_index + 1;
    ASSERT(out->size >= n);

    if (!s || s->id >= mtx->n) {
        return;
    }

    bitmap_fill_n(out, 0, mtx->n);

    // fill with s
    bitmap_t s_bits = sector_matrix_bits(level, mtx, s);
    bitmap_or(out, &s_bits);

    if (flags & SECTOR_MATRIX_WITH_PORTALS) {
        // add any sectors visible from portals
        llist_each(sector_sides, &s->sides, it) {
            if (it.el->is_disconnect && it.el->portal->sector) {
                bitmap_t p_bits =
                    sector_matrix_bits(level, mtx, it.el->portal->sector);
                bitmap_or(out, &p_bits);
            }
        }
    }
}

bool sector_matrix_get_for_sector(
    const level_t *level,
    const sector_matrix_t *mtx,
    const sector_t *a,
    const sector_t *b) {
    const bitmap_t bits = sector_matrix_bits(level, mtx, a);
    return bitmap_get(&bits, b->id);
}

void sector_matrix_to_any(
        const level_t *level,
        any_t *dst,
        const sector_matrix_t *mtx) {
    kvstore_t *kvs = any_set_kvstore(dst, NULL);
    kvstore_set_int(kvs, "n", mtx->n);

    const int n_bytes = mtx->n * BITMAP_SIZE_TO_BYTES(mtx->n);
    kvstore_set_bytes(
        kvs,
        "data",
        &(range_t) { .ptr = mtx->matrix, .size = n_bytes });
}

bool sector_matrix_from_any(
        level_t *level,
        sector_matrix_t *mtx,
        const any_t *src,
        const char **errmsg) {
    const kvstore_t *kvs = any_get_kvstore(src);
    if (!kvs) {
        *errmsg = "any is not kvstore";
        return false;
    } else if (!kvstore_has(kvs, "n")) {
        *errmsg = "missing n";
        return false;
    } else if (!kvstore_has(kvs, "data")) {
        *errmsg = "missing data";
        return false;
    }

    // matrix cannot have been allocated already
    ASSERT(!mtx->matrix);
    ASSERT(!mtx->temp_matrix);
    ASSERT(!mtx->new_matrix);

    int n;
    if (!kvstore_get_as_int(kvs, "n", &n)) {
        *errmsg = "n is not int";
        return false;
    }

    // get onto level arena
    const range_t data = kvstore_get_as_bytes(kvs, "data", &level->arena);
    if (!data.ptr) {
        *errmsg = "data is not bytes";
        return false;
    }

    // TODO TODO TODO
    // BEFORE SHIPPING
    // NEED TO VALIDATE THIS !!
    *mtx = (sector_matrix_t) {
        .n = n,
        .matrix = data.ptr,
    };

    return true;
}
