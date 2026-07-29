#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/bitmap.h"
#include "util/assert.h"
#include "util/log.h"
#include "util/map.h"
#include "util/time.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

int main(int argc, char *argv[]) {
    {
        // const char* -> const char*
        map_t m;
        map_init(
            &m,
            g_mallocator,
            sizeof(const char*),
            sizeof(const char*),
            map_hash_str,
            map_cmp_str,
            map_default_free,
            map_default_free,
            NULL);

        char *key, *value;
        key = strdup("hello");
        value = strdup("world");
        map_insertp(&m, &key, &value);

        ASSERT(map_size(&m) == 1, "%d", map_size(&m));
        ASSERT(!strcmp(*map_getp(const char*, &m, &key), "world"));

        map_destroy(&m);
    }

    {
        // int -> const char*
        map_t m;
        map_init(
            &m,
            g_mallocator,
            sizeof(int),
            sizeof(const char*),
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            map_default_free,
            NULL);

        int k = 32;
        char *v = strdup("world");
        map_insertp(&m, &k, &v);

        ASSERT(map_size(&m) == 1);
        ASSERT(!strcmp(*map_getp(const char*, &m, &k), "world"));

        v = strdup("not world");
        map_insertp(&m, &k, &v);

        ASSERT(map_size(&m) == 1, "%d", map_size(&m));
        ASSERT(!strcmp(*map_getp(const char*, &m, &k), "not world"));

        k = 1;
        v = strdup("words");
        map_insertp(&m, &k, &v);

        ASSERT(map_size(&m) == 2);

        k = 32;
        ASSERT(!strcmp(*map_getp(const char*, &m, &k), "not world"));

        k = 1;
        ASSERT(!strcmp(*map_getp(const char*, &m, &k), "words"));

        for (int i = 0; i < 16384; i++) {
            k = i;

            char buf[64];
            snprintf(buf, sizeof(buf), "%d", i);
            v = strdup(buf);

            map_insertp(&m, &k, &v);
        }

        ASSERT(map_size(&m) == 16384, "%d", map_size(&m));

        k = 192;
        map_try_removep(&m, &k);

        k = 100;
        map_try_removep(&m, &k);

        k = 10111;
        map_try_removep(&m, &k);

        ASSERT(map_size(&m) == 16381, "%d", map_size(&m));

        BITMAP_STACKALLOC(bits, 16384);
        bitmap_fill(bits, 0);

        map_each(int, const char*, &m, it) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d", *it.key);
            ASSERT(!strcmp(*it.value, buf));

            bitmap_set(bits, *it.key);
        }

        for (int i = 0; i < 16384; i++) {
            if (i == 192 || i == 100 || i == 10111) {
                ASSERT(!bitmap_get(bits, i));
            } else {
                ASSERT(bitmap_get(bits, i));
            }
        }

        map_destroy(&m);
    }

    {
        // int -> int
        map_t m;
        map_init(
            &m,
            g_mallocator,
            sizeof(int),
            sizeof(int),
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            NULL,
            NULL);

        for (int i = 0; i < 50000; i++) {
            map_insertp(&m, &i, &i);
        }

        ASSERT(map_size(&m) == 50000);
        for (int i = 49999; i >= 0; i--) {
            ASSERT(map_containsp(&m, &i));
            map_try_removep(&m, &i);
        }
        ASSERT(map_size(&m) == 0);

        map_destroy(&m);
    }

    {
        // set of ints
        map_t m;
        map_init(
            &m,
            g_mallocator,
            sizeof(int),
            0,
            map_hash_bytes,
            map_cmp_bytes,
            NULL,
            NULL,
            NULL);

        for (int i = 0; i < 50000; i++) {
            map_insertp(&m, &i, NULL);
        }

        ASSERT(map_size(&m) == 50000);
        for (int i = 49999; i >= 0; i--) {
            ASSERT(map_containsp(&m, &i));
            map_try_removep(&m, &i);
        }
        ASSERT(map_size(&m) == 0);

        map_destroy(&m);
    }
    return 0;
}
