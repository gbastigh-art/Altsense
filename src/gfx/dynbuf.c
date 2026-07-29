#include "gfx/dynbuf.h"

// optimization opportunity:
// allocations should start looking from the last known free node (rover)

// #define DO_DEBUG_DYNBUFS

#ifdef DO_DEBUG_DYNBUFS
#define DEBUG_DYNBUFS(...) LOG(__VA_ARGS__)
#else
#define DEBUG_DYNBUFS(...)
#endif // ifdef DO_DEBUG_DYNBUFS

typedef struct dynbuf_region {
    void *ptr;
    u32 size;
    bool free;
    DLIST_NODE(struct dynbuf_region) node;
} dynbuf_region_t;

#ifdef DO_DEBUG_DYNBUFS
static void dynbuf_check(dynbuf_t *buf) {
    dlist_each(node, &buf->list, it) {
        if (!it.el->free) {
            dynbuf_region_t **pslot =
                map_getp(dynbuf_region_t*, &buf->lookup, &it.el->ptr);
            ASSERT(
                pslot,
                "%p: found region %p (%p / size %d/ free? %s) in list, but not lookup?",
                buf, it.el, it.el->ptr, it.el->size, it.el->free ? "true" : "false");
        }

        ASSERT(
            !it.el->node.next
            || (it.el->ptr + it.el->size == it.el->node.next->ptr),
            "%p: region end mismatch");
    }
}
#else
#define dynbuf_check(...)
#endif // ifdef DO_DEBUG_DYNBUFS

bool dynbuf_valid(dynbuf_t *buf) {
    return buf && buf->allocator && buf->ptr && buf->capacity;
}

void dynbuf_init(dynbuf_t *buf, allocator_t *allocator, usize capacity) {
    DEBUG_DYNBUFS("initializing buf @ %p (cap %d)", buf, capacity);

    *buf = (dynbuf_t) {
        .allocator = allocator,
        .ptr = mem_alloc(allocator, capacity),
        .capacity = capacity,
    };

    map_init(
        &buf->lookup,
        allocator,
        sizeof(void*),
        sizeof(dynbuf_region_t*),
        map_hash_bytes,
        map_cmp_bytes,
        NULL, NULL, NULL);

    dlist_init(&buf->list);

    dynbuf_region_t *base = mem_alloc(buf->allocator, sizeof(*base));
    *base = (dynbuf_region_t) {
        .ptr = buf->ptr,
        .size = capacity,
        .free = true
    };
    dlist_prepend(node, &buf->list, base);
}

void dynbuf_destroy(dynbuf_t *buf) {
    DEBUG_DYNBUFS("destroying buf @ %p", buf);
    mem_free(buf->allocator, buf->ptr);

    dlist_each(node, &buf->list, it) {
        dlist_remove(node, &buf->list, it.el);
        mem_free(buf->allocator, it.el);
    }

    map_destroy(&buf->lookup);
}

void dynbuf_reset(dynbuf_t *buf) {
    DEBUG_DYNBUFS("resetting buf @ %p", buf);

    dlist_each(node, &buf->list, it) {
        dlist_remove(node, &buf->list, it.el);
        mem_free(buf->allocator, it.el);
    }
    map_clear(&buf->lookup);

    dlist_init(&buf->list);
    dynbuf_region_t *base = mem_alloc(buf->allocator, sizeof(*base));
    *base = (dynbuf_region_t) {
        .ptr = buf->ptr,
        .size = buf->capacity,
        .free = true
    };
    dlist_prepend(node, &buf->list, base);

    dynbuf_check(buf);
}

