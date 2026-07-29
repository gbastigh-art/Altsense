#pragma once

#include "util/llist.h"
#include "util/thread.h"
#include "util/range.h"
#include "ext/printf.h"

// TODO: optional file/line/stack trace/etc. tagging
// TODO: play nice with ASAN

typedef struct allocator allocator_t;

#define mem_alloc mem_alloc_impl
#define mem_alloc_inplace mem_alloc_inplace_impl
#define mem_calloc mem_calloc_impl
#define mem_free mem_free_impl
#define mem_free_range mem_free_range_impl

void *mem_alloc_impl(allocator_t *a, usize n);
void *mem_alloc_inplace_impl(allocator_t *a, usize n, const void *data);
void *mem_calloc_impl(allocator_t *a, usize n);
void mem_free_impl(allocator_t *a, void *ptr);
void mem_free_range_impl(allocator_t *a, const range_t *r);

// see ext/stb_malloc.h
typedef struct stbm_heap stbm_heap;

typedef void *(*alloc_fn)(allocator_t*, int n);
typedef void (*free_fn)(allocator_t*, void *p);

// duplicate string onto allocator
char *mem_strdup(allocator_t *a, const char *str);

// format string onto allocator
char *mem_vstrfmt(allocator_t *a, const char *fmt, va_list ap);

// format string onto allocator
char *mem_strfmt(allocator_t *a, const char *fmt, ...);

// format a string and concatenate it onto another
// str can be NULL
char *mem_strfcat(allocator_t *a, const char *str, const char *fmt, ...);

// allocate to range
range_t mem_alloc_range(allocator_t *a, size_t n);

// allocate + clear to range
range_t mem_calloc_range(allocator_t *a, size_t n);

typedef struct allocator_stats {
    // currently used
    usize used;

    // total memory reserved from parent allocator
    usize reserved;

    // peak of reserved
    usize peak;
} allocator_stats_t;

typedef struct bump_allocator_block {
    // size (not including header) + bytes used
    int size, used;

    // linked list node
    LLIST_NODE(struct bump_allocator_block) node;

    // array extending off of the end of the block, of
    // (size - sizeof(size) - sizeof(node)) bytes
    u8 bytes[];
} bump_allocator_block_t;

STATIC_ASSERT(sizeof(bump_allocator_block_t) % MAX_ALIGN == 0);

// saved bump allocator state
typedef struct bump_allocator_state {
    // head, NULL if no block was head
    bump_allocator_block_t *head;

    // bytes used in head block at time of state save
    int block_used;

    // total bytes used (for stats) at time of save state
    int total_used;
} bump_allocator_state_t;

typedef struct allocator {
    alloc_fn alloc;
    free_fn free;

    // (optional) mutex
    mtx_t *mutex;

    // if enabled, memory allocations functions will double check that they are
    // being called from the lock thread
    struct {
        bool enabled;
        thrd_t thread;
    } lock_thread;

    // (optional) stats to record
    // must be bound at allocator creation and NOT CHANGED for the lifetime of
    // the allocator
    allocator_stats_t *stats;

    union {
        struct {
            allocator_t *parent;
            int min_block_size, largest, allocated;
            LLIST(bump_allocator_block_t) blocks;
        } bump;

        struct {
            allocator_t *parent;

            // initial storage block for heap data
            void *storage;

            // stbm_heap sitting on top of parent
            stbm_heap *heap;
        } heap;

        struct {
            void *storage;
            int used, size;
        } ezbump;
    };
} allocator_t;

#define STACK_ALLOCATOR_IMPL(_name, _size, _storage)                      \
    allocator_t _name;                                                    \
    u8 _storage[(_size)];                                                 \
    ezbump_allocator_init(&_name, &_storage[0], (_size), NULL)

#define STACK_ALLOCATOR(_name, _size)                                     \
    STACK_ALLOCATOR_IMPL(_name, _size, CONCAT(_s, __COUNTER__))

// global mallocator
extern allocator_t *g_mallocator;

