#pragma once

// simple linear probing hash-map implementation
// can be used with value size == 0 as a set

#include <string.h>
#include <stdlib.h>

#include "util/alloc.h"
#include "util/hash.h"
#include "util/types.h"
#include "util/assert.h"
#include "util/dynlist.h"

typedef hash_t (*map_hash_f)(map_t*, const void*);
typedef void *(*map_dup_f)(map_t*, void*);
typedef void (*map_free_f)(map_t*, void*);
typedef int (*map_cmp_f)(map_t*, const void*, const void*);

typedef struct map_entry {
    bool used   : 1;  // true if entry is present
    u16 dist    : 15; // distance from "desired" position (linear probing)
    hash_t hash : 48; // lower 48 hash bits
} map_entry_t;

STATIC_ASSERT(sizeof(hash_t) >= 8);

typedef struct map {
    allocator_t *allocator;

    map_hash_f f_hash;
    map_cmp_f f_keycmp;
    map_free_f f_keyfree, f_valfree;

    int key_size, value_size, bucket_size, value_offset, used;

    // backing data: stored entries + buckets
    void *data;

    // arbitrary extra data, for user usage in function parameters
    void *userdata;

    // current prime
    u8 prime;
} map_t;

extern const int _MAP_PRIMES[29];

// hash functions
hash_t map_hash_bytes(map_t *m, const void *p);
hash_t map_hash_str(map_t*, const void *p);
hash_t map_hash_inplace_str(map_t*, const void *p);

// compare functions
int map_cmp_bytes(map_t *m, const void *p, const void *q);
int map_cmp_str(map_t*, const void *p, const void *q);
int map_cmp_inplace_str(map_t *m, const void *p, const void *q);

// default map_free_fn implemented with stdlib's "free()", assumes values are
// pointers into heap memory
void map_default_free(map_t*, void *p);

// map_free_fn which uses map_t::allocator to free pointers
void map_allocator_free(map_t*, void *p);

// free for dynlists
void map_free_dynlist(map_t*, void *p);

// internal use only
// returns position on insert
int _map_insert(
    map_t *map,
    const void *key,
    int key_size,
    const void *value,
    int value_size,
    bool ignore_if_found);

// internal use only
// finds key in map, returns -1 if not found
int _map_find_pos(const map_t *map, const void *key, int key_size);

// finds index of key in map (for map_*_at), returns -1 if not found
#define map_find_index(_m, _k) \
    (_map_find_pos((_m), (_k), sizeof(*(_k)) /* NOLINT */))

enum {
    _MAP_REMOVE_AT_NO_FLAGS = 0 << 0,
    _MAP_REMOVE_AT_NO_REHASH = 1 << 0,
    _MAP_REMOVE_AT_NO_VALFREE = 1 << 1,
};

// internal use only
void _map_remove_at(map_t *map, int pos, int flags);

// internal use only
// removes key with specified size from map if present, copying value into "out"
// if not NULL
// returns false if key is not in map
bool _map_try_removep(
    map_t *map,
    const void *key,
    int key_size,
    void *out,
    int out_size);

#define _map_try_removep2(_m, _k)                                               \
    (_map_try_removep(_m, (_k), sizeof(*(_k)), NULL, 0) /* NOLINT */)

#define _map_try_removep3(_m, _k, _o)                                           \
    (_map_try_removep(_m, (_k), sizeof(*(_k)), (_o), sizeof(*(_o))) /* NOLINT */)

// bool map_try_removep(map, ptr key)/(map, ptr key, value_out)
// removes key from map if present, returns false if not present
// if type value_out is present and key is present, value is copied out to
// value_out
#define map_try_removep(...) VMACRO(_map_try_removep, __VA_ARGS__)

#define _map_try_remove2(_m, _k) ({                                    \
        typeof(_k) __k = (_k);                                         \
        _map_try_removep(_m, &__k, sizeof(__k) /* NOLINT */, NULL, 0); \
    })                                                                 \

