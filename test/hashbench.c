#include <time.h>

#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define STB_MALLOC_IMPLEMENTATION
#include "../src/ext/stb_malloc.h"
#undef STB_MALLOC_IMPLEMENTATION

#include "../src/util/map.h"

static u64 test_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

static f32 ns_to_ms(u64 ns) {
    return ns / 1000000.0;
}

int main(int argc, char *argv[]) {
    map_t m0;
    typedef struct { int key; int value; } stbds_entry_t;
    stbds_entry_t *m1 = NULL;

    int numbers[10000];
    for (uint i = 0; i < ARRLEN(numbers); i++) {
        numbers[i] = rand();
    }

#define N_TEST 1000

    u64 start, total_m0 = 0, total_m1 = 0;

    for (uint n = 0; n < N_TEST; n++) {
        start = test_time_ns();
        map_init(
            &m0,
            g_mallocator,
            sizeof(int),
            sizeof(int),
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            NULL,
            NULL);
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            const int j = numbers[i] % 10;
            map_insertp(&m0, &numbers[i], &j);

            /* LOG("insert %d (%d -> %d)", i, numbers[i], j); */
            /* ASSERT(map_containsp(&m0, &numbers[i]), "bad insert for key %d (%d)", numbers[i], map_capacity(&m0)); */
            /* const int got = *map_getp(int, &m0, &numbers[i]); */
            /* ASSERT(got == j, "expected %d, got %d for index %d", j, got, i); */
        }
        map_destroy(&m0);
        total_m0 += test_time_ns() - start;
    }
    printf("map_t insert: %.3fms\n", ns_to_ms(total_m0 / N_TEST));

    for (uint n = 0; n < N_TEST; n++) {
        start = test_time_ns();
        m1 = NULL;
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            const int j = numbers[i] % 10;
            stbds_hmput(m1, numbers[i], j);
        }
        stbds_hmfree(m1);
        total_m1 += test_time_ns() - start;
    }
    printf("stbds insert: %.3fms\n", ns_to_ms(total_m1 / N_TEST));

    total_m0 = 0;
    total_m1 = 0;

    for (uint n = 0; n < N_TEST; n++) {
        start = test_time_ns();
        map_init(
            &m0,
            g_mallocator,
            sizeof(int),
            sizeof(int),
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            NULL,
            NULL);
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            const int j = numbers[i] % 10;
            map_insertp(&m0, &numbers[i], &j);

            /* ASSERT(map_containsp(&m0, &numbers[i]), "bad insert for key %d", numbers[i]); */
            /* const int got = *map_getp(int, &m0, &numbers[i]); */
            /* ASSERT(got == j, "expected %d, got %d for index %d", j, got, i); */
        }
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            map_try_remove(&m0, numbers[i]);
        }
        map_destroy(&m0);
        total_m0 += test_time_ns() - start;
    }
    printf("map_t insert/del: %.3fms\n", ns_to_ms(total_m0 / N_TEST));

    for (uint n = 0; n < N_TEST; n++) {
        start = test_time_ns();
        m1 = NULL;
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            const int j = numbers[i] % 10;
            stbds_hmput(m1, numbers[i], j);
        }
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            stbds_hmdel(m1, numbers[i]);
        }
        stbds_hmfree(m1);
        total_m1 += test_time_ns() - start;
    }
    printf("stbds insert/del: %.3fms\n", ns_to_ms(total_m1 / N_TEST));

    total_m0 = 0;
    total_m1 = 0;

    for (uint n = 0; n < N_TEST; n++) {
        start = test_time_ns();
        map_init(
            &m0,
            g_mallocator,
            sizeof(int),
            sizeof(int),
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            NULL,
            NULL);
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            const int j = numbers[i] % 10;
            if (!map_containsp(&m0, &numbers[(i + 1) % ARRLEN(numbers)])) {
                map_insertp(&m0, &numbers[i], &j);
            }
        }
        map_destroy(&m0);
        total_m0 += test_time_ns() - start;
    }
    printf("map_t insert/check: %.3fms\n", ns_to_ms(total_m0 / N_TEST));

    for (uint n = 0; n < N_TEST; n++) {
        start = test_time_ns();
        m1 = NULL;
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            const int j = numbers[i] % 10;
            if (!stbds_hmgetp_null(m1, numbers[(i + 1) % ARRLEN(numbers)])) {
                stbds_hmput(m1, numbers[i], j);
            }
        }
        stbds_hmfree(m1);
        total_m1 += test_time_ns() - start;
    }
    printf("stbds insert/check: %.3fms\n", ns_to_ms(total_m1 / N_TEST));

    total_m0 = 0;
    total_m1 = 0;

    u64 bigsum = 0;

    for (uint n = 0; n < N_TEST; n++) {
        start = test_time_ns();
        bigsum = 0;
        map_init(
            &m0,
            g_mallocator,
            sizeof(int),
            sizeof(int),
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            NULL,
            NULL);
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            const int j = numbers[i] % 10;
            map_insertp(&m0, &numbers[i], &j);
        }

        map_each(int, int, &m0, it) {
            bigsum += *it.value;
            /* printf("%d\n", *it.value); */
            ASSERT(_map_entries(&m0)[it.__i].used);
        }

        map_destroy(&m0);
        total_m0 += test_time_ns() - start;
    }
    printf("map_t iter (%" PRIu64 "): %.3fms\n", bigsum, ns_to_ms(total_m0 / N_TEST));

    bigsum = 0;

    for (uint n = 0; n < N_TEST; n++) {
        start = test_time_ns();
        bigsum = 0;
        m1 = NULL;
        for (uint i = 0; i < ARRLEN(numbers); i++) {
            const int j = numbers[i] % 10;
            stbds_hmput(m1, numbers[i], j);
        }
        for (uint i = 0; i < stbds_hmlen(m1); i++) {
            bigsum += m1[i].value;
        }
        stbds_hmfree(m1);
        total_m1 += test_time_ns() - start;
    }
    printf("stbds iter (%" PRIu64 "): %.3fms\n", bigsum, ns_to_ms(total_m1 / N_TEST));

    return 0;
}
