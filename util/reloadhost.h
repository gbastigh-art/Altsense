#pragma once

#include <stdatomic.h>

#include "../src/util/types.h"
#include "../src/util/thread.h" /* IWYU pragma: keep */

// operations for f_rh_entry
// RELOADHOST_INIT: client should initialize
// RELOADHOST_DEINIT: client should close
// RELOADHOST_STEP: client step (loop operation)
// RELOADHOST_PRE_RELOAD: client is about to be reloaded
// RELOADHOST_RELOAD: client has been reloaded, but no function pointers or
//                    variables have been updated
// RELOADHOST_POST_RELOAD: client is about to be reloaded AND all pointers,
//                         variables, etc. have been reloaded
typedef enum {
    RELOADHOST_INIT,
    RELOADHOST_DEINIT,
    RELOADHOST_STEP,
    RELOADHOST_PRE_RELOAD,
    RELOADHOST_RELOAD,
    RELOADHOST_POST_RELOAD,
} reloadhost_op_e;

// f_rh_entry returns this to request that it be called with RELOADHOST_DEINIT
#define RELOADHOST_CLOSE_REQUESTED INT_MAX

// name of f_rh_entry function in client
#define RELOADHOST_ENTRY_NAME reloadhost_entry

typedef struct reloadhost reloadhost_t;

typedef struct reloadhost_thread {
    thrd_t thread;
    atomic_bool *pause, *is_paused;
} reloadhost_thread_t;

typedef int (*reloadhost_entry_f)(int, char *[], reloadhost_op_e, reloadhost_t*);

typedef struct reloadhost {
    // pointer to function pointer registry function
    //
    // usage:
    //
    // struct foo { int (*funcptr)(); }
    // int myfunc() { ... }
    // struct foo f = malloc(sizeof(struct foo));
    // f->funcptr = myfunc;
    // reloadhost->regfunc(&f->funcptr);
    //
    // now f->funcptr is properly changed on code reload
    void (*reg_fn)(void*);

    // see regfunc
    // when heap storage for function pointer is free'd, call delfunc() with the
    // same address to free the function pointer in the reloadhost
    void (*del_fn)(void*);

    // register a variable for reloading
    void (*update_var_fn)(const char*, void*, size_t);

    // unregister a variable for reloading
    void (*del_var_fn)(const char*);

    // TODO: doc
    void (*reg_thread_fn)(thrd_t, atomic_bool *pause, atomic_bool *is_paused);
    void (*del_thread_fn)(thrd_t);

    // loaded address of module
    void *addr;

    // userdata pointer, can be used by client for arbitrary storage on reload
    // host
    void *userdata;

    // global recursive lock on reloadhost
    mtx_t lock;

    // program threads which should be paused while updating
    FIXLIST(reloadhost_thread_t, 32) threads;
} reloadhost_t;

/// FOR CLIENT APPLICATION ///

// used in macros
#include "../src/util/range.h" /* IWYU pragma: keep */

#ifdef RELOADHOST_CLIENT_ENABLED
#define RELOAD_VISIBLE
#else
typedef struct reloadhost reloadhost_t;
#define RELOAD_VISIBLE static
#endif // ifdef RELOADHOST_CLIENT_ENABLED

// current reload host, NULL if there isn't one / not built with RELOADABLE
extern reloadhost_t *g_reloadhost;

// must be defined by client application
#ifdef RELOADHOST_CLIENT_IMPL
reloadhost_t *g_reloadhost;
#endif // ifdef RELOADHOST_CLIENT_IMPL

typedef struct { void (*funcs[128])(); int n; } reloadhost_funclist_t;
extern reloadhost_funclist_t reloadhost_funclist;

#if defined(RELOADHOST_CLIENT_ENABLED)      \
    && !defined(RELOADHOST)                 \
    && !defined(RELOADHOST_CLIENT_DISABLED) \
    && !defined(CLANGD)

void reloadhost_static_var(
    const range_t *var,
    const char *name,
    const char *file,
    const char *func,
    bool use_thread,
    const range_t *buf);

#define _ON_RELOAD_IMPL(name_, ctor_name_, ...) \
    static void name_(void) __VA_ARGS__                                        \
    __attribute__((constructor)) static void ctor_name_(void) {                \
        reloadhost_funclist.funcs[reloadhost_funclist.n++] = name_;            \
    }                                                                          \

// do something on reload
// usage: ON_RELOAD({ x = y; })
#define ON_RELOAD(...)                         \
    _ON_RELOAD_IMPL(                           \
        CONCAT(_on_reload_, __COUNTER__),      \
        CONCAT(_on_reload_ctor_, __COUNTER__), \
        __VA_ARGS__)

