#pragma once

#include "util/types.h"

typedef struct range range_t;

// maximum size for kvstore name
#define MAX_KVSTORE_NAME_SIZE 16384

void kvstore_init(kvstore_t *kvs, allocator_t *allocator);

void kvstore_destroy(kvstore_t *kvs);

// clears kvstore
void kvstore_clear(kvstore_t *kvs);

// returns true if kvstore stores nothing
bool kvstore_empty(const kvstore_t *kvs);

// copies src to dst, both must be initialized (!)
void kvstore_copy(kvstore_t *dst, const kvstore_t *src);

// destructively moves kvstore onto specified allocator, no-op if it's already
// there
kvstore_t kvstore_move(kvstore_t *kvs, allocator_t *a);

// compute hash for kvstore
hash_t kvstore_hash(const kvstore_t *kvs);

// check if kvstores are equal
bool kvstore_eq(const kvstore_t *a, const kvstore_t *b);

// kvstore -> pretty string
char *kvstore_to_str(const kvstore_t *kvs, allocator_t *al);

// kvstore -> json string
char *kvstore_to_json(const kvstore_t *kvs, allocator_t *al);

// try json string -> kvstore, returns next character in view after kvstore
const char *kvstore_from_json(kvstore_t *kvs, const str_view_t *view);

// size of serialized kvstore
int kvstore_serialize_size(const kvstore_t *kvs);

// serlialize to list of bytes
// returns number of bytes written or <0 on error
int kvstore_serialize(const kvstore_t *kvs, const range_t *data);

// deserialize from bytes
// returns number of bytes read or <0 on error
int kvstore_deserialize(kvstore_t *kvs, const range_t *data);

// rename old_key to new_key, returns pointer to newly inserted value if present
const any_t *kvstore_rename(
    kvstore_t *kvs,
    const char *old_key,
    const char *new_key);

const any_t *kvstore_get(const kvstore_t *kvs, const char *name);

void kvstore_set(kvstore_t *kvs, const char *name, const any_t *value);

void kvstore_set_from_view(
    kvstore_t *kvs, const str_view_t *name, const any_t *value);

// move with destructive move of value
void kvstore_move_value(kvstore_t *kvs, const char *name, any_t *value);

// move with destructive move of value
void kvstore_move_value_from_view(
    kvstore_t *kvs, const str_view_t *name, any_t *value);

bool kvstore_has(const kvstore_t *kvs, const char *name);

bool kvstore_remove(kvstore_t *kvs, const char *name);

void kvstore_set_fmt(kvstore_t *kvs, const char *name, const char *fmt, ...);

void kvstore_set_str(kvstore_t *kvs, const char *name, const char *value);

bool kvstore_get_str(
    const kvstore_t *kvs, const char *name, allocator_t *a, char **value);

bool kvstore_get_strn(
    const kvstore_t *kvs, const char *name, char *value, size_t sz);

// returns NULL if not present
kvstore_t *kvstore_get_kvstore(const kvstore_t *kvs, const char *name);

// set name to kvstore value
// copies from "child"
void kvstore_set_kvstore(
        kvstore_t *kvs,
        const char *name,
        const kvstore_t *child);

// destructively move another kvstore into this kvstore
void kvstore_move_kvstore(
        kvstore_t *kvs,
        const char *name,
        kvstore_t *child);

void kvstore_set_bytes(
    kvstore_t *kvs,
    const char *name,
    const range_t *data);

range_t kvstore_get_bytes(
    const kvstore_t *kvs,
    const char *name);

// get "name" as bytes allocated on ""
range_t kvstore_get_as_bytes(
    const kvstore_t *kvs,
    const char *name,
    allocator_t *al);

bool kvstore_get_bytes_to(
    const kvstore_t *kvs,
    const char *name,
    const range_t *dst);

#define DECL_PRIMITIVE_GET_SET(T)                                              \
    bool kvstore_get_##T(                                                      \
        const kvstore_t *kvstore,                                              \
        const char *name,                                                      \
        T *out);                                                               \
                                                                               \
    bool kvstore_get_as_##T(                                                   \
        const kvstore_t *kvstore,                                              \
        const char *name,                                                      \
        T *out);                                                               \
                                                                               \
    T kvstore_get_##T##_or_default(                                            \
        const kvstore_t *kvstore,                                              \
        const char *name,                                                      \
        T _default);                                                           \
                                                                               \
    void kvstore_set_##T(                                                      \
        kvstore_t *kvstore,                                                    \
        const char *name,                                                      \
        T value);