// thread local scratch allocator
// REMEMBER TO CLEAR IF USED!
allocator_t *tlscratch();

allocator_stats_t global_malloc_stats();

bool allocator_valid(allocator_t *a);

void bump_allocator_init(
    allocator_t *a,
    allocator_t *parent,
    int min_block_size,
    allocator_stats_t *stats);

void bump_allocator_destroy(allocator_t *a);

void bump_allocator_reset(allocator_t *a, int cap);

void heap_allocator_init(
    allocator_t *a,
    allocator_t *parent,
    allocator_stats_t *stats);

void heap_allocator_destroy(allocator_t *a);

void ezbump_allocator_init(
    allocator_t *a,
    void *storage,
    int size,
    allocator_stats_t *stats);

#ifdef UTIL_IMPL
#include "ext/stb_malloc.h"
#include "util/macros.h"
#include "util/thread.h"
#include "util/math/util.h"
#include "util/assert.h"

#include <stdarg.h>
#include "reloadhost.h"

#ifdef TARGET_PLATFORM_macos
#include <malloc/malloc.h>
#endif // TODO: other platforms

#define ASSERT_THREAD_LOCK(_a) do {                                       \
        if ((_a)->lock_thread.enabled) {                                  \
            ASSERT(                                                       \
                thrd_current() == (_a)->lock_thread.thread,               \
                "!!!! THREAD-LOCKED ALLOCATOR USED ACROSS THREADS !!!");  \
        }                                                                 \
    } while (0)

void *mem_alloc_impl(allocator_t *a, usize n) {
    if (a->mutex) { ASSERT(mtx_lock(a->mutex) == thrd_success); }
    void *p = a->alloc(a, n);
    if (a->mutex) { ASSERT(mtx_unlock(a->mutex) == thrd_success); }
    ASSERT(p, "allocation failure (size %" PRIusize ")", n);
    return p;
}

void *mem_alloc_inplace_impl(allocator_t *a, usize n, const void *data) {
    void *p = a->alloc(a, n);
    memcpy(p, data, n);
    return p;
}

void *mem_calloc_impl(allocator_t *a, usize n) {
    void *p = a->alloc(a, n);
    memset(p, 0, n);
    return p;
}

void mem_free_impl(allocator_t *a, void *ptr) {
    // mem_free(NULL) is a no-op
    if (!ptr) { return; }

    if (a->mutex) { ASSERT(mtx_lock(a->mutex) == thrd_success); }
    a->free(a, ptr);
    if (a->mutex) { ASSERT(mtx_unlock(a->mutex) == thrd_success); }
}

void mem_free_range_impl(allocator_t *a, const range_t *r) {
    mem_free(a, r->ptr);
}

// run atexit on non-reloadable builds
void tlscratch_cleanup(void) {
    bump_allocator_destroy(tlscratch());
}

allocator_t *tlscratch() {
    static thread_local allocator_t allocator = { 0 };
    static thread_local allocator_stats_t stats = { 0 };
    static thread_local bool lazy = false;

    if (!lazy) {
        lazy = true;
        RELOAD_STATIC_THREAD_LOCAL_VAR(allocator);
    }

    if (!allocator_valid(&allocator)) {
        // allocate at 1 MiB/block
        bump_allocator_init(&allocator, g_mallocator, 1 * 1024 * 1024, &stats);
        allocator.lock_thread.enabled = true;
        allocator.lock_thread.thread = thrd_current();

#ifndef RELOADHOST_CLIENT_ENABLED
        // cleanup atexit
        ASSERT(!atexit(tlscratch_cleanup));
#endif // ifndef RELOADHOST_CLIENT_ENABLED
    }

    return &allocator;
}