#define RELOAD_STATIC_GLOBAL_RANGE_IMPL(_range, _name, _ctor_name)             \
    static void _name(void) {                                                  \
        static M_THREAD_LOCAL char buf[1024];                                  \
        const range_t _rng = (_range);                                         \
        reloadhost_static_var(                                                 \
            &_rng, #_range, __FILE__, "", false, RANGE_REF(buf));              \
    }                                                                          \
    __attribute__((constructor)) static void _ctor_name(void) {                \
        reloadhost_funclist.funcs[reloadhost_funclist.n++] = _name;            \
    }

#define RELOAD_STATIC_GLOBAL_RANGE(_range) \
    RELOAD_STATIC_GLOBAL_RANGE_IMPL(\
        (_range), \
        CONCAT(_rgbr_, __COUNTER__), \
        CONCAT(_rgbr_ctor_, __COUNTER__))

#define RELOAD_STATIC_GLOBAL(_var) RELOAD_STATIC_GLOBAL_RANGE(RANGE((_var)))

#define RELOAD_STATIC_RANGE(_range) do {                                       \
        static M_THREAD_LOCAL char buf[1024];                                    \
        const range_t _rng = (_range);                                         \
        reloadhost_static_var(                                           \
            &_rng, #_range, __FILE__, __FUNCTION__, false, RANGE_REF(buf));    \
    } while (0)

#define RELOAD_STATIC(_ptr, _sz) _RELOAD_STATIC_IMPL((_ptr), _ptr, _sz)

#define RELOAD_STATIC_VAR(_var) RELOAD_STATIC_RANGE(RANGE((_var)))

#define RELOAD_STATIC_THREAD_LOCAL_RANGE(_range) do {                          \
        static M_THREAD_LOCAL char buf[1024];                                    \
        const range_t _rng = (_range);                                         \
        reloadhost_static_var(                                           \
            &_rng, #_range, __FILE__, __FUNCTION__, true, RANGE_REF(buf));     \
    } while (0)

#define RELOAD_STATIC_THREAD_LOCAL(_ptr, _sz)                                  \
    _RELOAD_STATIC_THREAD_LOCAL_IMPL((_ptr), _ptr, _sz)

#define RELOAD_STATIC_THREAD_LOCAL_VAR(_var)                                   \
    RELOAD_STATIC_THREAD_LOCAL_RANGE(RANGE((_var)))

#define RELOAD_FUNCPTR(_pfuncptr) do {                                         \
        if (g_reloadhost) { g_reloadhost->reg_fn((_pfuncptr)); }               \
     } while (0);

#define RELOAD_DELETE_FUNCPTR(_pfuncptr) do {                                  \
        if (g_reloadhost) { g_reloadhost->del_fn((_pfuncptr)); }               \
     } while (0);

#define RELOAD_THREAD(_thread, _ppause, _pis_pause) do {                       \
        if (g_reloadhost) {                                                    \
            g_reloadhost->reg_thread_fn((_thread), (_ppause), (_pis_pause));   \
        }                                                                      \
    } while (0);

#define RELOAD_DELETE_THREAD(_thread) do {                                     \
        if (g_reloadhost) {                                                    \
            g_reloadhost->del_thread_fn((_thread));                            \
        }                                                                      \
    } while (0);

#else
#define ON_RELOAD(...)
#define RELOAD_STATIC_GLOBAL_RANGE(...)
#define RELOAD_STATIC_GLOBAL(...)
#define RELOAD_STATIC_RANGE(...)
#define RELOAD_STATIC(...)
#define RELOAD_STATIC_VAR(...)
#define RELOAD_STATIC_THREAD_LOCAL_RANGE(...)
#define RELOAD_STATIC_THREAD_LOCAL(...)
#define RELOAD_STATIC_THREAD_LOCAL_VAR(...)
#define RELOAD_FUNCPTR(...)
#define RELOAD_DELETE_FUNCPTR(...)
#define RELOAD_THREAD(...)
#define RELOAD_DELETE_THREAD(...)
#endif // defined(RELOADHOST_CLIENT_ENABLED) ...

#ifdef RELOADHOST_CLIENT_IMPL

#include <stdio.h>
#include "../src/util/assert.h"

reloadhost_funclist_t reloadhost_funclist;

void reloadhost_static_var(
        const range_t *var,
        const char *name,
        const char *file,
        const char *func,
        bool use_thread,
        const range_t *buf) {
    if (!g_reloadhost) { return; }

    char *bufstr = buf->ptr;
    if (!bufstr[0]) {
        int len = snprintf(bufstr, buf->size, "%s%s%s", file, func, name);
        ASSERT(len >= 0 && len < (int) buf->size, "%d", len);

        if (use_thread) {
            union { u8 bs[sizeof(thrd_t)]; thrd_t t; } u = {
                .t = thrd_current(),
            };

            for (int i = 0; i < ARRLEN(u.bs); i++) {
                const int res =
                    snprintf(bufstr + len, buf->size - len, "%02x", u.bs[i]);
                ASSERT(res == 2, "%d", res);
                len += 2;
            }
        }
    }

    g_reloadhost->update_var_fn(buf->ptr, var->ptr, var->size);
}

#endif // ifdef RELOADHOST_CLIENT_IMPL

/// END FOR CLIENT APPLICATION ///
