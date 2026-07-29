#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/assert.h"
#include "util/log.h"
#include "util/map.h"
#include "util/fixlist.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

int main(int argc, char *argv[]) {
    // basics
    {
        FIXLIST(int, 128) list = { 0 };
        ASSERT(list.n == 0);

        *fixlist_push(list) = 4;
        ASSERT(list.arr[0] == 4);
        ASSERT(list.n == 1);

        ASSERT(fixlist_remove(list, 0) == 4);
        ASSERT(list.n == 0);

        *fixlist_push(list) = 4;
        ASSERT(list.n == 1);
        ASSERT(list.arr[0] == 4);
        *fixlist_push(list) = 5;
        ASSERT(list.n == 2);
        ASSERT(list.arr[0] == 4);
        *fixlist_push(list) = 6;
        ASSERT(list.n == 3);

        ASSERT(list.arr[0] == 4);
        ASSERT(list.arr[1] == 5);
        ASSERT(list.arr[2] == 6);

        *fixlist_insert(list, 1) = 12;
        ASSERT(list.n == 4);
        ASSERT(list.arr[0] == 4);
        ASSERT(list.arr[1] == 12);
        ASSERT(list.arr[2] == 5);
        ASSERT(list.arr[3] == 6);

        ASSERT(fixlist_remove(list, 3) == 6);
        ASSERT(list.n == 3);
        ASSERT(list.arr[0] == 4);
        ASSERT(list.arr[1] == 12);
        ASSERT(list.arr[2] == 5);

        ASSERT(fixlist_remove(list, 1) == 12);
        ASSERT(list.n == 2);
        ASSERT(list.arr[0] == 4);
        ASSERT(list.arr[1] == 5);

        *fixlist_prepend(list) = 45;
        ASSERT(list.arr[0] == 45);
        ASSERT(list.n == 3);
        ASSERT(list.arr[0] == 45);
        ASSERT(list.arr[2] == 5);
    }

    // each
    {
        FIXLIST(int, 256) list = { 0 };
        fixlist_each(list, it) { ASSERT(false); }

        for (int i = 0; i < 256; i++) {
            *fixlist_push(list) = i * i;
        }

        fixlist_each(list, it) {
            ASSERT(*it.el == it.i * it.i);
        }

        for (int i = 0; i < 64; i++) {
            fixlist_remove(list, 0);
        }

        ASSERT(list.n == 256 - 64);

        for (int i = 0; i < 256 - 64; i++) {
            ASSERT(list.arr[i] == (i + 64) * (i + 64));
        }
    }

    // ensure
    {
        FIXLIST(int, 1024) list = { 0 };
        ASSERT(list.n == 0);

        for (int i = 0; i < 1024; i++) {
            *fixlist_push(list) = i << 1;
        }

        ASSERT(list.n == 1024);

        fixlist_remove(list, 500);
        fixlist_each(list, it) {
            ASSERT(*it.el != (500 << 1));
        }
    }
    return 0;
}