DECL_PRIMITIVE_GET_SET(int)
DECL_PRIMITIVE_GET_SET(char)
DECL_PRIMITIVE_GET_SET(bool)
DECL_PRIMITIVE_GET_SET(u8)
DECL_PRIMITIVE_GET_SET(u16)
DECL_PRIMITIVE_GET_SET(u32)
DECL_PRIMITIVE_GET_SET(u64)
DECL_PRIMITIVE_GET_SET(i8)
DECL_PRIMITIVE_GET_SET(i16)
DECL_PRIMITIVE_GET_SET(i32)
DECL_PRIMITIVE_GET_SET(i64)
DECL_PRIMITIVE_GET_SET(f32)
DECL_PRIMITIVE_GET_SET(f64)
DECL_PRIMITIVE_GET_SET(v2)
DECL_PRIMITIVE_GET_SET(v3)
DECL_PRIMITIVE_GET_SET(v4)
DECL_PRIMITIVE_GET_SET(v2i)

#undef DECL_PRIMITIVE_GET_SET

// iterate kvstore key/values
#define kvstore_each(_kvs, _it) map_each(char*, any_t, (_kvs)->map, _it)

// allocated memory footprint of this kvstore (in bytes)
// 0 if any has no allocated data
int kvstore_footprint(const kvstore_t*);

#ifdef UTIL_IMPL

#include "util/bytebuf.h"
#include "util/any.h"
#include "util/map.h"
#include "util/range.h"
#include "util/str.h"

void kvstore_init(kvstore_t *kvs, allocator_t *allocator) {
    *kvs = (kvstore_t) { .allocator = allocator };
}

static void kvstore_ensure_map(kvstore_t *kvs) {
    if (kvs->map) { return; }

    kvs->map = mem_alloc(kvs->allocator, sizeof(*kvs->map));
    map_init(
        kvs->map,
        kvs->allocator,
        sizeof(char*),
        sizeof(any_t),
        map_hash_str,
        map_cmp_str,
        map_allocator_free,
        map_any_free,
        NULL);
}

void kvstore_destroy(kvstore_t *kvs) {
    if (!kvs->map) { return; }
    map_destroy(kvs->map);
    mem_free(kvs->allocator, kvs->map);
    *kvs = (kvstore_t) { 0 };
}

void kvstore_clear(kvstore_t *kvs) {
    if (!kvs->map) { return; }
    map_destroy(kvs->map);
}

bool kvstore_empty(const kvstore_t *kvs) {
    return !kvs->map || map_empty(kvs->map);
}

void kvstore_copy(kvstore_t *dst, const kvstore_t *src) {
    ASSERT(dst->allocator);
    ASSERT(src->allocator);

    if (!src->map || map_empty(src->map)) {
        // nothing to copy
        return;
    }

    ASSERT(map_valid(src->map));
    kvstore_ensure_map(dst);

    map_each(char*, any_t, src->map, it) {
        any_t copy;
        any_init(&copy, dst->allocator);
        any_copy(&copy, it.value);

        char *key =
            mem_alloc_inplace(
                dst->allocator,
                strlen(*it.key) + 1,
                *it.key);
        map_insertp(dst->map, &key, &copy);
    }
}

kvstore_t kvstore_move(kvstore_t *kvs, allocator_t *a) {
    if (kvs->allocator == a) {
        kvstore_t res = *kvs;
        *kvs = (kvstore_t) { 0 };
        return res;
    }

    kvstore_t dst;
    kvstore_init(&dst, a);
    kvstore_copy(&dst, kvs);
    *kvs = (kvstore_t) { 0 };
    return dst;
}

hash_t kvstore_hash(const kvstore_t *kvs) {
    hash_t h = 0x18273645;
    if (!kvs->map) { return h; }
    map_each(char*, any_t, kvs->map, it) {
        h = hash_add_str(h, *it.key);
        h = hash_combine(h, any_hash(it.value));
    }
    return h;
}