#define _map_try_remove3(_m, _k, _o) ({                                \
        typeof(_k) __k = (_k);                                         \
        _map_try_removep(                                              \
            _m, &__k, sizeof(__k) /* NOLINT */, (_o), sizeof(*(_o)));  \
    })                                                                 \

// bool map_try_remove(map, key)/(map, key, value_out)
// removes key from map if present, returns false if not present
// if type value_out is present and key is present, value is copied out to
// value_out
#define map_try_remove(...) VMACRO(_map_try_remove, __VA_ARGS__)

// create new map
void map_init(
    map_t *map,
    allocator_t *allocator,
    int key_size,
    int value_size,
    map_hash_f f_hash,
    map_cmp_f f_keycmp,
    map_free_f f_keyfree,
    map_free_f f_valfree,
    void *userdata);

// destroy map
void map_destroy(map_t *map);

// clear map of all keys and values
void map_clear(map_t *map);

// returns true if map has been initialized
bool map_valid(const map_t *map);

// internal usage only
bool _map_contains(const map_t *map, const void *key, int key_size);

// returns true if map contains ptr key
#define map_containsp(_m, _k)                                                   \
    (_map_contains((_m), (_k), sizeof(*(_k)) /* NOLINT */))

// returns true if map contains key
#define map_contains(_m, _k) ({                                                 \
        typeof(_k) __k = (_k);                                                  \
        _map_contains((_m), &__k, sizeof(__k) /* NOLINT */);                                 \
    })

// get heap footprint of map
int map_footprint(const map_t *map);

// total map capacity
#define map_capacity(m) ((m)->data ? _MAP_PRIMES[(m)->prime] : 0)

M_INLINE map_entry_t *_map_entries(const map_t *map) {
    return map->data;
}

M_INLINE void *_map_buckets(const map_t *map) {
    if (M_UNLIKELY(!map->data)) { return NULL; }

    return
        map->data
            + round_up_to_mult(
                map_capacity(map) * sizeof(map_entry_t),
                MAX_ALIGN);
}

M_INLINE void *_map_bucket_at(const map_t *map, int index) {
    return _map_buckets(map) + (index * map->bucket_size);
}

#define map_key_at(T, _m, _i) ((T*) _map_key_at((_m), (_i)))

M_INLINE void *_map_key_at(const map_t *map, int index) {
    return _map_bucket_at(map, index);
}

#define map_value_at(T, _m, _i) ((T*) _map_value_at((_m), (_i)))

M_INLINE void *_map_value_at(const map_t *map, int index) {
    if (map->value_size == 0) { return NULL; }
    return _map_bucket_at(map, index) + map->value_offset;
}


// returns true if map is occupied at specified index
M_INLINE bool map_index_occupied(const map_t *map, int index) {
    ASSERT_DEBUG(index >= 0 && index <= map_capacity(map));
    return _map_entries(map)[index].used;
}

// insert (ptr key, NULL) into map (assumes value size is 0)
#define map_insertpk(_m, _k)                                                    \
    (_map_insert((_m), (_k), sizeof(*(_k)) /* NOLINT */, NULL, 0, false))

// insert (key, NULL) into map (assumes value size is 0)
#define map_insertk(_m, _k) ({                                                  \
        typeof((_k)) __k = (_k);                                                \
        map_t *__m = (_m);                                                      \
        const M_UNUSED int __n =                                                \
            _map_insert(__m, &__k, sizeof(__k) /* NOLINT */, NULL, 0, false);   \
        (typeof_pointer_to(typeof(_k))) _map_key_at(__m, __n);                  \
    })

// insert (key, NULL) into map - does no copy if found (assumes value size is 0)
#define map_insertk_if_not_found(_m, _k) ({                                    \
        typeof((_k)) __k = (_k);                                               \
        _map_insert((_m), &__k, sizeof(__k) /* NOLINT */, NULL, 0, true);      \
    })

