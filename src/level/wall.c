#include "level/wall.h"
#include "level/block.h"
#include "level/level.h"
#include "level/sector.h"
#include "level/side.h"
#include "level/vertex.h"

wall_t *wall_new(
    level_t *level,
    vertex_t *v0,
    vertex_t *v1) {
    wall_t *w = level_try_alloc(level, &level->walls);

    // either both NULL or both non-NULL
    ASSERT((!!v0) == (!!v1));

    if (v0 && v1) {
        wall_set_vertex(level, w, 0, v0);
        wall_set_vertex(level, w, 1, v1);
        w->sides[0] = NULL;
        w->sides[1] = NULL;

        vertex_recalculate(level, v0);
        vertex_recalculate(level, v1);
        wall_recalculate(level, w);
    }

    return w;
}

void wall_delete(level_t *level, wall_t *w) {
    for (int i = 0; i < 2; i++) {
        if (w->sides[i]) {
            side_delete(level, w->sides[i]);
        }
    }

    for (int i = 0; i < 2; i++) {
        if (!w->vertices[i]) { continue; }

        bool found = false;
        dynlist_each(w->vertices[i]->walls, it) {
            if (*it.el == w) {
                dynlist_remove_it(w->vertices[i]->walls, it);
                found = true;
                break;
            }
        }

        vertex_recalculate(level, w->vertices[i]);
        ASSERT(found);
    }

    level_blocks_remove_wall(level, w);
    level_free(level, &level->walls, w);
}

void wall_recalculate(level_t *level, wall_t *wall) {
    if (wall->lflags.do_not_recalc) { return; }
    wall->lflags.recalc_enqueued = false;

    wall->version++;
    level->version++;

    if (!wall->v0 || !wall->v1) {
        WARN("wall %d missing vertex", wall->id);
        return;
    }

    const v2
        a = wall->v0->pos,
        b = wall->v1->pos;

    // compute world-space normal of front side (right of a -> b)
    // use the fact that for a line segment (x0, y0) -> (x1, y1), with
    // dx = x1 - x0, dy = y1 - y0, then its 2D normals are (-dy, dx) and
    // (dy, -dx).
    wall->normal =
        v2_normalize(
            v2_of(b.y - a.y, -(b.x - a.x)));

    if (isnan(wall->normal.x) || isnan(wall->normal.y)) {
        wall->normal = v2_of(1, 0);
    }

    wall->len = v2_distance(a, b);

    level_update_wall_blocks(
        level,
        wall,
        &wall->last_pos[0],
        &wall->last_pos[1]);

    wall->last_pos[0] = wall->v0->pos;
    wall->last_pos[1] = wall->v1->pos;

    // recalculate sectors connected to this wall
    for (int i = 0; i < 2; i++) {
        if (wall->sides[i] && wall->sides[i]->sector) {
            sector_recalculate(level, wall->sides[i]->sector);
        }
    }
}

bool wall_in_sector(
    const level_t *level,
    const wall_t *wall,
    const sector_t *sector) {
    return (wall->sides[0] && wall->sides[0]->sector == sector)
        || (wall->sides[1] && wall->sides[1]->sector == sector);
}

sector_t *walls_shared_sector(
    const level_t *level,
    const wall_t *w0,
    const wall_t *w1) {
    const wall_t *ws[2] = { w0, w1 };
    const sector_t *ss[2][2];

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            ss[i][j] = ws[i]->sides[j] ? ws[i]->sides[j]->sector : NULL;
        }
    }


    // check if sectors of each first wall's sides match with either of those
    // of the second wall
    for (int j = 0; j < 2; j++) {
        if (!ss[0][j]) {
            continue;
        } else if (ss[1][j] == ss[0][j]) {
            return (sector_t*) ss[0][j];
        } else if (ss[1][1 - j] == ss[0][j]) {
            return (sector_t*) ss[0][j];
        }
    }

    return NULL;
}

