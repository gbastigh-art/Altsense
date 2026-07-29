#pragma once

#include <stdlib.h>
#include <stdio.h>

#include "util/macros.h" /* IWYU pragma: keep */
#include "util/log.h"    /* IWYU pragma: keep */

// assert _e, otherwise print _f with fmt __VA_ARGS__
#define _ASSERT_IMPL(_e, _expr, _fmt, ...)   \
    if (M_UNLIKELY(!(_e))) {                 \
        ERROR("(assertion expr %s)", _expr); \
        ERROR((_fmt), ##__VA_ARGS__);        \
        dumptrace(stderr);                   \
        abort();                             \
    }                                        \

#define _ASSERT1(_e)      _ASSERT_IMPL(_e, #_e, "%s", "assertion failed")
#define _ASSERT2(_e, ...) _ASSERT_IMPL(_e, #_e, __VA_ARGS__)
#define _ASSERT3(_e, ...) _ASSERT_IMPL(_e, #_e, __VA_ARGS__)
#define _ASSERT4(_e, ...) _ASSERT_IMPL(_e, #_e, __VA_ARGS__)
#define _ASSERT5(_e, ...) _ASSERT_IMPL(_e, #_e, __VA_ARGS__)
#define _ASSERT6(_e, ...) _ASSERT_IMPL(_e, #_e, __VA_ARGS__)
#define _ASSERT7(_e, ...) _ASSERT_IMPL(_e, #_e, __VA_ARGS__)
#define _ASSERT8(_e, ...) _ASSERT_IMPL(_e, #_e, __VA_ARGS__)
#define ASSERT(...)       VMACRO(_ASSERT, __VA_ARGS__)

// use ASSERT_DEBUG for debug-only asserts
#ifdef TARGET_DEBUG
    #define ASSERT_DEBUG(...) ASSERT(__VA_ARGS__)
#else
    #define ASSERT_DEBUG(...)
#endif // ifdef TARGET_DEBUG

// dumps a stacktrace to specified file
void dumptrace(FILE *f);

#ifdef UTIL_IMPL

#ifdef __GNUC__
#include <unistd.h>
#include <execinfo.h>

void dumptrace(FILE *f) {
    void *callstack[128];
    const int frames = backtrace(callstack, 128);
    char **strs = backtrace_symbols(callstack, frames);

    for (int i = 0; i < frames; i++) {
        fprintf(f, "%s\n", strs[i]);
    }

    free(strs);
}
#else
void dumptrace(FILE *f) {
    fprintf(f, "(no stack trace on this platform)\n");
}
#endif // ifdef __GNUC__

#endif // ifdef UTIL_IMPL
