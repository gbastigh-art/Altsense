#pragma once

#include "util/macros.h"
#include "util/types.h"

#define NS_PER_SECOND 1000000000
#define NS_PER_MS 1000000
#define MS_PER_SECOND 1000

#define ns_to_secs(_ns) ((stime_t)  ((_ns) / ((f64) NS_PER_SECOND)))
#define ns_to_ms(_ns)   ((mstime_t) ((_ns) / ((f64) NS_PER_MS)))
#define secs_to_ns(_s)  ((nstime_t) ((_s)  * ((f64) NS_PER_SECOND)))
#define secs_to_ms(_s)  ((mstime_t) ((_s)  * ((f64) MS_PER_SECOND)))

// time since start in nanoseconds
nstime_t time_ns();

// time since unix epoch
nstime_t time_epoch_ns();

// time since start in seconds
M_INLINE stime_t time_s() {
    return ns_to_secs(time_ns());
}

#ifdef UTIL_IMPL

#include <time.h>
#include "reloadhost.h"

static i64 time_start;
RELOAD_STATIC_GLOBAL(time_start)

#ifdef CLOCK_MONOTONIC_RAW
    #define SYSCLOCK CLOCK_MONOTONIC_RAW
#else
    #define SYSCLOCK CLOCK_MONOTONIC
#endif // ifdef CLOCK_MONOTONIC_RAW

nstime_t time_ns() {
    struct timespec ts;
    clock_gettime(SYSCLOCK, &ts);
    const i64 now = (i64) ((ts.tv_sec * 1000000000ULL) + ts.tv_nsec);

    if (!time_start) {
        time_start = now;
    }

    const i64 diff = now - time_start;
    return diff;
}

nstime_t time_epoch_ns() {
    struct timespec ts;
    clock_gettime(SYSCLOCK, &ts);
    return (ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

#endif // ifdef UTIL_IMPL