void wall_set_vertex(
    level_t *level,
    wall_t *wall,
    int i,
    vertex_t *v) {
    ASSERT(v);
    ASSERT(i >= 0 && i < 2);

    // remove from old vertex
    if (wall->vertices[i]) {
        bool found = false;
        dynlist_each(wall->vertices[i]->walls, it) {
            if (*it.el == wall) {
                dynlist_remove_it(wall->vertices[i]->walls, it);
                found = true;
                break;
            }
        }
        ASSERT(found);

        vertex_recalculate(level, wall->vertices[i]);
    }

    wall->vertices[i] = v;

    // add to new vertex, recalculate wall data
    ASSERT(
        v != wall->vertices[1 - i],
        "wall %d has same vertex %d twice", wall->id, v->id);

    dynlist_each(v->walls, it) {
        ASSERT(*it.el != wall);
    }

    *dynlist_push(v->walls) = wall;

    vertex_recalculate(level, v);
    wall_recalculate(level, wall);
}

void wall_replace_vertex(
    level_t *level,
    wall_t *wall,
    vertex_t *old_vert,
    vertex_t *new_vert) {
    ASSERT(wall->v0 == old_vert || wall->v1 == old_vert);
    wall_set_vertex(level, wall, old_vert == wall->v0 ? 0 : 1, new_vert);
}

void wall_set_side(
        level_t *level,
        wall_t *wall,
        int i,
        side_t *s) {

    if (wall->sides[i]) {
        wall->sides[i]->wall = NULL;
    }

    wall->sides[i] = s;

    if (s) {
        s->wall = wall;
        s->is_backside = i == 1;
    }

    wall_recalculate(level, wall);
}

v2 wall_midpoint(wall_t *wall) {
    return v2_lerp(wall->v0->pos, wall->v1->pos, 0.5f);
}

wall_t *wall_split(level_t *level, wall_t *w0, v2 p) {
    // snap point p to wall
    const v2 q = point_project_segment(p, w0->v0->pos, w0->v1->pos);

    /* const f32 start_len = w0->len; */
    /* const f32 u_split = v2_norm(v2_sub(q, w0->v0->pos)) / w0->len; */

    // need three vertices on wall, v0 (wall v0), v1 (q), and v2 (wall v1)
    vertex_t
        // v0 = wall->v0,
        *v1 = vertex_new(level, q),
        *v2 = w0->v1;

    // adjust current wall to span v0 -> v1
    wall_set_vertex(level, w0, 1, v1);

    // create new wall like w0
    wall_t *w1 = wall_new(level, v1, v2);

    w0->lflags.do_not_recalc = true;
    w1->lflags.do_not_recalc = true;

    for (int i = 0; i < 2; i++) {
        if (!w0->sides[i]) {
            continue;
        }

        side_t *new_side = side_new(level, w0->sides[i]);
        wall_set_side(level, w1, i, new_side);

        if (w0->sides[i]->sector) {
            sector_add_side(
                level,
                w0->sides[i]->sector,
                new_side);
        }

        // TODO
        /* // check if decals need to be moved */
        /* llist_each(node, &w0->sides[i]->decals, it) { */
        /*     const f32 u = it.el->ox / start_len; */
        /*     if (u < usplit) { */
        /*         continue; */
        /*     } */

        /*     // TODO: recompute ox */
        /*     // move to other side */
        /*     level_decal_set_side(level, it.el, new_side); */
        /* } */
    }

    // recreate portals as they are on original wall
    for (int i = 0; i < 2; i++) {
        if (w0->sides[i]
            && w0->sides[i]->portal
            && w0->sides[i]->portal == w0->sides[1 - i]) {
            w1->sides[i]->portal = w1->sides[1 - i];
        }
    }

    w0->lflags.do_not_recalc = false;
    w1->lflags.do_not_recalc = false;

    ASSERT(w0->v1 == w1->v0);
    wall_recalculate(level, w0);
    wall_recalculate(level, w1);

    return w1;
}
