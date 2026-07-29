#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/assert.h"
#include "util/alloc.h"
#include "util/log.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

int main(int argc, char *argv[]) {
    {
        allocator_t a;
        bump_allocator_init(&a, g_mallocator, 1024);
        int *ptr = a.alloc(&a, sizeof(int));
        ASSERT(ptr);
        ASSERT(ptr == (int*) &a.arena.blocks.head->bytes[0]);

        void *two_k = a.alloc(&a, 2048);
        ASSERT(two_k);

        ASSERT(two_k == &a.arena.blocks.head->bytes[0]);

        bump_allocator_reset(&a, 16384);

        ptr = a.alloc(&a, sizeof(int));
        ASSERT(ptr);
        ASSERT(ptr == (int*) &a.arena.blocks.head->bytes[0]);
        ASSERT(a.arena.blocks.head->size >= 2056);
        ASSERT(a.arena.blocks.head->size <= 16384);

        int *ptr2 = a.alloc(&a, sizeof(int));
        ASSERT(ptr2 >= (ptr + 1));

        bump_allocator_destroy(&a);
    }

    return 0;
}