allocator_stats_t global_malloc_stats() {
    allocator_stats_t stats = { 0 };

#ifdef TARGET_PLATFORM_macos
    malloc_zone_t **zones = NULL;
    unsigned int num_zones = 0;

    if (malloc_get_all_zones(0, NULL, (vm_address_t**) &zones, &num_zones)
            != KERN_SUCCESS) {
        num_zones = 0;
    }

    int used = 0, reserved = 0;
    for (int i = 0; i < (int) num_zones; i++) {
        malloc_zone_t *zone = zones[i];
        if (!zone || !zone->introspect || !zone->introspect->statistics) {
            continue;
        }

        malloc_statistics_t ms = { 0 };
        zone->introspect->statistics(zone, &ms);

        used += ms.size_in_use;
        reserved += ms.size_allocated;
    }

    stats.used = used;
    stats.peak = max(stats.peak, reserved);
    stats.reserved = reserved;
#endif // TODO: other platforms

    return stats;
}

bool allocator_valid(allocator_t *a) {
    return !!a->alloc;
}

static void *_mallocator_alloc(allocator_t *a, int n) {
    ASSERT_THREAD_LOCK(a);

    void *p = malloc(n);

    if (a->stats) {
#ifdef TARGET_PLATFORM_macos
        const size_t m = malloc_size(p);
        a->stats->used += m;
        a->stats->reserved += m;
        a->stats->peak = max(a->stats->peak, a->stats->used);
#endif // TODO: other platforms
    }

    return p;
}

static void _mallocator_free(allocator_t *a, void *p) {
    if (!p) { return; }

    ASSERT_THREAD_LOCK(a);

    if (a->stats) {
#ifdef TARGET_PLATFORM_macos
        const size_t n = malloc_size(p);
        a->stats->used -= n;
        a->stats->reserved -= n;
#endif // TODO: other platforms
    }

    free(p);
}

static allocator_stats_t mallocator_stats = { 0 };
RELOAD_STATIC_GLOBAL(mallocator_stats)

static allocator_t mallocator = {
    .alloc = _mallocator_alloc,
    .free = _mallocator_free,
    .stats = &mallocator_stats,
};
RELOAD_STATIC_GLOBAL(mallocator)
ON_RELOAD({ mallocator.stats = &mallocator_stats; })

// global mallocator
allocator_t *g_mallocator = &mallocator;

static void *_bump_allocator_alloc(allocator_t *a, int n) {
    ASSERT_THREAD_LOCK(a);
    n = round_up_to_mult(n, MAX_ALIGN);

    // look in last block (first block since we prepend to list) for free space
    bump_allocator_block_t *block = a->bump.blocks.head;

    if (!block || block->used + n > block->size) {
        // allocate head block/super block of largest allocated size
        const int size = max(a->bump.largest, n);

        block =
            mem_alloc(
                a->bump.parent,
                size + sizeof(bump_allocator_block_t));
        block->size = size;
        block->used = 0;
        llist_init_node(&block->node);
        llist_prepend(node, &a->bump.blocks, block);

        if (a->stats) {
            a->stats->reserved += size + sizeof(bump_allocator_block_t);
        }
    }

    void *q = &block->bytes[block->used];
    block->used += n;
    a->bump.allocated += n;

    if (a->stats) {
        a->stats->used += n;
        a->stats->peak = max(a->stats->peak, a->stats->used);
    }

    return q;
}

static void _bump_allocator_free(allocator_t *a, void*) {
    ASSERT_THREAD_LOCK(a);
    /* no-op */
}

void bump_allocator_init(
    allocator_t *a,
    allocator_t *parent,
    int min_block_size,
    allocator_stats_t *stats) {
    *a = (allocator_t) {
        .alloc = _bump_allocator_alloc,
        .free = _bump_allocator_free,
        .bump = {
            .parent = parent,
            .blocks = { NULL },
            .min_block_size = min_block_size,
            .largest = min_block_size,
            .allocated = 0,
        },
        .stats = stats,
    };
}

void bump_allocator_destroy(allocator_t *a) {
    bump_allocator_block_t *block = a->bump.blocks.head;
    while (block) {
        bump_allocator_block_t *next = block->node.next;
        a->bump.parent->free(a->bump.parent, block);
        block = next;
    }

    a->bump.blocks.head = NULL;
    *a = (allocator_t) { 0 };
}