// insert (*k, *v) into map, replacing value if it is already present, returns
// pointer to value with same type as _v
#define map_insertp(_m, _k, _v) ({                                              \
        map_t *__m = (_m);                                                      \
        const M_UNUSED int __n =                                                \
            _map_insert(                                                        \
                __m, (_k), sizeof(*(_k)) /* NOLINT */, (_v), sizeof(*(_v)), false);\
        (typeof((_v))) _map_value_at(__m, __n);                                 \
    })

// insert (k, v) into map, replacing value if it is already present, returns
// pointer to value with same type as _v
#define map_insert(_m, _k, _v) ({                                               \
        map_t *__m = (_m);                                                      \
        typeof(_k) __k = (_k);                                                  \
        typeof(_v) __v = (_v);                                                  \
        const M_UNUSED int __n =                                                \
            _map_insert(__m, &__k, sizeof(__k), &__v, sizeof(__v) /* NOLINT */, false);\
        (typeof(&__v)) _map_value_at(__m, __n);                                 \
    })

// returns _T *value, NULL if not present
#define _map_getp3(_T, _m, _k) ({                                               \
        const map_t *__m = (_m);                                                \
        const int _i = _map_find_pos(__m, (_k), sizeof(*(_k)) /* NOLINT */);    \
        _i == -1 ? NULL : (typeof_pointer_to(_T)) _map_value_at(__m, _i);       \
    })

// returns void **value, NULL if not present
#define _map_getp2(_m, _k) ({                                                   \
        const map_t *__m = (_m);                                                \
        const int _i = _map_find_pos(__m, (_k), sizeof(*(_k)) /* NOLINT */);    \
        _i == -1 ? NULL : _map_value_at(__m, _i);                               \
    })

// map_getp(map, ptr key)/(T, map, ptr key) -> ptr to value, NULL if not found
#define map_getp(...) VMACRO(_map_getp, __VA_ARGS__)

// returns _T *value, NULL if not present
#define _map_get3(_T, _m, _k) ({                                                \
        typeof(_k) __k = (_k);                                                  \
        _map_getp3(_T, (_m), &__k);                                             \
    })

// returns void **value, NULL if not present
#define _map_get2(_m, _k) ({                                                    \
        typeof(_k) __k = (_k);                                                  \
        _map_getp2((_m), &__k);                                                 \
    })

// map_get(map, key)/(T, map, key) -> ptr to value, NULL if not found
#define map_get(...) VMACRO(_map_get, __VA_ARGS__)

// get next used entry from map index _i. returns _m->capacity at map end
#define map_next(_m, _i) ({                                                     \
        typeof(_m) __m = (_m);                                                  \
        int _j = _i;                                                            \
        do {                                                                    \
            _j++;                                                               \
        } while (                                                               \
            __m->data                                                           \
            && _j < map_capacity(__m)                                           \
            && !_map_entries(__m)[_j].used);                                    \
        _j;                                                                     \
    })

#define _map_each_impl(_KT, _VT, _m, _it, _itname)                              \
    typedef struct {                                                            \
        typeof(_m) __m; int __i; _KT *key; _VT *value; } _itname;               \
    for (_itname _it = {                                                        \
            .__m = (_m),                                                        \
            .__i = map_next(_it.__m, -1),                                       \
            .key = _it.__i < (int) map_capacity(_it.__m) ?                      \
                _map_key_at(_it.__m, _it.__i) : NULL,                           \
            .value = _it.__i < (int) map_capacity(_it.__m) ?                    \
                _map_value_at(_it.__m, _it.__i) : NULL,                         \
         }; _it.__i < (int) map_capacity(_it.__m);                              \
         _it.__i = map_next(_it.__m, _it.__i),                                  \
        _it.key =                                                               \
            _it.__i < (int) map_capacity(_it.__m) ?                             \
                _map_key_at(_it.__m, _it.__i) : NULL,                           \
        _it.value =                                                             \
            _it.__i < (int) map_capacity(_it.__m) ?                             \
                _map_value_at(_it.__m, _it.__i) : NULL)

