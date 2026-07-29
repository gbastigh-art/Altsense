#include <time.h>

#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define STB_MALLOC_IMPLEMENTATION
#include "../src/ext/stb_malloc.h"
#undef STB_MALLOC_IMPLEMENTATION

#include "../src/util/kvstore.h"

static u64 time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

static f32 ns_to_ms(u64 ns) {
    return ns / 1000000.0;
}

int main(int argc, char *argv[]) {
    allocator_stats_t stats = { 0 };
    tlscratch()->stats = &stats;

    const char *json = "{ \"some\\tthing\": 844.1234e2\t\t\n, 'another'  : false\n\n, 'list': [3, 4, [[], false, {'lmao': false}, [4, {'ayy': 'lmbbbbao'}, null, null, 3, 3, 3, 3,3 , 3, 3, 3, 444444]], 5], }";

    strbuf_t buf = strbuf_create(tlscratch());
    strbuf_ap_fmt(&buf, "{ 'list': [");
    for (uint i = 0; i < 64; i++) {
        strbuf_ap_fmt(&buf, "%s, ", json);
    }
    strbuf_ap_fmt(&buf, "]}");

    const char *str = strbuf_dump(&buf, tlscratch());
    str_view_t view = str_view_from(str);
    LOG("%.3f KiB", str_view_len(&view) / 1024.0f);

    const u64 base = stats.used;
    LOG("base is %" PRIusize, base);

    u64 total = 0;

    allocator_stats_t heap_stats = { 0 };
    allocator_t heap;
    heap_allocator_init(&heap, g_mallocator, NULL);
    heap.stats = &heap_stats;

#define N 1000
    for (uint i = 0; i < N; i++) {
        const u64 start = time_ns();
        kvstore_t kvs;
        kvstore_init(&kvs, &heap);
        if (!kvstore_from_json(&kvs, &view)) {
            ERROR("error!");
        }
        if (i == N - 1) {
            LOG("%s", kvstore_to_json(&kvs, tlscratch()));
            LOG("%.3f KiB used", heap_stats.used / (1024.0));
        }
        kvstore_destroy(&kvs);
        total += (time_ns() - start);
    }

    LOG("%.3f KiB remaining (?)", heap_stats.used / (1024.0));
    heap_allocator_destroy(&heap);

    LOG("%.3f ms", ns_to_ms(total / N));
    LOG("%.3f MiB allocated (%.3f MiB/run)", (stats.used - base) / (1024.0 * 1024.0), ((stats.used - base) / (1024.0 * 1024.0)) / N);

    /* kvstore_each(&kvs, it) { */
    /*     LOG("%s: %s", *it.key, any_type_to_str(it.value->type)); */
    /* } */

    /* const char *out = kvstore_to_json(&kvs, tlscratch()); */
    /* LOG("got: \"%s\"", out); */

    bump_allocator_destroy(tlscratch());
    return 0;
}
