#pragma once

#include "util/bitmap.h"
#include "util/types.h"
#include "util/macros.h"
#include "util/alloc.h"

// implements blklist_t, a list which only supports "add at any index" and
// "remove at specific index" ops but maintains pointer AND index stability by
// allocating in blocks of N elements at a time
//
// internally implemented as a series of allocator_t allocated blocks prefixed
// each by an in-place bitmap_t keeping track of free indices

// header of a block layout (always just void* underlying)
// * aligned bitmap_t with enough space for bitmap_t and bytes for block_size
// * space for data, t_size * block_size
typedef struct {} blklist_block_t;

typedef struct blklist {
    allocator_t *allocator;

    // blocks - entries CAN BE NULL if block has been silently deallocated
    DYNLIST(blklist_block_t*) blocks;

    i32 size, capacity, block_size, t_size, good_block, max_size, max_index;
} blklist_t;

typedef struct {
    allocator_t *allocator;

    // size of each underlying block
    int block_size;

    // sizeof T for blklist(T)
    int t_size;

    // maximum allowed size (max index is max_size - 1)
    // (block_size % max_size) == 0 must hold
    int max_size;
} blklist_desc_t;

// initializes a blklist
void blklist_init(blklist_t *bl, const blklist_desc_t *desc);

// destroys a blklist
void blklist_destroy(blklist_t *bl);

// clears a blklist
void blklist_clear(blklist_t *bl);

// add into list at any index, returns pointer to allocated space
// can fail if list has max size and adding would exceed
void *blklist_add_voidp(blklist_t *bl);

// add into list at specified index, returns pointer to allocated space
// can fail if beyond maximum size or index is already occupied
void *blklist_try_add_at_voidp(blklist_t *bl, i32 index);

// removes from list at index
void blklist_remove(blklist_t *bl, i32 index);

// gets index from pointer into list, -1 if invalid
i32 blklist_index_of(const blklist_t *bl, const void *p);

// returns true if index exists in list
bool blklist_present(const blklist_t *bl, i32 index);

// get pointer to element at index
void *blklist_ptr_voidp(const blklist_t *bl, i32 index);

// get pointer to element at index (NO BOUNDS CHECK!)
void *blklist_ptr_voidp_unsafe(const blklist_t *bl, i32 index);

// get pointer to element at index IF it is present, otherwise return NULL
void *blklist_try_ptr_voidp(const blklist_t *bl, i32 index);

// gets next valid index for list after i, -1 if no such index
// pass i == -1 to get the first valid index
i32 blklist_next_index(const blklist_t *bl, i32 i);

// get memory footpri32 of list in bytes
usize blklist_footprint(const blklist_t *bl);

// get overhead (amount of extra memory used compared to simple array) of list
// in bytes
usize blklist_overhead(const blklist_t *bl);

// insert new element at any index, returns pointer to new element
#define blklist_add(_T, _l) ((_T*) blklist_add_voidp((_l)))

// tries to add element of type _T in list _l at index _i, returns NULL if index
// is out of bounds or already occupied
#define blklist_try_add_at(_T, _l, _i)                                         \
    ((_T*) blklist_try_add_at_voidp((_l), (_i)))

// get pointer to element at index
#define blklist_ptr(_T, _l, _i) ((_T*) blklist_ptr_voidp((_l), (_i)))

// get pointer to element at index (NO BOUNDS CHECK!)
#define blklist_ptr_unsafe(_T, _l, _i)                                         \
    ((_T*) blklist_ptr_voidp_unsafe((_l), (_i)))

// get pointer to element at index if it is present, otherwise returns NULL
#define blklist_try_ptr(_T, _l, _i) ((_T*) blklist_try_ptr_voidp((_l), (_i)))