// map_each(KEY_TYPE, VALUE_TYPE, *map, it_name)
#define map_each(_KT, _VT, _m, _it)                                             \
    _map_each_impl(                                                             \
        _KT,                                                                    \
        _VT,                                                                    \
        _m,                                                                     \
        _it,                                                                    \
        CONCAT(_mei, __COUNTER__))

// remove from map while iterating
// NOTE: will *intentionally* not invoke any form of map rehash! this is to
// keep things stable under iteration.
// ALSO: invalidates any pointers on iterator, only valid operation is to
// continue iteration
#define map_remove_it(_m, _it) \
    (_map_remove_at((_m), (_it).__i, _MAP_REMOVE_AT_NO_REHASH))

// true if map is empty
M_INLINE bool map_empty(const map_t *map) { return map->used == 0; }

// number of elements in map
M_INLINE int map_size(const map_t *map) { return map->used; }

#ifdef UTIL_IMPL

// #define DO_DEBUG_MAP

#ifdef DO_DEBUG_MAP
#define DEBUG_MAP(...) LOG(__VA_ARGS__)
#define ASSERT_MAP(...) ASSERT(__VA_ARGS__)
#else
#define DEBUG_MAP(...)
#define ASSERT_MAP(...)
#endif // ifdef DO_DEBUG_MAP

#include "util/assert.h"
#include "util/math/util.h"

#include <stdlib.h>
#include <string.h>

// planetmath.org/goodhashtableprimes
const int _MAP_PRIMES[29] = {
    11, 23, 37, 53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317,
    196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843,
    50331653, 100663319, 201326611, 402653189, 805306457, 1610612741
};

// load factors at which rehashing happens
#define MAP_LOAD_HIGH 0.75
#define MAP_LOAD_LOW 0.25

#define MAP_MAX_DISTANCE U16_MAX

// mask for map_internal_entry_t::hash
#define MAP_HASH_MASK ((1ULL << 48) - 1)

M_INLINE void _map_memcpy(void *dst, const void *src, usize n) {
    switch (n) {
    case 8: *((u64*) dst) = *((u64*) src);
    case 4: *((u32*) dst) = *((u32*) src);
    case 2: *((u16*) dst) = *((u16*) src);
    case 1: *((u8*) dst) = *((u8*) src);
    default: memcpy(dst, src, n);
    }
}

// compute maximum required alignment given size
// 5 -> 4
// 12 -> 8
// 16 -> 16
// 17 -> 16
// and so on
M_INLINE int _align_from_size(int sz) {
    if (sz >= MAX_ALIGN) {
        return MAX_ALIGN;
    }

    const int r = round_up_to_pot(sz);
    return sz < r ? (r >> 1) : r;
}

#ifdef DO_DEBUG_MAP
static void map_check_invariants(const map_t *map) {
    if (!map->data) {
        ASSERT(map->used == 0);
        return;
    }

    const map_entry_t *entries = _map_entries(map);
    const int cap = map_capacity(map);

    // check entry used count == map->used
    int count = 0;
    for (int i = 0; i < cap; i++) {
        if (entries[i].used) { count++; }
    }
    ASSERT(count == map->used, "%d vs. %d", count, map->used);

    // check: distances make sense
    for (int i = 0; i < cap; i++) {
        const map_entry_t *entry = &entries[i];
        if (entry->used && entry->dist != 0) {
            // check that the spot that we *want* to occupy is actually
            // occupied
            const int target_pos = ((i - entry->dist) + cap) % cap;
            ASSERT(entries[target_pos].used);

            // and check that, if it's in the spot *it* wants, we would also
            // end up in that spot
            ASSERT(
                entries[target_pos].dist != 0
                || (entries[target_pos].hash % cap) == (entry->hash % cap));
        }
    }

    // check: all entries are findable
    for (int i = 0; i < _MAP_PRIMES[map->prime]; i++) {
        const map_entry_t *entry = &_map_entries(map)[i];
        if (!entry->used) { continue; }

        const void *key = _map_key_at(map, i);

        const int pos =
            _map_find_pos(
                map,
                key,
                map->key_size);

        ASSERT(pos != -1);
    }
}
#else
// no-op
#define map_check_invariants(...)
#endif // ifdef DO_DEBUG_MAP