void bump_allocator_reset(allocator_t *a, int cap) {
    ASSERT_THREAD_LOCK(a);

    // nothing to do
    if (!a->bump.blocks.head) { return; }

    int n = 0;
    llist_each(node, &a->bump.blocks, it) {
        // reset
        it.el->used = sizeof(bump_allocator_block_t);
        n++;
    }

    // nothing to do if one block still
    if (n == 1) {
        goto done;
    }

    // if multiple blocks, need to coalesce into a mega block - update largest
    // allocation size and deallocate all existing blocks
    a->bump.largest = min(max(a->bump.largest, a->bump.allocated), cap);

    bump_allocator_block_t *block = a->bump.blocks.head;
    while (block) {
        bump_allocator_block_t *next = block->node.next;
        mem_free(a->bump.parent, block);
        block = next;
    }

    a->bump.blocks.head = NULL;

done:
    if (a->stats) {
        a->stats->reserved = 0;
        a->stats->used = 0;
    }

    a->bump.allocated = 0;
}

static void *_heap_allocator_system_alloc(
    void *userdata, size_t req, size_t *provided) {
    allocator_t *child = userdata, *parent = child->heap.parent;
    *provided = req;

    void *p;

    if (child->stats) {
        child->stats->reserved += MAX_ALIGN + req;

        // store allocation size in first MAX_ALIGN bytes
        STATIC_ASSERT(MAX_ALIGN >= sizeof(int));
        p = mem_alloc(parent, MAX_ALIGN + req);
        *((int*) p) = MAX_ALIGN + req;
        p = ((u8*) p) + MAX_ALIGN;
    } else {
        p = mem_alloc(parent, req);
    }

    return p;
}

static void _heap_allocator_system_free(void *userdata, void *p) {
    allocator_t *child = userdata, *parent = child->heap.parent;

    if (child->stats) {
        const int size = *((int*) (((u8*) p) - MAX_ALIGN));
        child->stats->reserved -= size;
        mem_free(parent, ((u8*) p) - MAX_ALIGN);
    } else {
        mem_free(parent, p);
    }
}

static void *_heap_allocator_alloc(allocator_t *a, int n) {
    ASSERT_THREAD_LOCK(a);

    if (!a->heap.heap) {
        stbm_heap_config config = { 0 };
        config.system_alloc = _heap_allocator_system_alloc;
        config.system_free = _heap_allocator_system_free;
        config.user_context = a;
        a->heap.storage = mem_alloc(a->heap.parent, STBM_HEAP_SIZEOF);
        a->heap.heap =
            stbm_heap_init(
                a->heap.storage,
                STBM_HEAP_SIZEOF,
                &config);

        // configure heap
        //
        // * 32k medium allocations (handles anything > sizeof(void*))
        // * 4k small allocations (for <= sizeof(void*))
        //
        // we set the medium allocation default lower (default is 256 KiB) since
        // I have a tendency to create a bunch of smaller heaps for easier
        // memory management :)
        stbm_heapconfig_set_medium_chunk_size(
            a->heap.heap,
            32 * (1 << 10),
            1024 * (1 << 10));

        stbm_heapconfig_set_small_chunk_size(a->heap.heap, 4 * (1 << 10));
    }

    void *p = stbm_alloc(NULL, a->heap.heap, n, 0);

    if (a->stats) {
        a->stats->used = stbm_heap_outstanding(a->heap.heap);
        a->stats->peak = max(a->stats->used, a->stats->peak);
    }

    return p;
}

static void _heap_allocator_free(allocator_t *a, void *p) {
    if (!p) { return; }

    ASSERT_THREAD_LOCK(a);

    stbm_free(NULL, a->heap.heap, p);

    if (a->stats) {
        a->stats->used = stbm_heap_outstanding(a->heap.heap);
    }
}