bool kvstore_eq(const kvstore_t *a, const kvstore_t *b) {
    if (!a->map) { return !b->map; }

    map_each(char*, any_t, a->map, it) {
        const any_t
            *v_a = it.value,
            *v_b = kvstore_get(b, *it.key);

        if (!v_b || !any_eq(v_a, v_b)) { return false; }
    }

    return true;
}

char *kvstore_to_str(const kvstore_t *kvs, allocator_t *al) {
    if (!kvs->map) { return mem_strdup(al, "{}"); }

    strbuf_t sb = strbuf_create(tlscratch());
    strbuf_ap_str(&sb, "{");

    bool first = true;
    map_each(char*, any_t, kvs->map, it) {
        const char *prefix;

        if (first) {
            prefix = " ";
            first = false;
        } else {
            prefix = ", ";
        }

        strbuf_ap_fmt(
            &sb,
            "%s%s: %s",
            prefix,
            *it.key,
            any_to_str(it.value, tlscratch()));
    }
    strbuf_ap_str(&sb, " }");
    return strbuf_dump(&sb, al);
}

char *kvstore_to_json(const kvstore_t *kvs, allocator_t *al) {
    if (!kvs->map) { return mem_strdup(al, "{}"); }

    strbuf_t sb = strbuf_create(tlscratch());
    strbuf_ap_str(&sb, "{");

    bool first = true;
    map_each(char*, any_t, kvs->map, it) {
        const char *prefix;

        if (first) {
            prefix = " ";
            first = false;
        } else {
            prefix = ", ";
        }

        str_view_t key = str_view_from(*it.key);
        key = str_view_escape(&key, tlscratch());

        strbuf_ap_fmt(
            &sb,
            "%s\"%s\": %s",
            prefix,
            key.start,
            any_to_json(it.value, tlscratch()));
    }
    strbuf_ap_str(&sb, " }");

    return strbuf_dump(&sb, al);
}

const char *kvstore_from_json(kvstore_t *kvs, const str_view_t *view) {
    ASSERT(kvs->allocator);
    kvstore_ensure_map(kvs);

    const char *errmsg = NULL;

    const char *p = view->start;
    if (*p != '{') {
        errmsg = "invalid kvstore start";
        goto fail;
    }

    // skip entry {
    p++;

    while (*p && *p != '}') {
        if (isspace(*p)) {
            p++;
        } else if (*p == '\'' || *p == '\"') {
            // key, read name
            char quote = *p;
            const char *start = p + 1;

            // skip entry quote
            p++;

            // seek to end quote
            while (*p && !(*p == quote && *(p - 1) != '\\')) { p++; }

            if (!*p) {
                errmsg = "unterminated key";
                goto fail;
            }

            const str_view_t name = { start, p };

            // skip ending quote
            p++;

            while (isspace(*p)) { p++; }

            if (!*p || *p != ':') {
                errmsg = "no colon";
                goto fail;
            }

            // skip colon, skip whitespace to first valid character
            p++;

            while (isspace(*p)) { p++; }

            if (!*p) {
                errmsg = "no value";
                goto fail;
            }

            any_t value = any_create(kvs->allocator);

            const char *old = p;
            p = any_from_json(&value, NULL, &(str_view_t) { p, view->end });

            if (!p || p <= old) {
                errmsg =
                    mem_strfmt(
                        tlscratch(),
                        "error parsing json value for %s",
                        str_view_dump(&name, tlscratch()));
                goto fail;
            }

            // unescape name for insert
            const str_view_t name_unescaped =
                str_view_unescape(&name, tlscratch());
            kvstore_move_value_from_view(kvs, &name_unescaped, &value);

            // seek to comma
            while (isspace(*p)) { p++; }

            // if comma (could also be end), continue
            if (*p == ',') { p++; }
        } else {
            errmsg = "malformatted json";
            goto fail;
        }
    }

    if (*p != '}') {
        errmsg = "unterminated json";
        goto fail;
    }

    // OK, return next char
    return p + 1;
fail:
     WARN(
        "%s \"%s\"",
        errmsg,
        str_view_dump(view, tlscratch()));
    return NULL;
}