void map_init(
        map_t *map,
        allocator_t *allocator,
        int key_size,
        int value_size,
        map_hash_f f_hash,
        map_cmp_f f_keycmp,
        map_free_f f_keyfree,
        map_free_f f_valfree,
        void *userdata) {
    // need to compute the offset of values in each bucket in order to put
    // together a bucket struct at runtime
    //
    // fx. if key_size is 5 and value_size is 7, we need 3 bytes of padding in
    // between - _align_from_size(7) gives 4, and round_up_to_mult(5) will give
    // 8
    int value_offset, bucket_size;

    if (value_size != 0) {
        value_offset = round_up_to_mult(key_size, _align_from_size(value_size));
        bucket_size =
            round_up_to_mult(
                value_offset + value_size,
                max(
                    _align_from_size(key_size),
                    _align_from_size(value_size)));
    } else {
        value_offset = 0;
        bucket_size = key_size;
    }

    *map = (map_t) {
        .allocator = allocator,
        .key_size = key_size,
        .value_size = value_size,
        .bucket_size = bucket_size,
        .value_offset = value_offset,
        .f_hash = f_hash,
        .f_keycmp = f_keycmp,
        .f_keyfree = f_keyfree,
        .f_valfree = f_valfree,
        .userdata = userdata,
    };
}

static void _map_internal_destroy(map_t *map) {
    if (!map->data) {
        return;
    }

    if (map->f_keyfree || map->f_valfree) {
        for (int i = 0, n = map_capacity(map); i < n; i++) {
            if (_map_entries(map)[i].used) {
                if (map->f_keyfree) {
                    map->f_keyfree(map, _map_key_at(map, i));
                }

                if (map->f_valfree && map->value_size) {
                    map->f_valfree(map, _map_value_at(map, i));
                }
            }
        }
    }

    mem_free(map->allocator, map->data);

    map->used = 0;
    map->prime = 0;
    map->data = NULL;
}

void map_destroy(map_t *map) {
    _map_internal_destroy(map);
}

bool map_valid(const map_t *map) {
    return !!map->allocator;
}

enum {
    REHASH_NONE = 0 << 0,
    REHASH_GROW = 1 << 0,
    REHASH_SHRINK = 1 << 1,
    REHASH_FORCE = 1 << 2,
};

