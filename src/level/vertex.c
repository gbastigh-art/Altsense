#include "level/vertex.h"
#include "level/level.h"
#include "level/sector.h"
#include "level/wall.h"
#include "game.h"

vertex_t *vertex_new(level_t *level, v2 pos) {
    vertex_t *v = level_try_alloc(level, &level->vertices);
    dynlist_init(v->walls, &level->arena);
    v->pos = pos;
    vertex_recalculate(level, v);
    return v;
}

void vertex_delete(level_t *level, vertex_t *v) {
    // prevent modification under iteration
    DYNLIST(wall_t*) walls = dynlist_create(wall_t*, &g->frame_arena);
    dynlist_copy_from(walls, v->walls);

    dynlist_each(walls, it) {
        ASSERT(v == (*it.el)->v0 || v == (*it.el)->v1);
        wall_delete(level, *it.el);
    }

    dynlist_destroy(v->walls);
    level_free(level, &level->vertices, v);
}

void vertex_recalculate(level_t *level, vertex_t *vertex) {
    if (vertex->lflags.do_not_recalc) { return; }
    vertex->lflags.recalc_enqueued = false;

    level->version++;
    vertex->version++;

    vertex->pos = v2_maxv(vertex->pos, v2_of(0));

    // recalculate all walls
    dynlist_each(vertex->walls, it) {
        wall_recalculate(level, *it.el);
    }
}

// set vertex position
void vertex_set(level_t *level, vertex_t *vertex, v2 pos) {
    vertex->pos = v2_maxv(pos, v2_of(0));
    vertex_recalculate(level, vertex);
}

int vertex_walls(
    level_t *level, vertex_t *v, wall_t **walls, int n) {
    int i = 0;
    dynlist_each(v->walls, it) {
        if (i == n) { break; }
        walls[i++] = *it.el;
    }
    return i;
}

int vertex_sides(
    level_t *level, vertex_t *v, side_t **sides, int n) {
    int i = 0, count = 0;
    dynlist_each(v->walls, it) {
        wall_t *w = *it.el;

        for (int i = 0; count < n && i < 2; i++) {
            if (w->sides[i]) { continue; }

            if (count >= n) {
                goto done;
            }

            sides[count++] = w->sides[i];
        }
    }

done:
    return i;
}

vertex_t *vertex_other(
    level_t *level, vertex_t *v, wall_t *wall) {
    if (v == wall->v0) {
        return wall->v1;
    } else if (v == wall->v1) {
        return wall->v0;
    }

    WARN("vertex_other returnining NULL");
    return NULL;
}

wall_t *vertices_shared_wall(
    const level_t *level,
    const vertex_t *v0,
    const vertex_t *v1) {
    dynlist_each(v0->walls, it) {
        wall_t *wall = *it.el;

        if ((v0 == wall->v0 && v1 == wall->v1)
            || (v0 == wall->v1 && v1 == wall->v0)) {
            return wall;
        }
    }

    return NULL;
}

side_t *vertices_shared_side(
    const level_t *level,
    const vertex_t *v0,
    const vertex_t *v1,
    const sector_t *sect) {
    const wall_t *wall = vertices_shared_wall(level, v0, v1);
    if (!wall) { return NULL; }

    side_t *side = NULL;
    for (int i = 0; i < 2; i++) {
        if (wall->sides[i]) {
            side = wall->sides[i];

            if (side->sector && side->sector == sect) {
                return side;
            }
        }
    }

    return side;
}

sector_t *vertices_shared_sector(
    const level_t *level,
    const vertex_t *v0,
    const vertex_t *v1) {
    dynlist_each(v0->walls, it) {
        const wall_t *wall = *it.el;

        const sector_t
            *c0 = wall->sides[0] ? wall->sides[0]->sector : NULL,
            *c1 = wall->sides[1] ? wall->sides[1]->sector : NULL;

        if (c0 && sector_contains_vertex(level, c0, v1)) {
            return (sector_t*) c0;
        } else if (c1 && sector_contains_vertex(level, c1, v1)) {
            return (sector_t*) c1;
        }
    }

    return NULL;
}