void *dynbuf_alloc(dynbuf_t *buf, usize n) {
    if (n == 0) {
        WARN("dynbuf @ %p got 0 byte alloc", buf);
        return NULL;
    }

    n = round_up_to_mult(n, max(alignof(max_align_t), MAX_ALIGN));

    DEBUG_DYNBUFS("allocating %p: %" PRIusize, buf, n);

    // search for a large enough space
    dlist_each(node, &buf->list, it) {
        if (!it.el->free || it.el->size < n) { continue; }

        if (it.el->size > n) {
            // add new region on tail
            dynbuf_region_t *region =
                mem_alloc(buf->allocator, sizeof(dynbuf_region_t));
            *region = (dynbuf_region_t) {
                .ptr = it.el->ptr + n,
                .size = it.el->size - n,
                .free = true
            };
            DEBUG_DYNBUFS("%p: new node %p (%d)", buf, region, it.el->size - n);

            dlist_insert_after(node, &buf->list, it.el, region);
        } else if (it.el->size == n) {
            DEBUG_DYNBUFS("found exact!");
        }

        it.el->size = n;
        it.el->free = false;
        map_insertp(&buf->lookup, &it.el->ptr, &it.el);
        DEBUG_DYNBUFS(
            "%p: allocating node @ %p (region %p) (size %d)",
            buf, it.el, it.el->ptr, it.el->size);

        buf->used =
            buf->list.tail->node.prev ?
                (buf->list.tail->ptr - buf->ptr)
                : 0;

        dynbuf_check(buf);
        return it.el->ptr;
    }

    WARN(
        "dynbuf @ %p: failed to allocate %" PRIusize " bytes",
         buf,
         n);
    return NULL;
}

void dynbuf_free(dynbuf_t *buf, void *p) {
    DEBUG_DYNBUFS("freeing region %p in buf @ %p", p, buf);

    // ensure region exists
    dynbuf_region_t **pslot = map_getp(dynbuf_region_t*, &buf->lookup, &p);
    
    if (!pslot) {
        // check if region is in list?
        dlist_each(node, &buf->list, it) {
            if (it.el->ptr == p) {
                ERROR(
                    "%p: found region %p (%p / size %d/ free? %s) in list, but not lookup?",
                    buf, it.el, it.el->ptr, it.el->size, it.el->free ? "true" : "false");
            }
        }
    }

    ASSERT(pslot, "dynbuf @ %p: invalid free %p", buf, p);

    // ensure region is in list
    dynbuf_region_t *region = *pslot;
    ASSERT(
        region == buf->list.head
        || region == buf->list.tail
        || region->node.prev
        || region->node.next,
        "dynbuf @ %p: region %p not in list",
        buf,
        region);

    dynbuf_check(buf);

    dynbuf_region_t *out;
    ASSERT(map_try_removep(&buf->lookup, &p, &out));
    ASSERT(out == region);
    region->free = true;

    // ASSERT(map_try_removep(&buf->lookup, &p));
    dynbuf_check(buf);

    dynbuf_region_t
        *left = region->node.prev,
        *right = region->node.next;

    // left coalesce
    if (left && left->free) {
        if (left->node.prev) {
            left->node.prev->node.next = region;
        } else if (left == buf->list.head) {
            buf->list.head = region;
        }

        region->node.prev = left->node.prev;
        region->ptr = left->ptr;
        region->size += left->size;
        ASSERT(!map_containsp(&buf->lookup, &left));
        DEBUG_DYNBUFS("%p: REMOVE (left) node %p", buf, left);
        mem_free(buf->allocator, left);
    }

    // right coalesce
    if (right && right->free) {
        if (right->node.next) {
            right->node.next->node.prev = region;
        } else if (right == buf->list.tail) {
            buf->list.tail = region;
        }

        region->node.next = right->node.next;
        region->size += right->size;
        ASSERT(!map_containsp(&buf->lookup, &right));
        mem_free(buf->allocator, right);
        DEBUG_DYNBUFS("%p: REMOVE (right) node %p", buf, right);
    }

    DEBUG_DYNBUFS("%d used / %d capacity", buf->used, buf->capacity);
    buf->used =
        buf->list.tail->node.prev ?
            (buf->list.tail->ptr - buf->ptr)
            : 0;
    dynbuf_check(buf);
}