// returns true on rehash
static bool map_rehash_or_alloc(map_t *map, int flags) { 
    const int old_capacity = map->data ? map_capacity(map) : 0;

    void *old_data = map->data;
    map_entry_t *old_entries = _map_entries(map);
    void *old_buckets = _map_buckets(map);

    const f32 load =
        old_capacity == 0 ? 0.0f : (map->used / (f32) (old_capacity));

    if (!map->data) {
        map->prime = 0;
    } else if (
        (flags & REHASH_FORCE)
        || (load > MAP_LOAD_HIGH && (flags & REHASH_GROW))) {
        if (map->prime == ARRLEN(_MAP_PRIMES) - 1) {
            return false;
        } else {
            map->prime++;
        }
    } else if (
        (load < MAP_LOAD_LOW && (flags & REHASH_SHRINK))) {
        if (map->prime == 0 || _MAP_PRIMES[map->prime - 1] <= map->used) {
            return false;
        } else {
            map->prime--;
        }
    } else {
        return false;
    }

    DEBUG_MAP(
        "rehashing %d -> %d",
        old_capacity,
        _MAP_PRIMES[map->prime]);

    // recalculated on the fly with inserts
    map->used = 0;

    // size = (entry size * capacity) + (bucket_size * capacity)
    const int
        capacity = _MAP_PRIMES[map->prime],
        size_data =
            round_up_to_mult(capacity * sizeof(map_entry_t), MAX_ALIGN)
                + (capacity * map->bucket_size);

    map->data = mem_alloc(map->allocator, size_data);

    map_entry_t *entries = _map_entries(map);
    for (int i = 0; i < capacity; i++) {
        entries[i].used = false;
    }

    // end here if no data to re-insert
    if (!old_data) { return true; }

    // re-insert all entries
    DEBUG_MAP("rehash inserting...");
    for (int i = 0; i < old_capacity; i++) {
        if (!old_entries[i].used) { continue; }

        // recount used entries
        map->used++;

        const void *old_bucket = old_buckets + (i * map->bucket_size);
        const map_entry_t *old_entry = &old_entries[i];

        int pos = old_entry->hash % capacity, dist = 0;

        while (entries[pos].used) {
            dist++;
            pos = (pos + 1) % capacity;
        }

        DEBUG_MAP(
            "%" PRIhash " at %d (from %d) with distance %d",
              old_entry->hash,
              pos,
              i,
              dist);

        entries[pos] = *old_entry;

        ASSERT_DEBUG(dist < MAP_MAX_DISTANCE);
        entries[pos].dist = dist;

        // copy entire bucket
        _map_memcpy(
            _map_bucket_at(map, pos),
            old_bucket,
            map->bucket_size);
    }

#ifdef DO_DEBUG_MAP
    // check: all old entries are findable after rehash
    for (int i = 0; i < old_capacity; i++) {
        if (!old_entries[i].used) { continue; }
        const void *old_bucket = old_buckets + (i * map->bucket_size);
        //const map_entry_t *old_entry = &old_entries[i];

        const int pos =
            _map_find_pos(
                map,
                old_bucket,
                map->key_size);

        ASSERT(pos != -1);
        if (map->value_size != 0) {
            ASSERT(
                !memcmp(
                    old_bucket + map->value_offset,
                    _map_value_at(map, pos),
                    map->value_size));
        }
    }
#endif // ifdef DO_DEBUG_MAP

    map_check_invariants(map);

    mem_free(map->allocator, old_data);
    return true;
}

int _map_insert(
        map_t *map,
        const void *key,
        int key_size,
        const void *value,
        int value_size,
        bool ignore_if_found) {
    ASSERT(
        key_size == map->key_size,
        "map key size mismatch: expected %d, got %d",
        map->key_size,
        key_size);

    ASSERT(
        value_size == map->value_size,
        "map value size mismatch: expected %d, got %d",
        map->value_size,
        value_size);

    map_check_invariants(map);

    if (M_UNLIKELY(!map->data)) {
        map_rehash_or_alloc(map, REHASH_NONE);
    }

    hash_t hash = map->f_hash(map, key) & MAP_HASH_MASK;

    int pos = hash % map_capacity(map), dist = 0;
    DEBUG_MAP(
        "attempting insert of %" PRIhash ", pos = %d",
        hash,
        pos);

    while (true) {
        map_entry_t *entry = &_map_entries(map)[pos];
        if (!entry->used) {
            map->used++;

            DEBUG_MAP(
                "  inserting %" PRIhash " at %d with distance %d",
                  hash,
                  pos,
                  dist);

            *entry = (map_entry_t) {
                .used = true,
                .dist = dist,
                .hash = hash,
            };

            _map_memcpy(_map_key_at(map, pos), key, map->key_size);
            if (map->value_size) {
                _map_memcpy(_map_value_at(map, pos), value, map->value_size);
            }

            break;
        } else if (
            entry->hash == hash
            && !map->f_keycmp(map, _map_key_at(map, pos), key)) {

            if (ignore_if_found) {
                // OK, found
                return pos;
            }

            // calculated distance ought to be the same
            ASSERT_MAP(dist == entry->dist);

            DEBUG_MAP(
                "  replacing %" PRIhash " at %d (distance %d)",
                  hash,
                  pos,
                  dist);

            // entry already exists, replace value
            if (map->f_keyfree) {
                map->f_keyfree(map, _map_key_at(map, pos));
            }

            if (map->f_valfree && map->value_size) {
                map->f_valfree(map, _map_value_at(map, pos));
            }

            _map_memcpy(_map_key_at(map, pos), key, map->key_size);
            if (map->value_size) {
                _map_memcpy(_map_value_at(map, pos), value, map->value_size);
            }

            break;
        }

        // rehash if we have a greater distance than storage allows
        dist++;

        if (M_UNLIKELY(dist >= MAP_MAX_DISTANCE)) {
            map_rehash_or_alloc(map, REHASH_FORCE);
            _map_insert(map, key, map->key_size, value, map->value_size, false);
            pos = _map_find_pos(map, key, key_size);
            goto done;
        }

        // check next location
        pos = (pos + 1) % map_capacity(map);
    }

    // rehash if necessary
    if (map_rehash_or_alloc(map, REHASH_GROW)) { 
        // recompute pos as it may have moved on rehash
        pos = _map_find_pos(map, key, key_size);
        ASSERT_MAP(pos != -1);
    }

done:
    map_check_invariants(map);
    return pos;
}

