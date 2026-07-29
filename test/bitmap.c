#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/assert.h"
#include "util/bitmap.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

int main(int argc, char *argv[]) {
    {
        BITMAP_STACKALLOC(bits, 256);
        memset(bits->bits, 0xFF, 32);

        int res;
        ASSERT((res = bitmap_find(bits, 0, false)) == -1, "%d", res);
        ASSERT((res = bitmap_rfind(bits, 0, false)) == -1, "%d", res);
    }

    {
        // check that extra bits on the end don't get counted
        BITMAP_STACKALLOC(bits, 11);
        STATIC_ASSERT(BITMAP_SIZE_TO_BYTES(11) == 2);
        bits->bits[0] = 0x00;
        bits->bits[1] = 0xF8;

        int res;
        ASSERT((res = bitmap_count(bits, true)) == 0, "%d", res);
        ASSERT((res = bitmap_find(bits, 0, true)) == -1, "%d", res);
        ASSERT((res = bitmap_count(bits, false)) == 11, "%d", res);
        ASSERT((res = bitmap_find(bits, 0, false)) == 0, "%d", res);
    }

    {
        // check that bitmap_fill_n works with weird sizes
        // check that extra bits on the end don't get counted
        BITMAP_STACKALLOC(bits, 11);
        STATIC_ASSERT(BITMAP_SIZE_TO_BYTES(11) == 2);
        bits->bits[0] = 0x0F;
        bits->bits[1] = 0xFF;

        int res;
        ASSERT((res = bitmap_count(bits, true)) == 7, "%d", res);

        bitmap_fill_n(bits, false, 5);
        ASSERT((res = bitmap_count(bits, true)) == 3, "%d", res);

        // should have two extra bits on the end
        BITMAP_STACKALLOC(big_bits, 2050);
        bitmap_fill(big_bits, true);
        bitmap_fill_n(big_bits, false, 2048);

        // should have two remaining unfilled bits
        ASSERT((res = bitmap_count(big_bits, true)) == 2, "%d", res);

        bitmap_fill(big_bits, true);
        bitmap_fill_n(big_bits, false, 2049);

        // should have one remaining unfilled bit
        ASSERT((res = bitmap_count(big_bits, true)) == 1, "%d", res);

        bitmap_fill_n(big_bits, true, 2049);

        // all should be filled
        ASSERT((res = bitmap_count(big_bits, true)) == 2050, "%d", res);
    }

    {
        BITMAP_STACKALLOC(bits, 3847);

        // check extra bit does not get counted
        bitmap_fill(bits, false);
        bits->bits[BITMAP_SIZE_TO_BYTES(bits->size) - 1] = 0x80;

        for (int i = 0; i < 3847; i++) {
            ASSERT(!bitmap_get(bits, i), "%d", i);
        }

        int res;
        ASSERT((res = bitmap_count(bits, true)) == 0, "%d", res);
        ASSERT((res = bitmap_find(bits, 0, true)) == -1, "%d", res);
        ASSERT((res = bitmap_count(bits, false)) == 3847, "%d", res);
        ASSERT((res = bitmap_find(bits, 0, false)) == 0, "%d", res);

        // test find
        bitmap_set(bits, 475);
        ASSERT((res = bitmap_find(bits, 0, true)) == 475, "%d", res);
        bitmap_set(bits, 470);
        ASSERT((res = bitmap_find(bits, 0, true)) == 470, "%d", res);
        ASSERT((res = bitmap_find(bits, 476, true)) == -1, "%d", res);
        ASSERT((res = bitmap_find(bits, 475, true)) == 475, "%d", res);
        ASSERT((res = bitmap_find(bits, 474, true)) == 475, "%d", res);
        ASSERT((res = bitmap_find(bits, 470, true)) == 470, "%d", res);
        ASSERT((res = bitmap_find(bits, 469, true)) == 470, "%d", res);
        bitmap_clr(bits, 470);
        ASSERT((res = bitmap_find(bits, 0, true)) == 475, "%d", res);

        ASSERT((res = bitmap_find(bits, 0, false)) == 0, "%d", res);
        bitmap_set(bits, 0);
        ASSERT((res = bitmap_find(bits, 0, false)) == 1, "%d", res);
        ASSERT((res = bitmap_find(bits, 0, true)) == 0, "%d", res);

        bitmap_fill(bits, 0);
        bitmap_set(bits, 3846);
        ASSERT((res = bitmap_find(bits, 0, true)) == 3846, "%d", res);

/* #define ITERS 1000000 */
        
/*         { */
/*             i64 acc = 0; */
/*             for (int i = 0; i < ITERS; i++) { */
/*                 const i64 start = time_ns(); */
/*                 bitmap_find(bits, 0, true); */
/*                 acc += time_ns() - start; */
/*             } */

/*             LOG("avg is %.5f ms", (acc / (f64)ITERS) / 1000000.0); */
/*         } */
    }

    return 0;
}
