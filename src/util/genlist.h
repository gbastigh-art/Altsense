#pragma once

#include "util/types.h"
#include "util/blklist.h"

#define GENLIST_HANDLE_BITS 20 // 2^20 = 1048576
#define GENLIST_GEN_BITS    12 // 2^12 = 4096

STATIC_ASSERT(GENLIST_HANDLE_BITS + GENLIST_GEN_BITS == 32);

typedef u16 genlist_gen_t;
STATIC_ASSERT(GENLIST_GEN_BITS <= sizeof(genlist_gen_t) * 8);

// generational list
typedef struct genlist {
    blklist_t data;

    // generation for each slot in data list, never shrinks
    DYNLIST(genlist_gen_t) gens;

    // maximum size, 0 if allowed to grow to any size
    // also gives the maximum allowed index for the block list
    int max_size;
} genlist_t;

// NOTE NOTE NOTE
// if changes are made to this struct, other anonymous unions MUST be changed to
// match!! grep the codebase for GENLIST_*_BITS.
// NOTE NOTE NOTE
typedef struct {
    int i: GENLIST_HANDLE_BITS;
    genlist_gen_t gen: GENLIST_GEN_BITS;
} genlist_handle_t;

#define GENLIST_HANDLE_NULL ((genlist_handle_t) { 0, 0 })

// returns true if two indices are equal
M_INLINE bool genlist_handle_eq(genlist_handle_t a, genlist_handle_t b) {
    return a.gen == b.gen && a.i == b.i;
}

// returns true if idx is GENLIST_HANDLE_NULL
M_INLINE bool genlist_handle_is_null(genlist_handle_t idx) {
    return !idx.gen && !idx.i;
}

typedef struct {
    allocator_t *allocator;

    // size of each block
    int block_size;

    // size of T for genlist(T)
    int t_size;

    // (optional) maximum size
    int max_size;
} genlist_desc_t;

void genlist_init(genlist_t *l, const genlist_desc_t *desc);

void genlist_destroy(genlist_t *l);

// clears list, keeping generations
void genlist_clear(genlist_t *l);

// resets list, ALSO ZEROING GENERATIONS (!) this invalidates all indices
void genlist_reset(genlist_t *l);

// add element to list, returns pointer to slot + optional index
// can fail if list has max size and adding would exceed
void *genlist_add_voidp(genlist_t *l, genlist_handle_t *handle_out);

// try to add to genlist at specific location, if not otherwise occupied
void *genlist_try_add_at_voidp(
        genlist_t *l,
        int i,
        genlist_handle_t *handle_out);

// try convert idx -> ptr, returns NULL if not present
void *genlist_try_ptr_voidp(const genlist_t *l, genlist_handle_t idx);

// returns true if idx is present
bool genlist_present(const genlist_t *l, genlist_handle_t idx);

// remove element at idx, crashes if not present
void genlist_remove(genlist_t *l, genlist_handle_t idx);

// try to remove element at idx if present, returns true on success
bool genlist_try_remove(genlist_t *l, genlist_handle_t idx);

// try to get index from raw index into list, returns GENLIST_HANDLE_NULL if not
// present
genlist_handle_t genlist_handle_of_index(const genlist_t *l, int index);

// try to get index from pointer into list, returns GENLIST_HANDLE_NULL if not
// present
genlist_handle_t genlist_handle_of_ptr(const genlist_t *l, const void *p);

// try to get void* from raw index
void *genlist_try_ptr_from_index_voidp(const genlist_t *l, int index);

// (type, list, handle_out)
#define _genlist_add3(T, l, h) ((T*) (genlist_add_voidp((l), (h))))

// (type, list)
#define _genlist_add2(T, l) ((T*) (genlist_add_voidp((l), NULL)))

// add an element of type T, returns pointer to element slot
// vmacro: (type, list) or (type, list, handle_out)
#define genlist_add(...) VMACRO(_genlist_add, __VA_ARGS__)

// (type, list, index, handle_out)
#define _genlist_try_add_at4(T, l, i, h) \
    ((T*) (genlist_try_add_at_voidp((l), (i), (h))))

// (type, list, index)
#define _genlist_try_add_at3(T, l, i) \
    ((T*) (genlist_add_voidp((l), (i), NULL)))

// tries to add an element of type T at specified index
// returns pointer to element slot (or NULL on failure)
// vmacro: (type, list, index) or (type, list, index, handle_out)
#define genlist_try_add_at(...) VMACRO(_genlist_try_add_at, __VA_ARGS__)

// try to get T* from index i
#define genlist_try_ptr(T, l, i) ((T*) (genlist_try_ptr_voidp((l), (i))))

// try to get T* from genlist_handle_t i
#define genlist_try_ptr_from_index(T, l, i) \
    ((T*) (genlist_try_ptr_from_index_voidp((l), (i))))

// iterate over a genlist
#define genlist_each(T, l, ...) blklist_each(T, &(l)->data, __VA_ARGS__)