// kvstore binary format:
// * KVSTORE_MAGIC
// * size (i32/4 bytes)
// * for each entry:
//   * name (null-terminated string)
//   * serialized any
// * KVSTORE_MAGIC

int kvstore_serialize_size(const kvstore_t *kvs) {
    // min size is magic + size + magic
    int size = 3 * 4;

    if (!kvs->map) {
        goto done;
    }

    ASSERT(map_valid(kvs->map));

    map_each(char*, any_t, kvs->map, it) {
        size += strlen(*it.key) + 1;
        size += any_serialize_size(it.value);
    }

done:
    return size;
}

#define KVSTORE_MAGIC 0x4A4A5B5B

int kvstore_serialize(const kvstore_t *kvs, const range_t *data) {
    const int expected_size = kvstore_serialize_size(kvs);
    bytebuf_t buf = bytebuf_wrap(data);

#define WRITE_INT(_i) do {                                                     \
        if (bytebuf_write_int(&buf, (_i)) != 4) {                              \
            WARN("out of space in bytebuf");                                   \
            dumptrace(stderr);                                                 \
            return -1;                                                         \
        }                                                                      \
    } while (0)

    WRITE_INT(KVSTORE_MAGIC);

    if (!kvs->map || map_size(kvs->map) == 0) {
        WRITE_INT(0);
        WRITE_INT(KVSTORE_MAGIC);
        ASSERT(bytebuf_tell(&buf) == 3 * 4);
        return 3 * 4;
    }

    ASSERT(map_valid(kvs->map));
    WRITE_INT(map_size(kvs->map));

    map_each(char*, any_t, kvs->map, it) {
        char *name = *it.key;
        ASSERT(strlen(name) <= MAX_KVSTORE_NAME_SIZE);
        bytebuf_write_str(&buf, name);

        // reserve bytes corresponding to size of any and write out
        const int any_size = any_serialize_size(it.value);
        if (bytebuf_remaining(&buf) < any_size) {
            WARN(
                "needed %d bytes but have %d left",
                any_size,
                bytebuf_remaining(&buf));
            return -2;
        }
        const range_t range = bytebuf_remaining_as_range(&buf);
        const int size_written = any_serialize(it.value, &range);
        ASSERT(size_written == any_size);
        bytebuf_skip(&buf, size_written);
    }

    WRITE_INT(KVSTORE_MAGIC);

#undef WRITE_INT

    ASSERT(
        bytebuf_tell(&buf) == expected_size,
        "%d / %d",
        bytebuf_tell(&buf), expected_size);
    return bytebuf_tell(&buf);
}

int kvstore_deserialize(kvstore_t *kvs, const range_t *data) {
    ASSERT(kvs->allocator);

    int res = 0;

    bytebuf_t buf = bytebuf_wrap(data);

#define READ_INT() ({                                               \
        int x;                                                      \
        if (bytebuf_read_int(&buf, &x) != 4) { res = 1; goto done; }\
        x;                                                          \
    })

    // check start magic
    if (READ_INT() != KVSTORE_MAGIC) { res = -1; goto done; }

    const int size = READ_INT();

    if (size == 0) {
        // quick exit, no need to allocate map for kvstore
        if (READ_INT() != KVSTORE_MAGIC) { res = -5; goto done; }
        goto done;
    }

    kvstore_ensure_map(kvs);

    for (int i = 0; i < size; i++) {
        char *name;
        if (bytebuf_read_str(&buf, tlscratch(), &name) < 0) {
            res = -2;
            goto done;
        }

        // read any into tmp
        any_t any;
        any_init(&any, tlscratch());

        const range_t range = bytebuf_remaining_as_range(&buf);
        const int n_read = any_deserialize(&any, &range);
        kvstore_set(kvs, name, &any);

        if (n_read <= 0) { res = -3; goto done; }
        bytebuf_skip(&buf, n_read);
    }

    // check end magic
    if (READ_INT() != KVSTORE_MAGIC) { res = -4; goto done; }
#undef READ_INT

done:
    // OK
    return res == 0 ? bytebuf_tell(&buf) : res;
}