#define _blklist_each_impl(T, LIST, IT, PNAME, ITNAME, INAME)                  \
    typedef struct { i32 i; T* el; } ITNAME;                                   \
    typeof((LIST)) PNAME = (LIST);                                             \
    for (ITNAME IT = ({                                                        \
            const int INAME = blklist_next_index(PNAME, -1);                   \
            (ITNAME) {                                                         \
                .i = INAME,                                                    \
                .el = INAME == -1 ? NULL : blklist_ptr_unsafe(T, LIST, INAME), \
            };                                                                 \
         });                                                                   \
         IT.i != -1 && IT.i < PNAME->capacity;                                 \
         IT.i = blklist_next_index(PNAME, IT.i),                               \
         IT.el = IT.i == -1 ? NULL : blklist_ptr_unsafe(T, LIST, IT.i))

// usage: blklist_each(<type>, <list>, <it>) { ... }
#define blklist_each(T, LIST, IT)                                              \
    _blklist_each_impl(                                                        \
        T,                                                                     \
        LIST,                                                                  \
        IT,                                                                    \
        CONCAT(_BLP, __COUNTER__),                                             \
        CONCAT(_BLI, __COUNTER__),                                             \
        CONCAT(_BLT, __COUNTER__))

#define _blklist_each_voidp_impl(LIST, IT, PNAME, ITNAME, INAME)               \
    typedef struct { i32 i; void* el; } ITNAME;                                \
    typeof((LIST)) PNAME = (LIST);                                             \
    for (ITNAME IT = ({                                                        \
            const int INAME = blklist_next_index(PNAME, -1);                   \
            (ITNAME) {                                                         \
                .i = INAME,                                                    \
                .el =                                                          \
                    INAME == -1 ?                                              \
                        NULL                                                   \
                        : blklist_ptr_voidp_unsafe(LIST, INAME),               \
            };                                                                 \
         });                                                                   \
         IT.i != -1 && IT.i < PNAME->capacity;                                 \
         IT.i = blklist_next_index(PNAME, IT.i),                               \
         IT.el = IT.i == -1 ? NULL : blklist_ptr_voidp_unsafe(LIST, IT.i))

// usage: blklist_each_voidp(<list>, <it>) { ... }
#define blklist_each_voidp(LIST, IT)                                           \
    _blklist_each_voidp_impl(                                                  \
        LIST,                                                                  \
        IT,                                                                    \
        CONCAT(_BLP, __COUNTER__),                                             \
        CONCAT(_BLI, __COUNTER__),                                             \
        CONCAT(_BLT, __COUNTER__))

// current size of blklist
M_INLINE int blklist_size(const blklist_t *bl) { return bl->size; }

// current max index of blklist
M_INLINE int blklist_max_index(const blklist_t *bl) { return bl->max_index; }

// returns true if blklist is not zero initialized
M_INLINE bool blklist_valid(const blklist_t *bl) {
    return !!bl->allocator && !!bl->blocks;
}

#ifdef UTIL_IMPL
#include "util/math.h"
#include "util/assert.h"
#include "util/dynlist.h"

void blklist_init(blklist_t *bl, const blklist_desc_t *desc) {
    ASSERT(desc->allocator);
    ASSERT(desc->t_size >= 0);
    ASSERT(desc->block_size >= 0);
    ASSERT(desc->max_size >= 0);
    ASSERT(desc->max_size % desc->block_size == 0);

    *bl = (blklist_t) {
        .allocator = desc->allocator,
        .size = 0,
        .capacity = 0,
        .block_size = desc->block_size,
        .t_size = desc->t_size,
        .good_block = -1,
        .blocks = dynlist_create(blklist_block_t*, desc->allocator, 1),
        .max_size = desc->max_size,
    };
}

void blklist_destroy(blklist_t *bl) {
    dynlist_each(bl->blocks, it) {
        if (*it.el) {
            mem_free(bl->allocator, *it.el);
        }
    }
    dynlist_destroy(bl->blocks);

    *bl = (blklist_t) { 0 };
}

void blklist_clear(blklist_t *bl) {
    dynlist_each(bl->blocks, it) {
        if (*it.el) {
            mem_free(bl->allocator, *it.el);
        }
    }

    dynlist_resize(bl->blocks, 0);
    bl->good_block = -1;
    bl->size = 0;
    bl->capacity = 0;
}

