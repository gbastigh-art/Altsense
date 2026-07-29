#ifndef UTIL_IMPL
#define UTIL_IMPL
#endif // ifndef UTIL_IMPL

#define RELOADHOST

#include "../src/util/assert.h"
#include "../src/util/log.h"
#include "../src/util/types.h"
#include "../src/util/map.h"
#include "../src/util/time.h"
#include "../src/util/fixlist.h"
#include "../src/util/thread.h"
#include "../src/util/time.h"

#define STB_MALLOC_IMPLEMENTATION
#include "ext/stb_malloc.h"

#include "ext/printf.c"
#include "ext/putchar.c"

#include "reloadhost.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    size_t size;
    void *addr;
    void *backup;
    bool dirty;
} var_t;

typedef struct {
    char *name;
    void (*func)(void*);
    void *userdata;
} hook_t;

// persistent data exposed to client
static reloadhost_t rh;

#define RELOADHOST_LOCK()   ASSERT(mtx_lock(&rh.lock) == thrd_success)
#define RELOADHOST_UNLOCK() ASSERT(mtx_unlock(&rh.lock) == thrd_success)

// currently loaded module
static void *module = NULL;

// map of storage address (void**) -> char* function name
static map_t addr_to_func_name;

// map of key -> var_t
static map_t name_to_var;

// map of void* -> Dl_info
static map_t addr_to_dladdr_cache;

// look up Dl_info through cache
static const Dl_info *dladdr_lookup(void *p) {
    Dl_info *info = map_get(Dl_info, &addr_to_dladdr_cache, p);

    if (!info) {
        Dl_info i;

        if (!dladdr(p, &i)) {
            WARN("(reloadhost) dladdr failed for %p", p);
            static Dl_info empty = (Dl_info) {
                .dli_fbase = NULL,
                .dli_fname = "",
                .dli_saddr = NULL,
                .dli_sname = "",
            };
            return &empty;
        }

        ASSERT(!map_contains(&addr_to_dladdr_cache, p));
        info = map_insert(&addr_to_dladdr_cache, p, i);
        ASSERT(!memcmp(info, &i, sizeof(i)));
    }

    return info;
}

// see reloadhost::reg_fn
static void reg_fn(void *p) {
    RELOADHOST_LOCK();
    const char *name = dladdr_lookup(*((void**) p))->dli_sname;
    ASSERT(name && name[0] != '\0', "could not dladdr_lookup %p", p);
    map_insert(&addr_to_func_name, p, mem_strdup(g_mallocator, name));
    RELOADHOST_UNLOCK();
}

// see reloadhost::del_fn
static void del_fn(void *p) {
    RELOADHOST_LOCK();
    if (!map_try_remove(&addr_to_func_name, p)) {
        WARN("bad del_fn for %p", p);
    }
    RELOADHOST_UNLOCK();
}

static void update_var_fn(const char *key, void *p, size_t size) {
    RELOADHOST_LOCK();
    var_t *pvar = map_get(var_t, &name_to_var, key);

    if (!pvar) {
        // register and return
        map_insert(
            &name_to_var,
            mem_strdup(g_mallocator, key),
            ((var_t) {
                .size = size,
                .addr = p,
                .backup = NULL,
                .dirty = false,
            }));
         goto done;
    }

    // nothing to do if not dirty
    if (!pvar->dirty) {
         goto done;
    }

    LOG("(reloadhost) updating variable %s @ %p", key, p);

    // copy and remove backup
    memcpy(p, pvar->backup, pvar->size);
    pvar->addr = p;

    pvar->dirty = false;
    mem_free(g_mallocator, pvar->backup);
    pvar->backup = NULL;

done:
    RELOADHOST_UNLOCK();
}

static void del_var_fn(const char *key) {
    RELOADHOST_LOCK();
    if (!map_try_remove(&name_to_var, key)) {
        WARN("bad del_var_fn for %s", key);
    }
    RELOADHOST_UNLOCK();
}

static void reg_thread_fn(
    thrd_t thread, atomic_bool *pause, atomic_bool *is_paused) {
    RELOADHOST_LOCK();
    *fixlist_push(rh.threads) = (reloadhost_thread_t) {
        .thread = thread,
        .pause = pause,
        .is_paused = is_paused,
    };
    RELOADHOST_UNLOCK();
}

