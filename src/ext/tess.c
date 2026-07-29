#include "ext/tess.h"

static void *tess_memalloc(void *user, unsigned int size) {
    allocator_t *a = user;
    return mem_alloc(a, size);
}

static void tess_memfree(void *user, void *p) {
    allocator_t *a = user;
    mem_free(a, p);
}

void tess_make_allocator(TESSalloc *alloc, allocator_t *allocator) {
    alloc->memalloc = tess_memalloc;
    alloc->memfree = tess_memfree;
    alloc->userData = allocator;
    if (!alloc->meshVertexBucketSize) { alloc->meshVertexBucketSize = 128; }
    if (!alloc->meshFaceBucketSize)   { alloc->meshFaceBucketSize   = 128; }
    if (!alloc->dictNodeBucketSize)   { alloc->dictNodeBucketSize   = 128; }
    if (!alloc->regionBucketSize)     { alloc->regionBucketSize     =  64; }
    if (!alloc->extraVertices)        { alloc->extraVertices        = 128; }
}
