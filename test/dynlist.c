#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/assert.h"
#include "util/dynlist.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

int main(int argc, char *argv[]) {
    // basics
    {
        int *list = NULL;
        dynlist_init(list, g_mallocator);
        ASSERT(dynlist_size(list) == 0);
        ASSERT(dynlist_capacity(list) > 0);

        *dynlist_push(list) = 4;
        ASSERT(list[0] == 4);
        ASSERT(dynlist_size(list) == 1);
        ASSERT(dynlist_capacity(list) >= 1);

        ASSERT(dynlist_remove(list, 0) == 4);
        ASSERT(dynlist_size(list) == 0);

        *dynlist_push(list) = 4;
        ASSERT(dynlist_size(list) == 1);
        ASSERT(list[0] == 4, "%d", list[0]);
        *dynlist_push(list) = 5;
        ASSERT(dynlist_size(list) == 2);
        ASSERT(list[0] == 4, "%d", list[0]);
        *dynlist_push(list) = 6;
        ASSERT(dynlist_size(list) == 3);

        ASSERT(list[0] == 4);
        ASSERT(list[1] == 5);
        ASSERT(list[2] == 6);

        *dynlist_insert(list, 1) = 12;
        ASSERT(dynlist_size(list) == 4);
        ASSERT(list[0] == 4);
        ASSERT(list[1] == 12);
        ASSERT(list[2] == 5);
        ASSERT(list[3] == 6);

        ASSERT(dynlist_remove(list, 3) == 6);
        ASSERT(dynlist_size(list) == 3);
        ASSERT(list[0] == 4);
        ASSERT(list[1] == 12);
        ASSERT(list[2] == 5);

        ASSERT(dynlist_remove(list, 1) == 12);
        ASSERT(dynlist_size(list) == 2);
        ASSERT(list[0] == 4);
        ASSERT(list[1] == 5);

        *dynlist_prepend(list) = 45;
        ASSERT(list[0] == 45);
        ASSERT(dynlist_size(list) == 3);
        ASSERT(list[0] == 45);
        ASSERT(list[2] == 5);
    }

    // each
    {
        int *list = NULL;
        dynlist_init(list, g_mallocator);
        dynlist_each(list, it) { ASSERT(false); }

        for (int i = 0; i < 256; i++) {
            *dynlist_push(list) = i * i;
        }

        dynlist_each(list, it) {
            ASSERT(*it.el == it.i * it.i);
        }

        for (int i = 0; i < 64; i++) {
            dynlist_remove(list, 0);
        }

        ASSERT(dynlist_size(list) == 256 - 64);

        for (int i = 0; i < 256 - 64; i++) {
            ASSERT(list[i] == (i + 64) * (i + 64));
        }
    }

    // alloc/free
    {
        int *list = NULL;
        dynlist_init(list, g_mallocator);
        ASSERT(list);

        dynlist_destroy(list);
        ASSERT(!list);
    }

    // ensure
    {
        int *list = NULL, *old_list;
        dynlist_init(list, g_mallocator);

        dynlist_reserve(list, 1024);
        old_list = list;

        ASSERT(list);
        ASSERT(dynlist_size(list) == 0);
        ASSERT(dynlist_capacity(list) >= 1024);

        for (int i = 0; i < 1024; i++) {
            *dynlist_push(list) = i << 1;
        }

        ASSERT(list == old_list);
        ASSERT(dynlist_size(list) == 1024);

        dynlist_remove(list, 500);
        dynlist_each(list, it) {
            ASSERT(*it.el != (500 << 1));
        }
    }

    // lists of pointers
    {
        /* const int some_ints[5] = { 1, 2, 3, 4, 5 }; */

        DYNLIST(int*) list = dynlist_create(int*, g_mallocator);
        DYNLIST(int*) *list_ptr = &list;

        dynlist_reserve(*list_ptr, 1024);
        dynlist_reserve(list, 1024);
    }
    return 0;
}