M_INLINE bitmap_t *block_bits(const blklist_t *bl, blklist_block_t *block) {
    return (bitmap_t*) block;
}

M_INLINE u8 *block_data(const blklist_t *bl, blklist_block_t *block) {
    return
        &((u8*) block)[
            round_up_to_mult(
                sizeof(bitmap_t)
                    + BITMAP_SIZE_TO_BYTES(bl->block_size),
                MAX_ALIGN)];
}

// allocate a new block for the specified list
// does not insert into internal block list
M_INLINE blklist_block_t *block_alloc(blklist_t *bl) {
    // allocate for bitmap + data
    blklist_block_t *block =
        mem_alloc(
            bl->allocator,
            round_up_to_mult(
                sizeof(bitmap_t)
                    + BITMAP_SIZE_TO_BYTES(bl->block_size),
                MAX_ALIGN)
                + (bl->t_size * bl->block_size));

    // init and zero bitmap
    bitmap_init(block_bits(bl, block), NULL, bl->block_size);
    bitmap_fill(block_bits(bl, block), 0);
    return block;
}

void *blklist_add_voidp(blklist_t *bl) {
    blklist_block_t *block = NULL;

    if (bl->size == bl->max_size) {
        // cannot expand
        return NULL;
    }

    // index of chosen block, internal free index into block
    i32 block_index = -1, internal_index = -1;

    ASSERT_DEBUG(bl->good_block == -1 || bl->blocks[bl->good_block]);

    // do we have a good_block?
    if (bl->good_block != -1) {
        const bitmap_t *bits = block_bits(bl, bl->blocks[bl->good_block]);
        internal_index = bitmap_find(bits, 0, false);

        if (internal_index != -1) {
            int count = bitmap_count(bits, true);
            ASSERT_DEBUG(count != bl->block_size);

            // got good block and good i
            block_index = bl->good_block;
            block = bl->blocks[bl->good_block];
        }
    }

    if (block_index == -1) {
        internal_index = -1;

        // scan from start of blocks
        dynlist_each(bl->blocks, it) {
            if (!*it.el) { continue; }

            internal_index = bitmap_find(block_bits(bl, *it.el), 0, false);

            if (internal_index != -1) {
                // found a good spot
                block_index = it.i;
                block = *it.el;
                break;
            }
        }

        if (internal_index == -1) {
            // not good, no block with available space
            block = NULL;
            block_index = -1;
        }
    }

    // check if we need to add a block
    if (!block) {
        // check if we can add a block...
        if (bl->max_size != 0
            && dynlist_size(bl->blocks) >= (bl->max_size / bl->block_size)) {
            return NULL;
        }

        block = block_alloc(bl);

        // allocate first element
        internal_index = 0;

        // check if there is free space (silently deallocated) for this block
        for (block_index = 0;
             block_index < dynlist_size(bl->blocks);
             block_index++) {
            if (!bl->blocks[block_index]) { break; }
        }

        if (block_index == dynlist_size(bl->blocks)) {
            // add block on end
            *dynlist_insert(bl->blocks, block_index) = block;
        } else {
            // assign to previously deallocated block
            bl->blocks[block_index] = block;
        }

        // recalculate capacity in case new block was added
        bl->capacity = bl->block_size * dynlist_size(bl->blocks);
    }

    ASSERT_DEBUG(block);
    ASSERT_DEBUG(internal_index != -1);
    ASSERT_DEBUG(block_index >= 0);
    ASSERT_DEBUG(block_index < dynlist_size(bl->blocks));

    // this is a good block if this isn't the last index being allocated
    if (internal_index != bl->block_size - 1) {
        bl->good_block = block_index;
    }

    bl->size++;

    const int true_index = internal_index + (block_index * bl->block_size);
    bl->max_index = max(bl->max_index, true_index);

    ASSERT_DEBUG(!bitmap_get(block_bits(bl, block), internal_index));
    bitmap_set(block_bits(bl, block), internal_index);
    ASSERT_DEBUG(blklist_present(bl, true_index));

    u8 *data = block_data(bl, block);
    return data + ((internal_index % bl->block_size) * bl->t_size);
}

