#pragma once

#include <stdarg.h>

#include "util/types.h"

void _log(
    const char *file,
    int line,
    const char *func,
    const char *pre,
    const char *fmt,
    ...);

// debug log message
#define LOG(_fmt, ...)                    \
    _log(                                 \
        __FILE__, __LINE__, __FUNCTION__, \
        "LOG", (_fmt), ##__VA_ARGS__)

#define WARN(_fmt, ...)                   \
    _log(                                 \
        __FILE__, __LINE__, __FUNCTION__, \
        "WRN", (_fmt), ##__VA_ARGS__)

#define ERROR(_fmt, ...)                  \
    _log(                                 \
        __FILE__, __LINE__, __FUNCTION__, \
        "ERR", (_fmt), ##__VA_ARGS__)

#ifdef UTIL_IMPL

#include <stdio.h>
#include "ext/printf.h"

#include "util/time.h"

void _log(
    const char *file,
    int line,
    const char *function,
    const char *prefix,
    const char *fmt,
    ...) {
    va_list ap;
    va_start(ap, fmt);
    FILE *fp = !strcmp(prefix, "LOG") ? stdout : stderr;
    const f32 time_s = time_ns() / 1000000000.0;
    fprintf(fp, "[%s][%.3f][%s:%d][%s] ", prefix, time_s, file, line, function);

    const int len = vsnprintf_(NULL, 0, fmt, ap);
    char buf[len + 1];
    vsnprintf_(buf, len + 1, fmt, ap);
    fprintf(fp, "%s%s", buf, buf[len] == '\n' ? "" : "\n");
    va_end(ap);
}

#endif //ifdef UTIL_IMPL
