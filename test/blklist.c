#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#include "util/alloc.h"
#include "util/assert.h"
#include "util/blklist.h"

#define RELOAD_HOST

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

static u64 time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

int main(int argc, char *argv[]) {
    {
        blklist_t b;
        blklist_init(&b, g_mallocator, 32, sizeof(int));

        ASSERT(b.capacity == 0);
        ASSERT(b.size == 0);
        ASSERT(b.block_size == 32);
        ASSERT(b.t_size == sizeof(int));

        // nothing should be iterated
        blklist_each(int, &b, it) {
            ASSERT(false);
        }

        *blklist_add(int, &b) = 10;

        ASSERT(b.capacity == 32);
        ASSERT(b.size == 1);
        blklist_each(int, &b, it) {
            ASSERT(*it.el == 10);
        }

        for (int i = 0; i < 256; i++) {
            *blklist_add(int, &b) = i;
        }

        ASSERT(b.size == 257);

        blklist_remove(&b, 10);

        ASSERT(b.size == 256);

        blklist_each(int, &b, it) {
            if (it.i == 0) { ASSERT(*it.el == 10); }
            else { ASSERT(*it.el == it.i - 1); }
            ASSERT(it.i != 10);
        }

        /* LOG("footprint %d", blklist_footprint(&b)); */

        // try to get an entire block silently deallocated
        for (int i = 32; i < 64; i++) {
            blklist_remove(&b, i);
        }

        ASSERT(b.blocks[0]);
        ASSERT(!b.blocks[1]);
        ASSERT(b.blocks[2]);

        // get last block deallocated
        for (int i = 224; i <= 256; i++) {
            blklist_remove(&b, i);
        }

        ASSERT(b.blocks[0]);
        ASSERT(!b.blocks[1]);
        ASSERT(b.blocks[2]);
        ASSERT(b.capacity == 8 * 32);

        // get first block deallocated
        for (int i = 0; i < 32; i++) {
            if (blklist_present(&b, i)) { blklist_remove(&b, i); }
        }

        ASSERT(!b.blocks[0]);

        LOG("footprint %d", blklist_footprint(&b));
        LOG("overhead %d", blklist_overhead(&b));

        // clear list
        blklist_destroy(&b);
    }

    // test fully contiguous iteration vs normal array
    {
#define SIZE 32767
#define N 1000
        int *arr = mem_alloc(g_mallocator, SIZE * sizeof(int));

        blklist_t list;
        blklist_init(&list, g_mallocator, 128, sizeof(int));

        for (int i = 0; i < SIZE; i++) {
            arr[i] = rand() % 10;
            *blklist_add(int, &list) = arr[i];
        }

        u64 start, total_arr = 0, total_blklist = 0;

        int sum = 0;

        for (int i = 0; i < N; i++) {
            start = time_ns();
            sum = rand();
            for (int j = 0; j < SIZE; j++) {
                if (j == 0 || arr[j] < arr[j - 1]) {
                    sum += arr[j];
                }
            }
            total_arr += time_ns() - start;
        }

        LOG("arr: %d", sum);
        sum = 0;

        for (int i = 0; i < N; i++) {
            start = time_ns();
            sum = rand();
            blklist_each(int, &list, it) {
                if (it.i == 0 || *it.el < *blklist_ptr_unsafe(int, &list, it.i - 1)) {
                    sum += *it.el;
                }
            }
            total_blklist += time_ns() - start;
        }

        LOG("blklist: %d", sum);
        LOG("arr: %.3f ms\nblklist: %.3f ms\n", (total_arr / 1000000.0) / N, (total_blklist / 1000000.0) / N);
        LOG("arr is %.3fx faster", (total_blklist / 1000000.0) / (total_arr / 1000000.0));
        LOG("overhead is %d\n", blklist_footprint(&list) - (SIZE * sizeof(int)));
    }

    return 0;
}
