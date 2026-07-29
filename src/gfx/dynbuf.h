#pragma once

#include "util/map.h"
#include "util/types.h"
#include "util/dlist.h"

typedef struct dynbuf_region dynbuf_region_t;

typedef struct {
    allocator_t *allocator;

    void *ptr;
    usize capacity;

    // used area, that is, the end of "rightmost" used area
    usize used;

    DLIST(dynbuf_region_t) list;

    // void* -> dynbuf_region_t*
    map_t lookup;
} dynbuf_t;

bool dynbuf_valid(dynbuf_t *buf);

void dynbuf_init(dynbuf_t *buf, allocator_t *allocator, usize capacity);

void dynbuf_destroy(dynbuf_t *buf);

void dynbuf_reset(dynbuf_t *buf);

void *dynbuf_alloc(dynbuf_t *buf, usize n);

void dynbuf_free(dynbuf_t *buf, void *p);
