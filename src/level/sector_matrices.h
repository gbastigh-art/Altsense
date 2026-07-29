#pragma once

#include "defs.h"
#include "util/bitmap.h"

typedef struct {
    int n, new_n, temp_n;

    u8 *matrix, *new_matrix, *temp_matrix;
} sector_matrix_t;

typedef struct sector_matrix_each_it {
    int i;
    sector_t *el;
} sector_matrix_each_it_t;

#define _sector_matrix_each_impl(level_, pmtx_, sect_, it_, bitmap_)   \
    bitmap_t bitmap_ = sector_matrix_bits((level_), (pmtx_), (sect_)); \
    sector_matrix_each_it_t it_ = { 0 };                               \
    it_.i = -1;                                                        \
    while ((it_.i = bitmap_find(&bitmap_, it_.i + 1, true)) != -1      \
           && it_.i < (level_)->sectors.data.capacity                  \
           && ((it_.el = genlist_try_ptr_from_index(sector_t, &(level_)->sectors, it_.i), true)))

#define sector_matrix_each(level_, pmtx_, sect_, it_)                    \
    _sector_matrix_each_impl(                                            \
        level_,                                                          \
        pmtx_,                                                           \
        sect_,                                                           \
        it_,                                                             \
        CONCAT(_smeb, __COUNTER__))

// enqueue a recompute of all sector matrices
void sector_matrices_recompute(level_t *level);

// update sector matrix data for level, call once per frame
void sector_matrices_update(level_t *level);

// get simple bitset from sector matrix
bitmap_t sector_matrix_bits(
    const level_t *level,
    const sector_matrix_t *mtx,
    const sector_t *s);

enum {
    SECTOR_MATRIX_NO_FLAGS     = 0 << 0,
    SECTOR_MATRIX_WITH_PORTALS = 1 << 0
};

// get list of sectors marked in matrix for the specified sector
int sector_matrix_get(
    const level_t *level,
    const sector_matrix_t *mtx,
    const sector_t *s,
    DYNLIST(sector_t*) *out,
    int flags);

// get sectors marked in matrix for the specified sector
// bitmap must have at least level->sector->max_index storage
void sector_matrix_get_as_bitmap(
    const level_t *level,
    const sector_matrix_t *mtx,
    const sector_t *s,
    bitmap_t *out,
    int flags);

// returns true if specified matrix entry for a has b's bit marked
bool sector_matrix_get_for_sector(
    const level_t *level,
    const sector_matrix_t *mtx,
    const sector_t *a,
    const sector_t *b);

// mtx into any
void sector_matrix_to_any(
        const level_t *level,
        any_t *dst,
        const sector_matrix_t *mtx);

// mtx from any
bool sector_matrix_from_any(
        level_t *level,
        sector_matrix_t *mtx,
        const any_t *src,
        const char **errmsg);