int _map_find_pos(const map_t *map, const void *key, int key_size) {
    ASSERT(
        key_size == map->key_size,
        "map key size mismatch: expected %d, got %d",
        map->key_size,
        key_size);

    if (!map->data) { return -1; }

    const hash_t hash = map->f_hash((map_t*) map, key) & MAP_HASH_MASK;

    const int capacity = map_capacity(map);
    int pos = hash % capacity, res = -1;
    const map_entry_t *entries = _map_entries(map);

    while (true) {
        if (!entries[pos].used) {
            break;
        }

        if (entries[pos].hash == hash
            && !map->f_keycmp(
                    (map_t*) map, _map_key_at((map_t*) map, pos), key)) {
            res = pos;
            break;
        }

        pos = (pos + 1) % capacity;
    }

    return res;
}

void _map_remove_at(map_t *map, int pos, int flags) {
    DEBUG_MAP("removing from map at %d", pos);

    ASSERT(
        pos >= 0 && pos < map_capacity(map),
        "could not remove key at pos %" PRIint,
        pos);

    map_entry_t *entries = _map_entries(map);
    map_entry_t *entry = &entries[pos];
    ASSERT(entry->used);

    if (map->f_keyfree) {
        map->f_keyfree(map, _map_key_at(map, pos));
    }

    if (!(flags & _MAP_REMOVE_AT_NO_VALFREE)
        && map->f_valfree
        && map->value_size) {
        map->f_valfree(map, _map_value_at(map, pos));
    }

    map->used--;

    // mark empty
    entry->used = false;

    // might rehash because of low load factor - if we do, don't bother shifting
    // any entries around
    if (!(flags & _MAP_REMOVE_AT_NO_REHASH)
        && map_rehash_or_alloc(map, REHASH_SHRINK)) {
        goto done;
    }

    const int cap = map_capacity(map);
    int dst_pos = pos, scan_pos = (pos + 1) % cap;

    // shift further entries back by one
    while (true) {
        // stop when empty
        // DO NOT STOP when dist is 0!
        //
        // we can end up in a situation where we have cells with
        //  [used = false, dist = ?]
        //  [used = true,  dist = 0]
        //  [used = true,  dist = 2]
        //
        // where the third cell needs to be moved into the position of the first
        // cell so that it can be found properly
        if (!entries[scan_pos].used) {
            DEBUG_MAP(
                "not shifting %d: (used: %s, dist: %d)",
                scan_pos,
                entries[scan_pos].used ? "true" : "false",
                entries[scan_pos].dist);
            break;
        }

        // how many spaces back would we be moving this key?
        int diff;

        if (scan_pos < dst_pos) {
            diff = scan_pos - (dst_pos - cap);
        } else {
            diff = scan_pos - dst_pos;
        }

        if (entries[scan_pos].dist < diff) {
            // keep scanning and DON'T update dist pos - this cell will remain
            // untouched
            goto next;
        }

        DEBUG_MAP("shifting %d -> %d", scan_pos, dst_pos);

        // copy scan_pos into dst_pos, scan_pos becomes new dst_pos
        entries[dst_pos] = entries[scan_pos];
        entries[dst_pos].dist -= diff;

        _map_memcpy(
            _map_bucket_at(map, dst_pos),
            _map_bucket_at(map, scan_pos),
            map->bucket_size);

        // mark scan_pos as empty, start scanning from there to move down
        entries[scan_pos].used = false;

        dst_pos = scan_pos;

next:
        scan_pos = (scan_pos + 1) % cap;
    }

done:
    map_check_invariants(map);
}

