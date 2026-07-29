#pragma once

#include "util/types.h"
#include "util/macros.h"

typedef int (*sort_cmp_f)(const void*, const void*, void*);

// cross platform reentrant sort
void sort(
    void *base_,
    usize n,
    usize width,
    sort_cmp_f cmp,
    void *arg);

#ifndef sign
    #define sign(_f) ({                                                        \
            typeof(_f) __f = (_f);                                             \
            (__f > 0) - (__f < 0);                                             \
        })
#endif // ifndef sign

#define SIMPLE_SORT_CMP(TYPE)                                         \
    typedef struct { int offset; } sort_cmp_##TYPE##_field_params_t;  \
    M_INLINE int sort_cmp_##TYPE##_field(                             \
            const void *a,                                            \
            const void *b,                                            \
            void *userdata) {                                         \
        const int offset =                                            \
            ((sort_cmp_##TYPE##_field_params_t*) (userdata))->offset; \
        return                                                        \
            sign(                                                     \
                 *((TYPE*) (((u8*) a) + offset))                      \
                    - *((TYPE*) (((u8*) b) + offset)));               \
    }                                                                 \

SIMPLE_SORT_CMP(f32)
SIMPLE_SORT_CMP(f64)
SIMPLE_SORT_CMP(uint)
SIMPLE_SORT_CMP(i8)
SIMPLE_SORT_CMP(i16)
SIMPLE_SORT_CMP(i32)
SIMPLE_SORT_CMP(i64)
SIMPLE_SORT_CMP(int)

#undef SIMPLE_SORT_CMP

#ifdef UTIL_IMPL

#include <stdlib.h>

typedef struct sort_data {
    void *arg;
    int (*cmp)(const void *a, const void *b, void*);
} sort_data_t;

int sort__argswap(void *arg, const void *a, const void *b) {
    sort_data_t *s = (sort_data_t*) arg;
    return s->cmp(a, b, s->arg);
}

void sort(
    void *base,
    usize n,
    usize width,
    sort_cmp_f cmp,
    void *arg) {
#if TARGET_PLATFORM_macos
    sort_data_t data = { .arg = arg, .cmp = cmp };
    qsort_r(base, n, width, &data, sort__argswap);
#elifdef TARGET_PLATFORM_windows
    sort_data_t data = { .arg = arg, .cmp = cmp };
    qsort_s(base, n, width, sort__argswap, &data);
#elifdef TARGET_PLATFORM_linux
    // linux
    qsort_r(base, n, width, cmp, arg);
#else
    #error bad_platform
#endif // ifdef TARGET_PLATFORM_osx
}

#endif // ifdef UTIL_IMPL