const any_t *kvstore_rename(
    kvstore_t *kvs,
    const char *old_key,
    const char *new_key) {
    if (!kvs->map) { return NULL; }

    any_t value;
    if (!map_try_remove(kvs->map, old_key, &value)) {
        return NULL;
    }

    ASSERT_DEBUG(value.allocator == kvs->allocator);

    return map_insert(kvs->map, mem_strdup(kvs->allocator, new_key), value);
}

const any_t *kvstore_get(const kvstore_t *kvs, const char *name) {
    return kvs->map ? map_getp(any_t, kvs->map, &name) : NULL;
}

void kvstore_set(kvstore_t *kvs, const char *name, const any_t *value) {
    kvstore_ensure_map(kvs);

    any_t any;
    any_init(&any, kvs->allocator);
    any_copy(&any, value);

    char *key = mem_strdup(kvs->allocator, name);
    map_insertp(kvs->map, &key, &any);
}

void kvstore_set_from_view(
    kvstore_t *kvs, const str_view_t *name, const any_t *value) {
    kvstore_ensure_map(kvs);

    any_t any;
    any_init(&any, kvs->allocator);
    any_copy(&any, value);

    char *key = str_view_dump(name, kvs->allocator);
    map_insertp(kvs->map, &key, &any);
}

void kvstore_move_value(kvstore_t *kvs, const char *name, any_t *value) {
    kvstore_ensure_map(kvs);
    const any_t any = any_move(value, kvs->allocator);
    char *key = mem_alloc_inplace(kvs->allocator, strlen(name) + 1, name);
    map_insertp(kvs->map, &key, &any);
}

void kvstore_move_value_from_view(
    kvstore_t *kvs, const str_view_t *name, any_t *value) {
    kvstore_ensure_map(kvs);
    const any_t any = any_move(value, kvs->allocator);
    char *key = str_view_dump(name, kvs->allocator);
    map_insertp(kvs->map, &key, &any);
}

bool kvstore_has(const kvstore_t *kvs, const char *name) {
    return kvs->map ? map_containsp(kvs->map, &name) : false;
}

bool kvstore_remove(kvstore_t *kvs, const char *name) {
    return kvs->map ? map_try_remove(kvs->map, &name) : false;
}

void kvstore_set_fmt(kvstore_t *kvs, const char *name, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    any_t any;
    any_init(&any, tlscratch());
    any_set_str(&any, mem_vstrfmt(tlscratch(), fmt, args));
    kvstore_set(kvs, name, &any);
}

void kvstore_set_str(kvstore_t *kvs, const char *name, const char *value) {
    any_t any;
    any_init(&any, tlscratch());
    any_set_str(&any, value);
    kvstore_set(kvs, name, &any);
}

bool kvstore_get_str(
        const kvstore_t *kvs,
        const char *name,
        allocator_t *a,
        char **value) {
    const any_t *any = kvstore_get(kvs, name);
    if (!any) { return false; }
    *value = mem_strdup(a, any_get_str(any));
    return true;
}

bool kvstore_get_strn(
    const kvstore_t *kvs, const char *name, char *value, size_t sz) {
    char *str;
    if (!kvstore_get_str(kvs, name, tlscratch(), &str)) { return false; }
    const int res = snprintf(value, sizeof(value), "%s", str);
    return res >= 0 && res < (int) sz;
}

kvstore_t *kvstore_get_kvstore(const kvstore_t *kvs, const char *name) {
    const any_t *a = kvstore_get(kvs, name);
    return a ? any_get_kvstore(a) : NULL;
}

void kvstore_set_kvstore(
        kvstore_t *kvs,
        const char *name,
        const kvstore_t *child) {
    any_t value;
    any_init(&value, kvs->allocator);
    any_set_kvstore(&value, child);
    kvstore_move_value(kvs, name, &value);
}

void kvstore_move_kvstore(
        kvstore_t *kvs,
        const char *name,
        kvstore_t *child) {
    any_t value;
    any_init(&value, kvs->allocator);
    any_move_kvstore(&value, child);
    kvstore_move_value(kvs, name, &value);
}

void kvstore_set_bytes(
    kvstore_t *kvs,
    const char *name,
    const range_t *data) {
    any_t any;
    any_init(&any, tlscratch());
    any_set_bytes(&any, data);
    kvstore_set(kvs, name, &any);
}