static void del_thread_fn(thrd_t thread) {
    RELOADHOST_LOCK();
    bool found = false;
    fixlist_each(rh.threads, it) {
        if (it.el->thread == thread) {
            fixlist_remove_it(rh.threads, it);
            found = true;
            break;
        }
    }
    ASSERT(found);
    RELOADHOST_UNLOCK();
}

static void var_free(map_t*, void *userdata) {
    var_t *var = userdata;
    ASSERT((!var->backup && !var->dirty) || (var->backup && var->dirty));

    if (var->backup) {
        mem_free(g_mallocator, var->backup);
    }
}

int main(int argc, char *argv[]) {
    const char *usage = "usage: reloadhost <module> [args...]";
    ASSERT(argc >= 2, usage);

    char *modpath = argv[1];
    LOG("(reloadhost) module is %s", modpath);

    rh = (reloadhost_t) {
        .reg_fn = reg_fn,
        .del_fn = del_fn,
        .update_var_fn = update_var_fn,
        .del_var_fn = del_var_fn,
        .reg_thread_fn = reg_thread_fn,
        .del_thread_fn = del_thread_fn,
        .userdata = NULL
    };

    ASSERT(mtx_init(&rh.lock, mtx_recursive) == thrd_success);

    map_init(
        &addr_to_func_name,
        g_mallocator,
        sizeof(void**),
        sizeof(const char*),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        map_allocator_free,
        NULL);

    map_init(
        &name_to_var,
        g_mallocator,
        sizeof(char*),
        sizeof(var_t),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        var_free,
        NULL);

    map_init(
        &addr_to_dladdr_cache,
        g_mallocator,
        sizeof(void*),
        sizeof(Dl_info),
        map_hash_bytes,
        map_cmp_bytes,
        NULL,
        NULL,
        NULL);

    struct timespec mod_time;
    reloadhost_entry_f func = NULL;
    reloadhost_op_e op = RELOADHOST_INIT;

    bool first_try = false;

    char path[1024], cmd[1024];
    path[0] = '\0';
    cmd[0] = '\0';

    i64 last_stat = 0;
    struct stat st;

    while (true) {
        int res;

        // only stat every 100ms
        const i64 now = time_ns();

        if (!last_stat || (now - last_stat) / 1000000.0f > 100.0f) {
            last_stat = now;
            res = stat(modpath, &st);

            if (res < 0) {
                WARN("failed to stat %s", modpath);

                if (first_try) {
                    ASSERT(false);
                } else {
                    continue;
                }
            }

            first_try = false;
        }

        struct timespec ts;
        timespec_get(&ts, TIME_UTC);

        // set to thue if other threads are paused
        bool threads_paused = false;

        // reload
        if (op == RELOADHOST_INIT
            || (st.st_mtimespec.tv_sec > mod_time.tv_sec
                && ts.tv_sec > st.st_mtimespec.tv_sec)) {
            if (op != RELOADHOST_INIT) {
                // pause all threads...
                // TODO: don't wait in sequence
                RELOADHOST_LOCK();
                fixlist_each(rh.threads, it) {
                    LOG("pausing thread %p", it.el->thread);

                    *it.el->pause = true;
                    while (!*it.el->is_paused) {
                        struct timespec
                            sleep = { .tv_nsec = secs_to_ns(0.05f) },
                            remaining;
                        thrd_sleep(&sleep, &remaining);
                    }
                }
                RELOADHOST_UNLOCK();

                threads_paused = true;
            }

            if (module) {
                res = func(argc - 1, &modpath, RELOADHOST_PRE_RELOAD, &rh);
                LOG("(reloadhost) module is %p", module);
                ASSERT(!res, "failed RELOADHOST_PRE_RELOAD: %d", res);

                // make copies of all non-dirty vars
                map_each(char*, var_t, &name_to_var, it) {
                    LOG("(reloadhost) backing up %s", *it.key);

                    // dirty vars don't need to be copied, they haven't been
                    // touched since last reload
                    if (it.value->dirty) { continue; }

                    ASSERT(!it.value->backup);
                    it.value->backup =
                        mem_alloc_inplace(
                            g_mallocator,
                            it.value->size,
                            it.value->addr);
                    it.value->dirty = true;
                }

                ASSERT(!dlclose(module));
            }

            if (strlen(path) != 0) {
                snprintf(cmd, sizeof(cmd), "rm %s", path);
                LOG("(reloadhost) executing %s", cmd);
                res = system(cmd);
                LOG("(reloadhost)  returned %d", res);
            }

            // copy to tmp, open from there
            snprintf(
                path,
                sizeof(path),
                "/tmp/reload-%d.dylib",
                rand());

            snprintf(
                cmd,
                sizeof(cmd),
                "cp %s %s",
                modpath,
                path);

            LOG("(reloadhost) executing %s", cmd);
            system(cmd);

            struct stat st;
            int res = stat(path, &st);
            int attempts = 0;

            do {
                res = stat(path, &st);
                LOG("(reloadhost) stat returned %d", res);
                attempts++;

                if (res < 0) {
                    WARN("failed to stat copied %s", path);
                    system("sleep 0.05");
                }

                ASSERT(attempts < 100, "failed after 100 attempts to stat");
            } while (res < 0);

            attempts = 0;
            do {
                attempts++;
                if (!module) {
                    LOG("(reloadhost) load failed, sleeping");
                    system("sleep 0.05");
                }

                LOG("(reloadhost) error is %s", dlerror());
                module = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
                rh.addr = module;
                map_clear(&addr_to_dladdr_cache);
                LOG("(reloadhost) module is %p", module);

                if (!module) {
                    LOG("(reloadhost) failed to load: %s", dlerror());
                }

                ASSERT(attempts < 100, "failed after 100 attempts to dlopen");
            } while (!module);

            ASSERT(module, "failed to load: %s", dlerror());

            func =
                (reloadhost_entry_f) dlsym(
                    module,
                    STRINGIFY(RELOADHOST_ENTRY_NAME));
            ASSERT(func, "could not find entry");

            ASSERT(stat(modpath, &st) >= 0, "failed to stat for timespec");
            mod_time = st.st_mtimespec;

            // do RELOADHOST_RELOAD before registering variables
            LOG("(reloadhost) calling RELOADHOST_RELOAD");
            res = func(argc - 1, &modpath, RELOADHOST_RELOAD, &rh);
            LOG("(reloadhost)   OK.");
            ASSERT(!res, "failed RELOADHOST_RELOAD: %d", res);

            // call reloadhost_funclist
            reloadhost_funclist_t *funclist =
                dlsym(module, "reloadhost_funclist");

            LOG("(reloadhost) calling %d funcs...", funclist->n);
            for (int i = 0; i < funclist->n; i++) {
                funclist->funcs[i]();
            }

            if (op != RELOADHOST_INIT) {
                op = RELOADHOST_POST_RELOAD;
            }
        }

        if (op == RELOADHOST_POST_RELOAD) {
            // reload function pointers
            map_each(void**, const char*, &addr_to_func_name, it) {
                void *sym = dlsym(module, *it.value);
                ASSERT(
                    sym,
                    "could not reload function pointer %s", *it.value);
                memcpy(*it.key, &sym, sizeof(void*));
            }
        }

        res = func(argc - 1, &modpath, op, &rh);

        if (res) {
            if (res == RELOADHOST_CLOSE_REQUESTED) {
                LOG("client requested close, exiting");
                return func(argc - 1, &modpath, RELOADHOST_DEINIT, &rh);
            } else {
                LOG("client exited with code %d", res);
                return res;
            }
        }

        // unpause threads
        if (threads_paused) {
            RELOADHOST_LOCK();
            fixlist_each(rh.threads, it) {
                LOG("unpausing thread %p", it.el->thread);
                *it.el->pause = false;
            }
            RELOADHOST_UNLOCK();
        }

        op = RELOADHOST_STEP;
    }

    map_destroy(&addr_to_func_name);
    map_destroy(&name_to_var);
    map_destroy(&addr_to_dladdr_cache);

    return 0;
}
