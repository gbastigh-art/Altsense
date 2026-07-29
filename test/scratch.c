#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOAD_HOST

#include "util/assert.h"
#include "util/math.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

int main(int argc, char *argv[]) {
    char buf[64];
    const vec3s v = vec3_of(104.4556, 23457935.2345, 895.333333);
    snprintf(buf, sizeof(buf), "%" PRIv3, FMTv3(v));

    int res;
    vec3s u;
    ASSERT((res = sscanf(buf, "(%f, %f, %f)", &u.x, &u.y, &u.z)), "%d", res);
    return 0;
}