range_t kvstore_get_bytes(
    const kvstore_t *kvs,
    const char *name) {
    const any_t *any = kvstore_get(kvs, name);
    return any ? any_get_bytes(any) : (range_t) { 0 };
}

range_t kvstore_get_as_bytes(
    const kvstore_t *kvs,
    const char *name,
    allocator_t *al) {
    const any_t *any = kvstore_get(kvs, name);
    return any ? any_get_as_bytes(any, al) : (range_t) { 0 };
}

bool kvstore_get_bytes_to(
    const kvstore_t *kvs,
    const char *name,
    const range_t *dst) {
    const any_t *any = kvstore_get(kvs, name);
    if (!any) { WARN("could not find"); return false; }
    const range_t range = any_get_bytes(any);
    if (range.size != dst->size) {
        WARN("bad size: %d/%d", range.size, dst->size);
        return false;
    }
    memcpy(dst->ptr, range.ptr, dst->size);
    return true;
}

#define DEFINE_PRIMITIVE_GET_SET(T, A)                                         \
    bool kvstore_get_##T(                                                      \
        const kvstore_t *kvstore,                                              \
        const char *name,                                                      \
        T *out) {                                                              \
        const any_t *any = kvstore_get(kvstore, name);                         \
        return any && any_get_##T(any, out);                                   \
    }                                                                          \
                                                                               \
    bool kvstore_get_as_##T(                                                   \
        const kvstore_t *kvstore,                                              \
        const char *name,                                                      \
        T *out) {                                                              \
        const any_t *any = kvstore_get(kvstore, name);                         \
        if (!any) { return false; }                                            \
        any_t copy = any_create(NULL);                                         \
        if (!any_try_massage_type(&copy, any, A)) { return false; }            \
        return any_get_##T(&copy, out);                                        \
    }                                                                          \
                                                                               \
    T kvstore_get_##T##_or_default(                                            \
        const kvstore_t *kvstore,                                              \
        const char *name,                                                      \
        T _default) {                                                          \
        T res;                                                                 \
        return kvstore_get_##T(kvstore, name, &res) ? res : _default;          \
    }                                                                          \
                                                                               \
    void kvstore_set_##T(                                                      \
        kvstore_t *kvstore,                                                    \
        const char *name,                                                      \
        T value) {                                                             \
        any_t any;                                                             \
        any_init(&any, tlscratch());                                           \
        any_set_##T(&any, value);                                              \
        kvstore_set(kvstore, name, &any);                                      \
    }

DEFINE_PRIMITIVE_GET_SET(int, ANY_INT)
DEFINE_PRIMITIVE_GET_SET(char, ANY_CHAR)
DEFINE_PRIMITIVE_GET_SET(bool, ANY_BOOL)
DEFINE_PRIMITIVE_GET_SET(u8, ANY_U8)
DEFINE_PRIMITIVE_GET_SET(u16, ANY_U16)
DEFINE_PRIMITIVE_GET_SET(u32, ANY_U32)
DEFINE_PRIMITIVE_GET_SET(u64, ANY_U64)
DEFINE_PRIMITIVE_GET_SET(i8, ANY_I8)
DEFINE_PRIMITIVE_GET_SET(i16, ANY_I16)
DEFINE_PRIMITIVE_GET_SET(i32, ANY_I32)
DEFINE_PRIMITIVE_GET_SET(i64, ANY_I64)
DEFINE_PRIMITIVE_GET_SET(f32, ANY_F32)
DEFINE_PRIMITIVE_GET_SET(f64, ANY_F64)
DEFINE_PRIMITIVE_GET_SET(v2, ANY_V2)
DEFINE_PRIMITIVE_GET_SET(v3, ANY_V3)
DEFINE_PRIMITIVE_GET_SET(v4, ANY_V4)
DEFINE_PRIMITIVE_GET_SET(v2i, ANY_V2I)

#undef DEFINE_PRIMITIVE_GET_SET

int kvstore_footprint(const kvstore_t *kvs) {
    if (!kvs->allocator || !kvs->map) { return 0; }

    int sz = map_footprint(kvs->map);
    sz += sizeof(*kvs->map);

    map_each(char*, any_t, kvs->map, it) {
        sz += any_footprint(it.value);
    }

    return sz;
}

#endif // ifdef UTIL_IMPL