void heap_allocator_init(
    allocator_t *a,
    allocator_t *parent,
    allocator_stats_t *stats) {
    *a = (allocator_t) {
        .alloc = _heap_allocator_alloc,
        .free = _heap_allocator_free,
        .heap = {
            .parent = parent,
            .storage = NULL,
            .heap = NULL
        },
        .stats = stats,
    };
}

void heap_allocator_destroy(allocator_t *a) {
    if (a->heap.heap) {
        stbm_heap_free(a->heap.heap);
        mem_free(a->heap.parent, a->heap.storage);
    }
    *a = (allocator_t) { 0 };
}

static void *_ezbump_alloc(allocator_t *a, int n) {
    ASSERT_THREAD_LOCK(a);
    n = round_up_to_mult(n, MAX_ALIGN);

    if (a->ezbump.used + n > a->ezbump.size) {
        WARN("ezbump @ %p (size %d) allocation failure", a, a->ezbump.size);
        return NULL;
    }

    void *p = a->ezbump.storage + a->ezbump.used;
    a->ezbump.used += n;

    if (a->stats) {
        a->stats->reserved = a->ezbump.size;
        a->stats->used += n;
        a->stats->peak += n;
    }

    return p;
}

static void _ezbump_free(allocator_t *a, void *p) {
    ASSERT_THREAD_LOCK(a);
    /* no-op */
}

void ezbump_allocator_init(
    allocator_t *a,
    void *storage,
    int size,
    allocator_stats_t *stats) {
    *a = (allocator_t) {
        .alloc = _ezbump_alloc,
        .free = _ezbump_free,
        .ezbump = {
            .storage = storage,
            .used = 0,
            .size = size
        },
        .stats = stats,
    };
}

char *mem_strdup(allocator_t *a, const char *str) {
    ASSERT(str);
    return mem_alloc_inplace(a, strlen(str) + 1, str);
}

char *mem_vstrfmt(allocator_t *a, const char *fmt, va_list ap) {
    // try to vsnprintf to a small buffer first
    static thread_local char buf[1024];
    const int len = vsnprintf_(buf, sizeof(buf), fmt, ap);
    if (len < 0) {
        // error
        return mem_strdup(a, "(mem_vstrfmt failure)");
    } else if (len < (int) sizeof(buf)) {
        // OK, copy from buf
        return mem_alloc_inplace(a, len + 1, buf);
    }

    // need to write into a len-sized buf
    char *res = mem_alloc(a, len + 1);
    const int len2 = vsnprintf_(res, len + 1, fmt, ap);
    if (len2 < 0 || len2 >= len + 1) {
        // error
        mem_free(a, res);
        return mem_strdup(a, "(mem_vstrfmt failure)");
    }

    return res;
}

char *mem_strfmt(allocator_t *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *out = mem_vstrfmt(a, fmt, ap);
    va_end(ap);
    return out;
}

char *mem_vstrfcat(allocator_t *a, const char *str, const char *fmt, va_list ap) {
    usize sz;
    if ((sz = vsnprintf_(NULL, 0, fmt, ap)) < 0) {
        return mem_strdup(a, "(mem_strfcat failure)");
    }

    const usize len = str ? strlen(str) : 0;

    char *out = mem_alloc(a, len + sz + 1);

    if (str) {
        // copy incl. null terminator
        memcpy(out, str, len + 1);
    }

    // print after copy
    ASSERT(vsnprintf_(&out[len], sz + 1, fmt, ap) >= 0);
    return out;
}

char *mem_strfcat(allocator_t *a, const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *res = mem_vstrfcat(a, str, fmt, ap);
    va_end(ap);
    return res;
}

range_t mem_alloc_range(allocator_t *a, size_t n) {
    void *p = mem_alloc(a, n);
    return p ? (range_t) { p, n } : (range_t) { 0 };
}

range_t mem_calloc_range(allocator_t *a, size_t n) {
    void *p = mem_calloc(a, n);
    return p ? (range_t) { p, n } : (range_t) { 0 };
}

#endif // ifdef UTIL_IMPL