bool _map_try_removep(
    map_t *map,
    const void *key,
    int key_size,
    void *out,
    int out_size) {

    ASSERT(
        key_size == map->key_size,
        "map key size mismatch: expected %d, got %d",
        map->key_size,
        key_size);

    ASSERT(
        !out || out_size == map->value_size,
        "map value size (out ptr size) mismatch: expected %d, got %d",
        map->value_size,
        out_size);

    const int pos = _map_find_pos(map, key, map->key_size);
    if (pos != -1) {
        if (out && map->value_size) {
            _map_memcpy(out, _map_value_at(map, pos), map->value_size);
        }

        _map_remove_at(map, pos, _MAP_REMOVE_AT_NO_VALFREE);
    }
    return pos != -1;
}

bool _map_contains(const map_t *map, const void *key, int key_size) {
    return _map_find_pos(map, key, map->key_size) != -1;
}

void map_clear(map_t *map) {
    _map_internal_destroy(map);
}

int map_footprint(const map_t *map) {
    const int cap = map_capacity(map);
    return
        round_up_to_mult(cap * sizeof(map_entry_t), MAX_ALIGN)
            + (cap * map->bucket_size);
}

hash_t map_hash_bytes(map_t *m, const void *p) {
    switch (m->key_size) {
    case 8: return hash_add_u64(0xDEADBEEF, *(u64*) p);
    case 4: return hash_add_u32(0xDEADBEEF, *(u32*) p);
    case 2: return hash_add_u16(0xDEADBEEF, *(u16*) p);
    case 1: return hash_add_u8(0xDEADBEEF, *(u8*) p);
    default: return hash_add_bytes(0xDEADBEEF, p, m->key_size);
    }
}

hash_t map_hash_str(map_t*, const void *p) {
    return hash_add_str(0xDEADBEEF, *(const char**) p);
}

hash_t map_hash_inplace_str(map_t*, const void *p) {
    return hash_add_str(0xDEADBEEF, p);
}

int map_cmp_bytes(map_t *m, const void *p, const void *q) {
    switch (m->key_size) {
    case 8: return *((u64*) p) - *((u64*) q);
    case 4: return *((u32*) p) - *((u32*) q);
    case 2: return *((u16*) p) - *((u16*) q);
    case 1: return *((u8*) p) - *((u8*) q);
    default: return memcmp(p, q, m->key_size);
    }
}

int map_cmp_str(map_t*, const void *p, const void *q) {
    return strcmp(*(const char**) p, *(const char**) q);
}

int map_cmp_inplace_str(map_t *m, const void *p, const void *q) {
    return strncmp(p, q, m->key_size);
}

// default map_free_fn implemented with stdlib's "free()", assumes values are
// pointers into heap memory
void map_default_free(map_t*, void *p) {
    free(*(void**) p);
}

void map_allocator_free(map_t *m, void *p) {
    mem_free(m->allocator, *(void**) p);
}

// free for dynlists
void map_free_dynlist(map_t*, void *p) {
    dynlist_destroy(*(DYNLIST(void)*) p);
}
#endif // ifdef UTIL_IMPL