void *blklist_try_add_at_voidp(blklist_t *bl, i32 index) {
    ASSERT(index >= 0);

    if (bl->max_size != 0 && index >= bl->max_size) {
        // out of range
        return NULL;
    }

    // index of chosen block, internal free index into block
    i32 block_index = index / bl->block_size,
        internal_index = index % bl->block_size;

    ASSERT_DEBUG(internal_index != -1);
    ASSERT_DEBUG(block_index >= 0);

    // extend list to accomodate up to block with empty blocks
    while (block_index >= dynlist_size(bl->blocks)) {
        *dynlist_push(bl->blocks) = NULL;
    }

    ASSERT_DEBUG(block_index < dynlist_size(bl->blocks));

    blklist_block_t **pblock = &bl->blocks[block_index];

    if (*pblock) {
        // check if index is free
        if (bitmap_get(block_bits(bl, *pblock), internal_index)) {
            return NULL;
        }
    } else {
        // need to add the block
        *pblock = block_alloc(bl);

        // this block is good, it's newly allocated
        bl->good_block = block_index;

        // new block, recalc capacity
        bl->capacity = bl->block_size * dynlist_size(bl->blocks);
    }

    ASSERT_DEBUG(*pblock);

    bl->size++;

    const int true_index = internal_index + (block_index * bl->block_size);
    bl->max_index = max(bl->max_index, true_index);

    ASSERT_DEBUG(!bitmap_get(block_bits(bl, *pblock), internal_index));
    bitmap_set(block_bits(bl, *pblock), internal_index);
    ASSERT_DEBUG(blklist_present(bl, true_index));

    u8 *data = block_data(bl, *pblock);
    return data + ((internal_index % bl->block_size) * bl->t_size);
}

void blklist_remove(blklist_t *bl, i32 index) {
    ASSERT(index >= 0 && index <= bl->capacity);

    const int
        block_index = index / bl->block_size,
        i_block = index % bl->block_size;

    ASSERT(block_index < dynlist_size(bl->blocks));

    blklist_block_t *block = bl->blocks[block_index];
    ASSERT(block);

    ASSERT(bitmap_get(block_bits(bl, block), i_block));
    bitmap_clr(block_bits(bl, block), i_block);

    bl->size--; 

    // check if we can deallocate block
    if (bitmap_count(block_bits(bl, block), 1) == 0) {
        // remove block, zero place out - may not be able to shrink list
        // however
        mem_free(bl->allocator, block);
        bl->blocks[block_index] = NULL;

        // remove NULL blocks off of list end (this block + maybe more)
        while (
            dynlist_size(bl->blocks) > 0
            && !bl->blocks[dynlist_size(bl->blocks) - 1]) {
            dynlist_pop(bl->blocks);
            bl->capacity -= bl->block_size;
        }

        if (bl->good_block == block_index) {
            bl->good_block = -1;
        }

        ASSERT_DEBUG(bl->good_block == -1 || bl->blocks[bl->good_block]);
    }

    if (index == bl->max_index) {
        // recalculate max index
        bl->max_index = 0;

        // look from the end of the list on the right side of each block
        for (int i = dynlist_size(bl->blocks) - 1; i >= 0; i--) {
            blklist_block_t *blk = bl->blocks[i];

            // empty blocks on the end of the list *should* have been
            // deallocated
            ASSERT_DEBUG(blk);

            const bitmap_t *bits = block_bits(bl, blk);

            const int j = bitmap_rfind(bits, -1, true);
            if (j != -1) {
                bl->max_index = (i * bl->block_size) + j;
                break;
            }
        }
    }
}

i32 blklist_index_of(const blklist_t *bl, const void *p) {
    dynlist_each(bl->blocks, it) {
        if (!*it.el) { continue; }

        void *data = block_data(bl, *it.el);
        if (p >= data && p < (data + (bl->block_size * bl->t_size))) {
            return
                (it.i * bl->block_size)
                    + (((int) (p - data)) / bl->t_size);
        }
    }

    return -1;
}

