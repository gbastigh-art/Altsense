#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/assert.h"
#include "util/alloc.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

int main(int argc, char *argv[]) {
    {
        allocator_t a;
        heap_allocator_init(&a, g_mallocator, NULL);
        int *ptr = a.alloc(&a, sizeof(int));
        ASSERT(ptr);

        void *two_k = a.alloc(&a, 2048);
        ASSERT(two_k);

        ptr = a.alloc(&a, sizeof(int));
        ASSERT(ptr);

        int *ptr2 = a.alloc(&a, sizeof(int));
        int *ptr3 = a.alloc(&a, 8 * 1024 * 1024);
        ASSERT(ptr2);
        ASSERT(ptr3);
        heap_allocator_destroy(&a);
    }

    return 0;
}
