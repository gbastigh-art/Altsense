#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define STB_MALLOC_IMPLEMENTATION
#include "../src/ext/stb_malloc.h"
#undef STB_MALLOC_IMPLEMENTATION

#include "../src/util/alloc.h"
#include "../src/util/log.h"

int main(int argc, char *argv[]) {
    allocator_t heap;
    allocator_stats_t stats = { 0 };
    heap_allocator_init(&heap, g_mallocator, NULL);
    heap.stats = &stats;

    void *p = mem_alloc(&heap, 65536);
    mem_free(&heap, p);
    for (int i = 0; i < 64; i++) {
        p = mem_alloc(&heap, 32);
    }
    LOG("%" PRIusize " / %" PRIusize, (usize) stats.used, (usize) stats.reserved);
    heap_allocator_destroy(&heap);
    return 0;
}
