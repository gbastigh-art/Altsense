#pragma once

#include "util/math.h"
#include "level/level_types.h"
#include "defs.h"
#include "config.h"

// TODO: better names
enum {
    LTB_NONE        = 0 << 0,

    // TODO: doc?
    LTB_BORDER_BOTH = 1 << 1,
};

typedef struct {
    v2i pos;
    block_t *el;
    struct { v2i bmin, bmax; } _m;
} level_for_blocks_in_area_it_t;

// NOTE: min/max must be clamped otherwise can crash
#define level_for_blocks_in_area(l_, bmin_, bmax_, it_)                             \
    level_for_blocks_in_area_it_t it_ = { ._m.bmin = (bmin_), ._m.bmax = (bmax_) }; \
    for (it_.pos.y = it_._m.bmin.y; it_.pos.y <= it_._m.bmax.y; it_.pos.y++)        \
        for (it_.pos.x = it_._m.bmin.x,                                             \
                it_.el = level_get_block_unsafe((l_), it_.pos);                     \
             it_.pos.x <= it_._m.bmax.x;                                            \
             it_.pos.x++, it_.el = level_get_block_unsafe((l_), it_.pos))           \

// level pos -> block pos
M_INLINE v2i level_pos_to_block(v2 point) {
    return v2i_of(max(point.x / BLOCK_SIZE, 0), max(point.y / BLOCK_SIZE, 0));
}

// clamp block pos
M_INLINE v2i level_clamp_block(const level_t *level, v2i block) {
    return
        v2i_clampv(
            block,
            level->blocks.offset,
            v2i_add(
                level->blocks.offset,
                v2i_sub(level->blocks.size, v2i_of(1))));
}

// get block position from block
M_INLINE v2i level_block_to_blockpos(const level_t *level, block_t *block) {
    const int i = (int) (block - level->blocks.arr);
    return v2i_of(i % level->blocks.size.x, i / level->blocks.size.x);
}

M_INLINE v2i level_pos_to_block_clamped(
        const level_t *level,
        v2 point) {
    return level_clamp_block(level, level_pos_to_block(point));
}

// get block at blockpos, NULL if out of bounds/not found
M_INLINE block_t *level_get_block(const level_t *l, v2i b_pos) {
    if (M_UNLIKELY(!l->blocks.arr)) {
        return NULL;
    }

    if (M_UNLIKELY(
            b_pos.x < l->blocks.offset.x
            || b_pos.y < l->blocks.offset.y
            || b_pos.x >= (l->blocks.offset.x + l->blocks.size.x)
            || b_pos.y >= (l->blocks.offset.y + l->blocks.size.y))) {
        return NULL;
    }

    return &l->blocks.arr[
        (b_pos.y - l->blocks.offset.y) * l->blocks.size.x
            + (b_pos.x - l->blocks.offset.x)];
}

// get block at blockpos, undefined behavior if out of bounds
M_INLINE block_t *level_get_block_unsafe(const level_t *l, v2i b_pos) {
    return &l->blocks.arr[
        (b_pos.y - l->blocks.offset.y) * l->blocks.size.x
            + (b_pos.x - l->blocks.offset.x)];
}

// remove a sector from block data
void level_blocks_remove_sector(level_t *level, sector_t *sector);

// remove a wall from block data
void level_blocks_remove_wall(level_t *level, wall_t *wall);

// update blocks for an entity after movement
void level_blocks_update_entity(level_t *level, entity_t *ent);

// remove an entity from block data
void level_blocks_remove_entity(level_t *level, entity_t *entity);

// update blocks
void blocks_update(level_t *level);

// recalculate/reset all blocks
void blocks_reset(level_t *level);

typedef bool (*traverse_blocks_f)(level_t*, block_t*, v2i, void*);

bool level_traverse_blocks_bump_version(
    level_t *level,
    block_t *block,
    v2i,
    void*);

void level_traverse_blocks(
    level_t *level,
    v2 from,
    v2 to,
    traverse_blocks_f callback,
    void *userdata,
    int flags);

void level_traverse_block_area(
    level_t *level,
    v2 mi,
    v2 ma,
    traverse_blocks_f callback,
    void *userdata,
    int flags);

void level_update_wall_blocks(
    level_t *level,
    wall_t *wall,
    const v2 *old_v0,
    const v2 *old_v1);

void level_update_sector_blocks(
    level_t *level,
    sector_t *sector);

void level_set_subsector_blocks(
    level_t *level,
    subsector_t *sub);

void level_blocks_remove_subsector(
    level_t *level,
    subsector_t *sub);

// retrieve blocks spanned by sector
// appends to out, does not clear
void level_blocks_for_sector(
    level_t *level, sector_t *sector, DYNLIST(block_t*) *out);