// iterate over a genlist (void pointer)
#define genlist_each_voidp(l, ...) blklist_each_voidp(&(l)->data, __VA_ARGS__)

// current size of genlist
M_INLINE int genlist_size(const genlist_t *l) { return l->data.size; }

// current max index of genlist
M_INLINE int genlist_max_index(const genlist_t *l) {
    return blklist_max_index(&l->data);
}

#ifdef UTIL_IMPL

void genlist_init(genlist_t *l, const genlist_desc_t *desc) {
    ASSERT(desc->block_size >= 0);
    ASSERT(desc->t_size >= 0);
    ASSERT(desc->max_size >= 0);
    ASSERT(desc->max_size % desc->block_size == 0);

    blklist_init(
        &l->data,
        &(blklist_desc_t) {
            .allocator = desc->allocator,
            .block_size = desc->block_size,
            .t_size = desc->t_size,
            .max_size = desc->max_size,
        });

    dynlist_init(l->gens, desc->allocator);
    l->max_size = desc->max_size;
}

void genlist_destroy(genlist_t *l) {
    blklist_destroy(&l->data);
    dynlist_destroy(l->gens);
    *l = (genlist_t) { 0 };
}

void genlist_clear(genlist_t *l) {
    blklist_clear(&l->data);
}

void genlist_reset(genlist_t *l) {
    blklist_clear(&l->data);
    dynlist_resize(l->gens, 0);
}

static void genlist_ensure_gen_capacity_up_to(genlist_t *l, int index) {
    // ensure capacity in indices
    if (index >= dynlist_size(l->gens)) {
        const i32 old_size = dynlist_size(l->gens);
        dynlist_resize(l->gens, index + 1);

        // initialize to gen 0
        memset(
            &l->gens[old_size],
            0,
            ((index + 1) - old_size) * sizeof(l->gens[0]));
    }
}

void *genlist_add_voidp(genlist_t *l, genlist_handle_t *handle_out) {
    void *result = blklist_add_voidp(&l->data);

    if (!result) {
        return NULL;
    }

    const i32 index = blklist_index_of(&l->data, result);

    genlist_ensure_gen_capacity_up_to(l, index);

    if (l->gens[index] == 0) {
        l->gens[index] = 1;
    }

    if (handle_out) {
        *handle_out = (genlist_handle_t) { .gen = l->gens[index], .i = index };
    }

    return result;
}

void *genlist_try_add_at_voidp(
        genlist_t *l,
        int index,
        genlist_handle_t *handle_out) {
    void *result = blklist_try_add_at_voidp(&l->data, index);

    if (!result) {
        return NULL;
    }

    genlist_ensure_gen_capacity_up_to(l, index);

    if (l->gens[index] == 0) {
        l->gens[index] = 1;
    }

    if (handle_out) {
        *handle_out = (genlist_handle_t) { .gen = l->gens[index], .i = index };
    }

    return result;
}


void *genlist_try_ptr_voidp(const genlist_t *l, genlist_handle_t idx) {
    if (idx.i >= (int) dynlist_size(l->gens)
        || l->gens[idx.i] != idx.gen) {
        return NULL;
    }

    return blklist_try_ptr_voidp(&l->data, idx.i);
}

bool genlist_present(const genlist_t *l, genlist_handle_t idx) {
    return !!genlist_try_ptr_voidp(l, idx);
}

void genlist_remove(genlist_t *l, genlist_handle_t idx) {
    ASSERT_DEBUG(genlist_present(l, idx));

    blklist_remove(&l->data, idx.i);

    if (l->gens[idx.i] == (1 << GENLIST_GEN_BITS) - 1) {
        l->gens[idx.i] = 0;
    } else {
        l->gens[idx.i]++;
    }
}

bool genlist_try_remove(genlist_t *l, genlist_handle_t idx) {
    if (!genlist_present(l, idx)) {
        return false;
    }

    genlist_remove(l, idx);
    return true;
}

genlist_handle_t genlist_handle_of_index(const genlist_t *l, int index) {
    if (!blklist_present(&l->data, index)) { return GENLIST_HANDLE_NULL; }
    return (genlist_handle_t) { .gen = l->gens[index], .i = index };
}

void *genlist_try_ptr_from_index_voidp(const genlist_t *l, int index) {
    const genlist_handle_t idx = genlist_handle_of_index(l, index);
    return genlist_handle_is_null(idx) ? NULL : genlist_try_ptr_voidp(l, idx);
}

genlist_handle_t genlist_handle_of_ptr(const genlist_t *l, const void *p) {
    const i32 index = blklist_index_of(&l->data, p);
    if (index == -1) { return GENLIST_HANDLE_NULL; }
    if (!blklist_present(&l->data, index)) { return GENLIST_HANDLE_NULL; }
    return (genlist_handle_t) { .gen = l->gens[index], .i = index };
}

#endif // ifdef UTIL_IMPL