bool blklist_present(const blklist_t *bl, i32 index) {
    const i32 block_index = index / bl->block_size;

    if (M_UNLIKELY(
            index < 0
            || index >= bl->capacity
            || block_index >= dynlist_size(bl->blocks)
            || !bl->blocks[block_index])) {
        return false;
    }

    return
        bitmap_get(
            block_bits(bl, bl->blocks[block_index]),
            index % bl->block_size);
}

void *blklist_ptr_voidp(const blklist_t *bl, i32 index) {
    ASSERT_DEBUG(blklist_present(bl, index));

    return
        block_data(bl, bl->blocks[index / bl->block_size])
            + ((index % bl->block_size) * bl->t_size);
}

void *blklist_ptr_voidp_unsafe(const blklist_t *bl, i32 index) {
    return
        block_data(bl, bl->blocks[index / bl->block_size])
            + ((index % bl->block_size) * bl->t_size);
}

void *blklist_try_ptr_voidp(const blklist_t *bl, i32 index) {
    const i32 block_index = index / bl->block_size;

    if (M_UNLIKELY(
            index < 0
            || index >= bl->capacity
            || block_index >= dynlist_size(bl->blocks))) {
        return NULL;
    }

    blklist_block_t *block = bl->blocks[block_index];

    if (M_UNLIKELY(
            !block
            || !bitmap_get(block_bits(bl, block), index % bl->block_size))) {
        return NULL;
    }

    return block_data(bl, block) + ((index % bl->block_size) * bl->t_size);
}

i32 blklist_next_index(const blklist_t *bl, i32 index) {
    i32 block_index;

    if (index == -1) {
        // start searching at block 0
        block_index = -1;
        goto find_next;
    } else {
        block_index = index / bl->block_size;
    }

    if (block_index < 0 || block_index >= dynlist_size(bl->blocks)) {
        // index is invalid
        return -1;
    }

    i32 i;

    // check if we can advance in the same block -
    // * block must still exist
    // * must not be at last index of block
    // * must be able to find next present bit in block
    if (bl->blocks[block_index]
        && (index % bl->block_size) != bl->block_size - 1
        && (i =
            bitmap_find(
                block_bits(bl, bl->blocks[block_index]),
                (index % bl->block_size) + 1,
                true)) != -1) {
        return (block_index * bl->block_size) + i;
    }

find_next:;

    // try to advance to the next block
    dynlist_each(bl->blocks, it, block_index + 1) {
        if (!*it.el) { continue; }

        i = bitmap_find(block_bits(bl, *it.el), 0, true);

        // if i is -1, that means that this block *should* have been deallocated
        // but wasn't for some reason. if you hit this assert, something is
        // wrong in blklist_remove
        ASSERT_DEBUG(i != -1);

        return (ARR_PTR_INDEX(bl->blocks, it.el) * bl->block_size) + i;
    }

    // could not find next block, must be at the end
    return -1;
}

usize blklist_footprint(const blklist_t *bl) {
    usize n = 0;
    n += dynlist_footprint(bl->blocks);

    dynlist_each(bl->blocks, it) {
        if (*it.el) {
            n +=
                round_up_to_mult(
                    sizeof(bitmap_t) + BITMAP_SIZE_TO_BYTES(bl->block_size),
                    MAX_ALIGN)
                + (bl->t_size * bl->block_size);
        }
    }

    return n;
}

usize blklist_overhead(const blklist_t *bl) {
    usize n = 0;

    dynlist_each(bl->blocks, it) {
        if (!*it.el) { continue; }

        // add bitmap size
        n +=
            round_up_to_mult(
                sizeof(bitmap_t) + BITMAP_SIZE_TO_BYTES(bl->block_size),
                MAX_ALIGN);

        // add for number of unused entries
        n += bl->t_size * bitmap_count(block_bits(bl, *it.el), false);
    }

    return
        sizeof(blklist_t)
            + dynlist_size_bytes(bl->blocks)
            + n;
}

#endif // ifdef UTIL_IMPL
