#pragma once

#include "util/enum.h"

#define ENUM_HOOK_TYPE(F, ...)     \
    F(PRE_RELOAD, 0, __VA_ARGS__)  \
    F(POST_RELOAD, 1, __VA_ARGS__) \
    F(EXIT, 2, __VA_ARGS__)        \

ENUM_DECL(hook_type, HOOK, ENUM_HOOK_TYPE)

typedef int hook_id_t;
typedef void (*hook_f)(void*);

// register a new hook
hook_id_t hook_register(hook_type_e type, hook_f fn, void *param);

// deregister a hook registered with hook_register
void hook_deregister(hook_id_t hook);

// called by "host" to execute all existing hooks of a specific type
void hook_call_hooks(hook_type_e);

#ifdef UTIL_IMPL

#include "util/alloc.h"
#include "util/fixlist.h"
#include "reloadhost.h"

#include <dlfcn.h>

ENUM_DEFINE(hook_type, HOOK, ENUM_HOOK_TYPE)

typedef struct {
    hook_id_t id;
    hook_type_e type;
    hook_f fn;
    void *param;
} hook_id_pair_t;

typedef struct {
    hook_id_t next_id;
    FIXLIST(hook_id_pair_t, 1024) list;
} hook_state_t;

// heap-allocated hook state
static hook_state_t *hs;

static void lazy_init_hooks() {
    RELOAD_STATIC_RANGE(RANGE(hs));

    if (hs) { return; }

    hs = mem_calloc(g_mallocator, sizeof(*hs));
    hs->next_id = 1;
}

static void deinit_hooks() {
    if (hs) { mem_free(g_mallocator, hs); }
    hs = NULL;
}

hook_id_t hook_register(hook_type_e type, hook_f fn, void *param) {
    lazy_init_hooks();

#ifndef RELOADHOST_CLIENT_ENABLED
    if (type == HOOK_PRE_RELOAD || type == HOOK_POST_RELOAD) {
        WARN("ignoring reload hook registered in non-reload build");
        return -1;
    }
#endif

    hook_id_pair_t *pair = fixlist_push(hs->list);
    *pair = (hook_id_pair_t) { hs->next_id, type, fn, param };
    hs->next_id++;

#ifdef RELOADHOST_CLIENT_ENABLED
    if (g_reloadhost) {
        g_reloadhost->reg_fn(&pair->fn);
    }
#endif

    return pair->id;
}

void hook_deregister(hook_id_t hook) {
    if (hook <= 0) {
        WARN("trying to deregister bad hook %d", hook);
        dumptrace(stderr);
        return;
    }

    fixlist_each(hs->list, it) {
        if (it.el->id == hook) {
#ifdef RELOADHOST_CLIENT_ENABLED
            if (g_reloadhost) {
                g_reloadhost->del_fn(&it.el->fn);
            }
#endif
            fixlist_remove_it(hs->list, it);
            return;
        }
    }

    ASSERT(false, "no such hook with id %d", hook);
}

void hook_call_hooks(hook_type_e type) {
    lazy_init_hooks();

    LOG("calling type %s", hook_type_to_str(type));

    fixlist_each(hs->list, it) {
        if (it.el->type == type) {
            it.el->fn(it.el->param);

            Dl_info info;
            if (!dladdr((const void*) it.el->fn, &info)) {
                WARN("  (failed to dladdr)");
            } else {
                LOG("  %s()", info.dli_sname);
            }
        }
    }

    if (type == HOOK_EXIT) {
        // also deinit hooks
        deinit_hooks();
    }
}

#endif // ifdef UTIL_IMPL
