#pragma once

#include "util/alloc.h"
#include "util/range.h"
#include "util/hash.h"
#include "util/enum.h"
#include "util/dynlist.h"
#include "util/str.h"

// basic "any" type for a number of possible primitive types, supporting some
// simple type-safe get/set operations

#define ENUM_ANY_TYPE(_F, ...)   \
    _F(NONE,    0,  __VA_ARGS__) \
    _F(INT,     1,  __VA_ARGS__) \
    _F(UINT,    2,  __VA_ARGS__) \
    _F(U8,      3,  __VA_ARGS__) \
    _F(U16,     4,  __VA_ARGS__) \
    _F(U32,     5,  __VA_ARGS__) \
    _F(U64,     6,  __VA_ARGS__) \
    _F(I8,      7,  __VA_ARGS__) \
    _F(I16,     8,  __VA_ARGS__) \
    _F(I32,     9,  __VA_ARGS__) \
    _F(I64,     10, __VA_ARGS__) \
    _F(F32,     11, __VA_ARGS__) \
    _F(F64,     12, __VA_ARGS__) \
    _F(BOOL,    13, __VA_ARGS__) \
    _F(V2,      14, __VA_ARGS__) \
    _F(V2I,     15, __VA_ARGS__) \
    _F(STR,     16, __VA_ARGS__) \
    _F(CHAR,    17, __VA_ARGS__) \
    _F(BYTES,   18, __VA_ARGS__) \
    _F(KVSTORE, 19, __VA_ARGS__) \
    _F(LIST,    20, __VA_ARGS__) \
    _F(V3,      21, __VA_ARGS__) \
    _F(V4,      22, __VA_ARGS__) \

ENUM_DECL(any_type, ANY, ENUM_ANY_TYPE, u8)

typedef struct any {
    allocator_t *allocator;
    any_type_e type;
    union {
        int int_value;
        uint uint_value;
        u8 u8_value;
        u16 u16_value;
        u32 u32_value;
        u64 u64_value;
        i8 i8_value;
        i16 i16_value;
        i32 i32_value;
        i64 i64_value;
        bool bool_value;
        f32 f32_value;
        f64 f64_value;
        v2 v2_value;
        v3 v3_value;
        v4 v4_value;
        v2i v2i_value;
        str_view_t str_value;
        char char_value;
        struct { u8 *arr; int n; } bytes_value;
        kvstore_t kvstore_value;
        DYNLIST(any_t) list_value;
    };
} any_t;

// construct any in constant context
#define any_const_none()   ((any_t) { .type = ANY_NONE  })
#define any_const_int(_v)  ((any_t) { .type = ANY_INT,  .int_value  = (_v) })
#define any_const_uint(_v) ((any_t) { .type = ANY_UINT, .uint_value = (_v) })
#define any_const_u8(_v)   ((any_t) { .type = ANY_U8,   .u8_value   = (_v) })
#define any_const_i8(_v)   ((any_t) { .type = ANY_I8,   .i8_value   = (_v) })
#define any_const_u16(_v)  ((any_t) { .type = ANY_U16,  .u16_value  = (_v) })
#define any_const_i16(_v)  ((any_t) { .type = ANY_I16,  .i16_value  = (_v) })
#define any_const_u32(_v)  ((any_t) { .type = ANY_U32,  .u32_value  = (_v) })
#define any_const_i32(_v)  ((any_t) { .type = ANY_I32,  .i32_value  = (_v) })
#define any_const_u64(_v)  ((any_t) { .type = ANY_U64,  .u64_value  = (_v) })
#define any_const_i64(_v)  ((any_t) { .type = ANY_I64,  .i64_value  = (_v) })
#define any_const_bool(_v) ((any_t) { .type = ANY_BOOL, .bool_value = (_v) })
#define any_const_f32(_v)  ((any_t) { .type = ANY_F32,  .f32_value  = (_v) })
#define any_const_f64(_v)  ((any_t) { .type = ANY_F64,  .f64_value  = (_v) })
#define any_const_v2(_v)   ((any_t) { .type = ANY_V2,   .v2_value   = (_v) })
#define any_const_v3(_v)   ((any_t) { .type = ANY_V3,   .v3_value   = (_v) })
#define any_const_v4(_v)   ((any_t) { .type = ANY_V4,   .v4_value   = (_v) })
#define any_const_v2i(_v)  ((any_t) { .type = ANY_V2I,  .v2i_value  = (_v) })

M_INLINE void any_init(any_t *a, allocator_t *allocator) {
    *a = (any_t) { .allocator = allocator, .type = ANY_NONE };
}

M_INLINE any_t any_create(allocator_t *allocator) {
    return (any_t) { .allocator = allocator, .type = ANY_NONE };
}

// destructive move from a onto specified allocator, no-op if already there :)
M_INLINE any_t any_move(any_t *a, allocator_t *allocator) {
    if (a->allocator == allocator) {
        any_t res = *a;
        *a = (any_t) { 0 };
        return res;
    }

    any_t dst = any_create(allocator);

    void any_copy(any_t*, const any_t*);
    any_copy(&dst, a);

    *a = (any_t) { 0 };
    return dst;
}

M_INLINE void any_destroy(any_t *a) {
    if (a->allocator) {
        switch (a->type) {
        case ANY_STR:
            if (str_view_valid(&a->str_value)) {
                mem_free(a->allocator, (void*) a->str_value.start);
            }
            break;
        case ANY_BYTES:
            if (a->bytes_value.arr) {
                mem_free(a->allocator, a->bytes_value.arr);
            }
            break;
        case ANY_KVSTORE:;
            extern void kvstore_destroy(kvstore_t*);
            kvstore_destroy(&a->kvstore_value);
            break;
        case ANY_LIST:;
            dynlist_each(a->list_value, it) {
                any_destroy(it.el);
            }
            dynlist_destroy(a->list_value);
            break;
        default:
        }
    }

    *a = (any_t) { 0 };
}

M_INLINE bool any_is(const any_t *a, any_type_e type) {
    return a->type == type;
}

#define DEFINE_GET_SET_WRAP(T, t_name, field_name, enum_val)                   \
    M_INLINE bool any_get_##t_name(const any_t *a, T *out) {                   \
        if (a->type == enum_val) {                                             \
            *out = a->field_name;                                              \
            return true;                                                       \
        }                                                                      \
        return false;                                                          \
    }                                                                          \
                                                                               \
    M_INLINE T any_get_##t_name##_or_default(const any_t *a, T _default) {     \
        return a->type == enum_val ? a->field_name : _default;                 \
    }                                                                          \
                                                                               \
    M_INLINE void any_set_##t_name(any_t *a, T value) {                        \
        allocator_t *allocator = a->allocator;                                 \
        any_destroy(a);                                                        \
        any_init(a, allocator);                                                \
        a->type = enum_val;                                                    \
        a->field_name = value;                                                 \
    }                                                                          \
                                                                               \
    M_INLINE any_t any_wrap_##t_name(allocator_t *al, T value) {               \
        any_t a = any_create(al);                                              \
        any_set_##t_name(&a, value);                                           \
        return a;                                                              \
    }

DEFINE_GET_SET_WRAP(int,  int,  int_value,  ANY_INT)
DEFINE_GET_SET_WRAP(uint, uint, uint_value, ANY_UINT)
DEFINE_GET_SET_WRAP(u8,   u8,   u8_value,   ANY_U8)
DEFINE_GET_SET_WRAP(u16,  u16,  u16_value,  ANY_U16)
DEFINE_GET_SET_WRAP(u32,  u32,  u32_value,  ANY_U32)
DEFINE_GET_SET_WRAP(u64,  u64,  u64_value,  ANY_U64)
DEFINE_GET_SET_WRAP(i8,   i8,   i8_value,   ANY_I8)
DEFINE_GET_SET_WRAP(i16,  i16,  i16_value,  ANY_I16)
DEFINE_GET_SET_WRAP(i32,  i32,  i32_value,  ANY_I32)
DEFINE_GET_SET_WRAP(i64,  i64,  i64_value,  ANY_I64)
DEFINE_GET_SET_WRAP(f32,  f32,  f32_value,  ANY_F32)
DEFINE_GET_SET_WRAP(f64,  f64,  f64_value,  ANY_F64)
DEFINE_GET_SET_WRAP(bool, bool, bool_value, ANY_BOOL)
DEFINE_GET_SET_WRAP(v2,   v2,   v2_value,   ANY_V2)
DEFINE_GET_SET_WRAP(v3,   v3,   v3_value,   ANY_V3)
DEFINE_GET_SET_WRAP(v4,   v4,   v4_value,   ANY_V4)
DEFINE_GET_SET_WRAP(v2i,  v2i,  v2i_value,  ANY_V2I)
DEFINE_GET_SET_WRAP(char, char, char_value, ANY_CHAR)

#undef DEFINE_GET_SET

M_INLINE void any_set_none(any_t *a) {
    allocator_t *al = a->allocator;
    any_destroy(a);
    any_init(a, al);
    a->type = ANY_NONE;
}

M_INLINE const char *any_get_str(const any_t *a) {
    return a->type == ANY_STR ? a->str_value.start : NULL;
}

M_INLINE const char *any_get_str_or_default(
    const any_t *a,
    const char *_default) {
    const char *res = any_get_str(a);
    return res ? res : _default;
}

M_INLINE void any_set_str(any_t *a, const char *value) {
    ASSERT(a->allocator);
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);
    a->type = ANY_STR;

    const usize len = strlen(value);
    char *str = mem_alloc_inplace(a->allocator, len + 1, value);
    a->str_value = (str_view_t) { str, &str[len] };
}

M_INLINE void any_set_str_from_view(any_t *a, const str_view_t *view) {
    ASSERT(a->allocator);
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);
    a->type = ANY_STR;
    a->str_value = str_view_dup(view, a->allocator);
}

M_INLINE void any_set_bytes(any_t *a, const range_t *data) {
    ASSERT(a->allocator);
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);

    a->type = ANY_BYTES;
    a->bytes_value.n = data->size;
    a->bytes_value.arr = mem_alloc_inplace(allocator, data->size, data->ptr);
}

// move data onto any
// NOTE: it is up to the user to ensure that "data" is allocated on the same
// allocator as "a"!!!
M_INLINE void any_move_bytes(any_t *a, const range_t *data) {
    ASSERT(a->allocator);
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);

    a->type = ANY_BYTES;
    a->bytes_value.n = data->size;
    a->bytes_value.arr = mem_alloc_inplace(allocator, data->size, data->ptr);
}

M_INLINE range_t any_get_bytes(const any_t *a) {
    ASSERT(a->type == ANY_BYTES);
    return (range_t) { .ptr = a->bytes_value.arr, .size = a->bytes_value.n };
}

// get any a as bytes into "out" on specified allocator
// returns NULL/0 range if not possible to get any as bytes
// NOTE: return value data is immutable
range_t any_get_as_bytes(const any_t *a, allocator_t *al);

M_INLINE kvstore_t *any_get_kvstore(const any_t *a) {
    return a->type == ANY_KVSTORE ? (kvstore_t*) &a->kvstore_value : NULL;
}

// copies kvs if present
kvstore_t *any_set_kvstore(any_t *a, const kvstore_t *kvs);

// destructively moves kvs if prsent
kvstore_t *any_move_kvstore(any_t *a, kvstore_t *kvs);

M_INLINE DYNLIST(any_t) *any_get_list(const any_t *a) {
    return a->type == ANY_LIST ? (DYNLIST(any_t)*) &a->list_value : NULL;
}

// copies 'list' if present, returns ptr to internal dynlist
DYNLIST(any_t) *any_set_list(any_t *a, const DYNLIST(any_t) *list);

// moves 'list' if present, returns ptr to internal dynlist
DYNLIST(any_t) *any_move_list(any_t *a, DYNLIST(any_t) *list);

// gets ANY numeric value as an f64
M_INLINE bool any_get_as_f64(const any_t *a, f64 *out) {
    switch (a->type) {
    case ANY_INT:  *out = a->int_value;  return true;
    case ANY_UINT: *out = a->uint_value; return true;
    case ANY_U8:   *out = a->u8_value;   return true;
    case ANY_U16:  *out = a->u16_value;  return true;
    case ANY_U32:  *out = a->u32_value;  return true;
    case ANY_U64:  *out = a->u64_value;  return true;
    case ANY_I8:   *out = a->i8_value;   return true;
    case ANY_I16:  *out = a->i16_value;  return true;
    case ANY_I32:  *out = a->i32_value;  return true;
    case ANY_I64:  *out = a->i64_value;  return true;
    case ANY_F32:  *out = a->f32_value;  return true;
    case ANY_F64:  *out = a->f64_value;  return true;
    case ANY_BOOL: *out = a->bool_value; return true;
    case ANY_CHAR: *out = a->char_value; return true;
    case ANY_NONE: *out = 0;             return true;
    default: return false;
    }
}

// gets ANY numeric value as an f64 or default
M_INLINE f64 any_get_as_f64_or_default(const any_t *a, f64 _default) {
    f64 value;
    return any_get_as_f64(a, &value) ? value : _default;
}

// gets ANY numeric value as an f64 or default
M_INLINE f64 any_get_as_f64_or_die(const any_t *a) {
    f64 value;
    ASSERT(any_get_as_f64(a, &value));
    return value;
}

// true if same type and equal values
bool any_eq(const any_t *a, const any_t *b);

void any_copy(any_t *dst, const any_t *src);

void any_set_fmt(any_t *a, const char *fmt, ...);

void any_set_vfmt(any_t *a, const char *fmt, va_list args);

// for map
typedef struct map map_t;
M_INLINE void map_any_free(map_t*, void *p) { any_destroy(p); }

// gets size required to serialize any
int any_serialize_size(const any_t *any);

// serializes an any, returns number of bytes read, <= 0 on error
int any_serialize(const any_t *a, const range_t *data);

// deserializes an any, returns number of bytes written, <= 0 on error
int any_deserialize(any_t *a, const range_t *data);

// tries to massage any into target type
// returns false on failure
bool any_try_massage_type(any_t *dst, const any_t *src, any_type_e type);

// any -> string
// WARN: uses tlscratch
char *any_to_str(const any_t *a, allocator_t *al);

// any -> json string
// WARN: uses tlscratch
char *any_to_json(const any_t *a, allocator_t *al);

// json string -> any, returns next character in view following value
const char *any_from_json(
    any_t *dst, const any_type_e *opt_type, const str_view_t *view);

// compute hash for any
hash_t any_hash(const any_t *a);

// allocated memory footprint of this any (in bytes)
// 0 if any has no allocated data
int any_footprint(const any_t *a);

#ifdef UTIL_IMPL

#include "util/hash.h"
#include "util/bytebuf.h"
#include "util/kvstore.h"
#include "util/str.h"

ENUM_DEFINE(any_type, ANY, ENUM_ANY_TYPE)

void any_set_fmt(any_t *a, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    any_set_vfmt(a, fmt, args);
    va_end(args);
}

void any_set_vfmt(any_t *a, const char *fmt, va_list args) {
    char *dst;
    vasprintf(&dst, fmt, args);
    any_set_str(a, dst);
    free(dst);
}

int any_serialize_size(const any_t *any) {
    switch (any->type) {
    case ANY_NONE:  return 1;
    case ANY_INT:   return 1 + sizeof(any->int_value);
    case ANY_UINT:  return 1 + sizeof(any->uint_value);
    case ANY_U8:    return 1 + sizeof(any->u8_value);
    case ANY_U16:   return 1 + sizeof(any->u16_value);
    case ANY_U32:   return 1 + sizeof(any->u32_value);
    case ANY_U64:   return 1 + sizeof(any->u64_value);
    case ANY_I8:    return 1 + sizeof(any->i8_value);
    case ANY_I16:   return 1 + sizeof(any->i16_value);
    case ANY_I32:   return 1 + sizeof(any->i32_value);
    case ANY_I64:   return 1 + sizeof(any->i64_value);
    case ANY_F32:   return 1 + sizeof(any->f32_value);
    case ANY_F64:   return 1 + sizeof(any->f64_value);
    case ANY_BOOL:  return 1 + sizeof(any->bool_value);
    case ANY_V2:    return 1 + sizeof(any->v2_value);
    case ANY_V3:    return 1 + sizeof(any->v2_value);
    case ANY_V4:    return 1 + sizeof(any->v2_value);
    case ANY_V2I:   return 1 + sizeof(any->v2i_value);
    case ANY_CHAR:  return 1 + sizeof(any->char_value);
    case ANY_STR:   return 1 + str_view_len(&any->str_value) + 1;
    case ANY_BYTES: return 1 + sizeof(int) + any->bytes_value.n;
    case ANY_KVSTORE: return 1 + kvstore_serialize_size(&any->kvstore_value);
    default: unreachable();
    }
}

static const int offsets[ANY_COUNT] = {
    [ANY_INT]  = offsetof(any_t, int_value),
    [ANY_UINT] = offsetof(any_t, uint_value),
    [ANY_U8]   = offsetof(any_t, u8_value),
    [ANY_U16]  = offsetof(any_t, u16_value),
    [ANY_U32]  = offsetof(any_t, u32_value),
    [ANY_U64]  = offsetof(any_t, u64_value),
    [ANY_I8]   = offsetof(any_t, i8_value),
    [ANY_I16]  = offsetof(any_t, i16_value),
    [ANY_I32]  = offsetof(any_t, i32_value),
    [ANY_I64]  = offsetof(any_t, i64_value),
    [ANY_F32]  = offsetof(any_t, f32_value),
    [ANY_F64]  = offsetof(any_t, f64_value),
    [ANY_BOOL] = offsetof(any_t, bool_value),
    [ANY_V2]   = offsetof(any_t, v2_value),
    [ANY_V3]   = offsetof(any_t, v3_value),
    [ANY_V4]   = offsetof(any_t, v4_value),
    [ANY_V2I]  = offsetof(any_t, v2i_value),
    [ANY_CHAR] = offsetof(any_t, char_value),
};

static const int sizes[ANY_COUNT] = {
    [ANY_INT]  = sizeof_field(any_t, int_value),
    [ANY_UINT] = sizeof_field(any_t, uint_value),
    [ANY_U8]   = sizeof_field(any_t, u8_value),
    [ANY_U16]  = sizeof_field(any_t, u16_value),
    [ANY_U32]  = sizeof_field(any_t, u32_value),
    [ANY_U64]  = sizeof_field(any_t, u64_value),
    [ANY_I8]   = sizeof_field(any_t, i8_value),
    [ANY_I16]  = sizeof_field(any_t, i16_value),
    [ANY_I32]  = sizeof_field(any_t, i32_value),
    [ANY_I64]  = sizeof_field(any_t, i64_value),
    [ANY_F32]  = sizeof_field(any_t, f32_value),
    [ANY_F64]  = sizeof_field(any_t, f64_value),
    [ANY_BOOL] = sizeof_field(any_t, bool_value),
    [ANY_V2]   = sizeof_field(any_t, v2_value),
    [ANY_V3]   = sizeof_field(any_t, v3_value),
    [ANY_V4]   = sizeof_field(any_t, v4_value),
    [ANY_V2I]  = sizeof_field(any_t, v2i_value),
    [ANY_CHAR] = sizeof_field(any_t, char_value),
};

int any_serialize(const any_t *a, const range_t *data) {
    const int expected_size = any_serialize_size(a);
    ASSERT((int) data->size >= expected_size);

    int res;
    bytebuf_t buf = bytebuf_wrap(data);

    ASSERT(bytebuf_write_u8(&buf, a->type) > 0);

    switch (a->type) {
    case ANY_KVSTORE:;
        const int sz = kvstore_serialize_size(&a->kvstore_value);
        const range_t range = bytebuf_remaining_as_range(&buf);
        ASSERT(kvstore_serialize(&a->kvstore_value, &range) == sz);
        bytebuf_skip(&buf, sz);
        break;
    case ANY_BYTES:
        ASSERT(
            bytebuf_write_u8_array_var(
                &buf,
                &(range_t) { a->bytes_value.arr, a->bytes_value.n },
                a->bytes_value.n) > 0);
        break;
    case ANY_STR:
        ASSERT(
            (res = bytebuf_write_str(&buf, a->str_value.start) > 0), "%d", res);
        break;
    case ANY_U16:
    case ANY_I16:
        ASSERT(
            bytebuf_write_u16(
                &buf, *(u16*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_F32:
    case ANY_INT:
    case ANY_UINT:
    case ANY_U32:
    case ANY_I32:
        ASSERT(
            bytebuf_write_u32(
                &buf, *(u32*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_F64:
    case ANY_U64:
    case ANY_I64:
        ASSERT(
            bytebuf_write_u64(
                &buf, *(u64*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_V2:
    case ANY_V2I:
        ASSERT(
            bytebuf_write_v2i(
                &buf, *(v2i*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_V3:
        ASSERT(
            bytebuf_write_v3(
                &buf, *(v3*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_V4:
        ASSERT(
            bytebuf_write_v4(
                &buf, *(v4*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_U8:
    case ANY_I8:
    case ANY_BOOL:
    case ANY_CHAR:
        ASSERT(
            bytebuf_write_u8(
                &buf, *(u8*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_NONE: break;
    default: unreachable();
    }

    ASSERT(bytebuf_tell(&buf) == expected_size);
    return expected_size;
}

int any_deserialize(any_t *a, const range_t *data) {
    if (data->size < 1) { goto fail; }
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);

    bytebuf_t buf = bytebuf_wrap(data);

    u8 type;
    ASSERT(bytebuf_read_u8(&buf, &type) > 0);
    a->type = type;

    switch (a->type) {
    case ANY_KVSTORE: {
        ASSERT(a->allocator);
        kvstore_init(&a->kvstore_value, a->allocator);
        const range_t range = bytebuf_remaining_as_range(&buf);
        ASSERT(kvstore_deserialize(&a->kvstore_value, &range) > 0);
    } break;
    case ANY_BYTES: {
        ASSERT(a->allocator);
        range_t range;
        const int res =
            bytebuf_read_u8_array_var_alloc(&buf, a->allocator, &range);
        ASSERT(res > 0, "%d", res);
        a->bytes_value.arr = range.ptr;
        a->bytes_value.n = range.size;
    } break;
    case ANY_STR:
        ASSERT(a->allocator);
        char *str;
        ASSERT(
            bytebuf_read_str(&buf, a->allocator, &str) > 0);
        a->str_value = str_view_from(str);
        break;
    case ANY_U16:
    case ANY_I16:
        ASSERT(
            bytebuf_read_u16(&buf, (u16*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_F32:
    case ANY_UINT:
    case ANY_INT:
    case ANY_U32:
    case ANY_I32:
        ASSERT(
            bytebuf_read_u32(&buf, (u32*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_F64:
    case ANY_U64:
    case ANY_I64:
        ASSERT(
            bytebuf_read_u64(&buf, (u64*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_V2:
    case ANY_V2I:
        ASSERT(
            bytebuf_read_v2i(
                &buf, (v2i*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_V3:
        ASSERT(
            bytebuf_read_v3(
                &buf, (v3*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_V4:
        ASSERT(
            bytebuf_read_v4(
                &buf, (v4*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_BOOL:
    case ANY_I8:
    case ANY_U8:
    case ANY_CHAR:
        ASSERT(
            bytebuf_read_u8(
                &buf, (u8*) (((u8*) a) + offsets[a->type])) > 0);
        break;
    case ANY_NONE: break;
    default: unreachable();
    }

    return any_serialize_size(a);
fail:
    return -1;
}

bool any_try_massage_type(any_t *dst, const any_t *src, any_type_e type) {
    // no conversion necessary
    if (src->type == type) {
        any_copy(dst, src);
        return true;
    }

    // if allocating type but dst has no allocator, fail
    switch (type) {
    case ANY_STR:
    case ANY_CHAR:
    case ANY_BYTES:
    case ANY_KVSTORE:
    case ANY_LIST:
        if (!dst->allocator) {
            ERROR(
                "cannot massage %s into %s, no allocator on dst",
                any_type_to_str(src->type),
                any_type_to_str(type));
            return false;
        }
    default:
    }

    // no conversions from none
    if (src->type == ANY_NONE) {
        return false;
    }

    if (src->type == ANY_V2 && type == ANY_V2I) {
        // v2 -> v2i
        any_set_v2i(dst, v2i_of(src->v2_value.x, src->v2_value.y));
        return true;
    } else if (src->type == ANY_V2I && type == ANY_V2) {
        // v2i -> v2
        any_set_v2(dst, v2_of(src->v2i_value.x, src->v2i_value.y));
        return true;
    }

    if (src->type == ANY_LIST && type == ANY_BYTES) {
        // list -> bytes
        DYNLIST(any_t) *list = any_get_list(src);
        ASSERT(list);

        // preallocate for expected size
        u8 *data = mem_alloc(dst->allocator, dynlist_size(*list));

        dynlist_each(*list, it) {
            any_t byte = any_create(NULL);
            if (!any_try_massage_type(&byte, it.el, ANY_U8)) {
                // fail
                mem_free(dst->allocator, data);
                return false;
            }

            ASSERT_DEBUG(any_is(&byte, ANY_U8));
            data[it.i] = byte.u8_value;
        }

        // ok since "data" is on dst->allocator
        any_move_bytes(
            dst,
            &(range_t) { .ptr = data, .size = dynlist_size(*list) });

        return true;
    } else if (src->type == ANY_BYTES && type == ANY_LIST) {
        // bytes -> list
        DYNLIST(any_t) *list = any_set_list(dst, NULL);
        for (int i = 0; i < src->bytes_value.n; i++) {
            any_t byte = any_create(dst->allocator);
            any_set_u8(&byte, src->bytes_value.arr[i]);
            *dynlist_push(*list) = byte;
        }
        return true;
    }

    // list -> vector types
    if (src->type == ANY_LIST) {
        bool f;
        uint n;
        f32 fs[4];
        i32 xs[4];

        switch (type) {
        case ANY_V2:  f = true;  n = 2; break;
        case ANY_V3:  f = true;  n = 3; break;
        case ANY_V4:  f = true;  n = 4; break;
        case ANY_V2I: f = false; n = 2; break;
        default: return false;
        }

        if (dynlist_size(src->list_value) != (int) n) { return false; }

        dynlist_each(src->list_value, it) {
            any_t copy = any_create(NULL);
            if (!any_try_massage_type(&copy, it.el, f ? ANY_F32 : ANY_I32)) {
                return false;
            }

            if (f) {
                if (!any_get_f32(&copy, &fs[it.i])) { return false; }
            } else  {
                if (!any_get_i32(&copy, &xs[it.i])) { return false; }
            }
        }

        switch (type) {
        case ANY_V2:
            any_set_v2(dst, v2_of(fs[0], fs[1]));
            break;
        case ANY_V3:
            any_set_v3(dst, v3_of(fs[0], fs[1], fs[2]));
            break;
        case ANY_V4:
            any_set_v4(dst, v4_of(fs[0], fs[1], fs[2], fs[3]));
            break;
        case ANY_V2I:
            any_set_v2i(dst, v2i_of(xs[0], xs[1]));
            break;
        default: ASSERT(false);
        }

        return true;
    }

    switch (src->type) {
    case ANY_BYTES:
    case ANY_STR:
    case ANY_V2:
    case ANY_V3:
    case ANY_V4:
    case ANY_V2I:
    case ANY_NONE:
        return false;
    default:
    }

    if (type == ANY_F64 || type == ANY_F32) {
        f64 val;
        if (!any_get_as_f64(src, &val)) {
             return false;
        }

        if (type == ANY_F64) {
            any_set_f64(dst, val);
            return true;
        } else if (type == ANY_F32) {
            any_set_f32(dst, val);
            return true;
        }

        return false;
    }

    // integer types

    i64 val;
    switch (src->type) {
    case ANY_INT:  val = any_get_int_or_default(src,  0); break;
    case ANY_UINT: val = any_get_uint_or_default(src, 0); break;
    case ANY_U8:   val = any_get_u8_or_default(src,   0); break;
    case ANY_U16:  val = any_get_u16_or_default(src,  0); break;
    case ANY_U32:  val = any_get_u32_or_default(src,  0); break;
    case ANY_U64:  val = any_get_u64_or_default(src,  0); break;
    case ANY_I8:   val = any_get_i8_or_default(src,   0); break;
    case ANY_I16:  val = any_get_i16_or_default(src,  0); break;
    case ANY_I32:  val = any_get_i32_or_default(src,  0); break;
    case ANY_I64:  val = any_get_i64_or_default(src,  0); break;
    case ANY_BOOL: val = any_get_bool_or_default(src, 0); break;
    case ANY_CHAR: val = any_get_char_or_default(src, 0); break;
    default: ASSERT(false);
    }

    switch (type) {
    case ANY_INT:  any_set_int(dst,  (int)  val); break;
    case ANY_UINT: any_set_uint(dst, (uint) val); break;
    case ANY_U8:   any_set_u8(dst,   (u8)   val); break;
    case ANY_U16:  any_set_u16(dst,  (u16)  val); break;
    case ANY_U32:  any_set_u32(dst,  (u32)  val); break;
    case ANY_U64:  any_set_u64(dst,  (u64)  val); break;
    case ANY_I8:   any_set_i8(dst,   (i8)   val); break;
    case ANY_I16:  any_set_i16(dst,  (i16)  val); break;
    case ANY_I32:  any_set_i32(dst,  (i32)  val); break;
    case ANY_I64:  any_set_i64(dst,  (i64)  val); break;
    case ANY_BOOL: any_set_bool(dst, (bool) val); break;
    case ANY_CHAR: any_set_char(dst, (char) val); break;
    default: ASSERT(false);
    }

    return true;
}

range_t any_get_as_bytes(const any_t *a, allocator_t *al) {
    if (any_is(a, ANY_BYTES)) {
        if (a->allocator == al) {
            return (range_t) {
                .ptr = a->bytes_value.arr,
                .size = a->bytes_value.n,
            };
        } else {
            range_t range = mem_alloc_range(al, a->bytes_value.n);
            memcpy(range.ptr, a->bytes_value.arr, a->bytes_value.n);
            return range;
        }
    }

    any_t bytes = any_create(al);
    if (!any_try_massage_type(&bytes, a, ANY_BYTES)) {
        any_destroy(&bytes);
        return (range_t) { 0 };
    }

    // don't need to destroy "bytes", all it has allocated is the range
    return any_get_bytes(&bytes);
}

kvstore_t *any_set_kvstore(any_t *a, const kvstore_t *kvs) {
    ASSERT(a->allocator);
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);
    a->type = ANY_KVSTORE;
    kvstore_init(&a->kvstore_value, allocator);

    if (kvs) {
        kvstore_copy(&a->kvstore_value, kvs);
    }

    return &a->kvstore_value;
}

kvstore_t *any_move_kvstore(any_t *a, kvstore_t *kvs) {
    ASSERT(a->allocator);
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);
    a->type = ANY_KVSTORE;
    kvstore_init(&a->kvstore_value, allocator);

    if (kvs) {
        a->kvstore_value = kvstore_move(kvs, allocator);
    }

    return &a->kvstore_value;
}

DYNLIST(any_t) *any_set_list(any_t *a, const DYNLIST(any_t) *list) {
    ASSERT(a->allocator);
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);
    a->type = ANY_LIST;
    a->list_value = dynlist_create(any_t, a->allocator);

    if (list) {
        dynlist_push_all(a->list_value, *list);
    }

    return &a->list_value;
}

DYNLIST(any_t) *any_move_list(any_t *a, DYNLIST(any_t) *list) {
    ASSERT(a->allocator);
    allocator_t *allocator = a->allocator;
    any_destroy(a);
    any_init(a, allocator);
    a->type = ANY_LIST;
    a->list_value = dynlist_create(any_t, a->allocator);

    if (list) {
        // destructive move
        dynlist_each(*list, it) {
            *dynlist_push(a->list_value) = any_move(it.el, a->allocator);
        }
        dynlist_destroy(*list);
        *list = NULL;
    }

    return &a->list_value;
}

bool any_eq(const any_t *a, const any_t *b) {
    if (a->type != b->type) { return false; }

    switch (a->type) {
    case ANY_NONE:  return true;
    case ANY_INT: return a->int_value == b->int_value;
    case ANY_UINT: return a->uint_value == b->uint_value;
    case ANY_U8: return a->u8_value == b->u8_value;
    case ANY_U16: return a->u16_value == b->u16_value;
    case ANY_U32: return a->u32_value == b->u32_value;
    case ANY_U64: return a->u64_value == b->u64_value;
    case ANY_I8: return a->i8_value == b->i8_value;
    case ANY_I16: return a->i16_value == b->i16_value;
    case ANY_I32: return a->i32_value == b->i32_value;
    case ANY_I64: return a->i64_value == b->i64_value;
    case ANY_F32: return a->f32_value == b->f32_value;
    case ANY_F64: return a->f64_value == b->f64_value;
    case ANY_BOOL: return a->bool_value == b->bool_value;
    case ANY_V2: return v2_eqv(a->v2_value, b->v2_value);
    case ANY_V3: return v3_eqv(a->v3_value, b->v3_value);
    case ANY_V4: return v4_eqv(a->v4_value, b->v4_value);
    case ANY_V2I: return v2i_eqv(a->v2i_value, b->v2i_value);
    case ANY_CHAR:  return a->char_value == b->char_value;
    case ANY_STR:   return !str_view_cmp(&a->str_value, &b->str_value);
    case ANY_BYTES:
        return a->bytes_value.n == b->bytes_value.n
            && !memcmp(
                a->bytes_value.arr,
                b->bytes_value.arr,
                a->bytes_value.n);
    case ANY_KVSTORE:
        return kvstore_eq(&a->kvstore_value, &b->kvstore_value);
    case ANY_LIST:
        if (dynlist_size(a->list_value) != dynlist_size(b->list_value)) {
            return false;
        }

        for (uint i = 0, n = dynlist_size(a->list_value); i < n; i++) {
            if (!any_eq(&a->list_value[i], &b->list_value[i])) {
                return false;
            }
        }

        return true;
    default: unreachable();
    }
}

void any_copy(any_t *dst, const any_t *src) {
    allocator_t *allocator = dst->allocator;
    any_destroy(dst);
    dst->allocator = allocator;
    dst->type = src->type;

    switch (src->type) {
    case ANY_KVSTORE:
        ASSERT(allocator);
        kvstore_init(&dst->kvstore_value, allocator);
        kvstore_copy(&dst->kvstore_value, &src->kvstore_value);
        break;
    case ANY_LIST:
        ASSERT(allocator);
        dst->list_value = dynlist_create(any_t, dst->allocator);
        dynlist_push_all(dst->list_value, src->list_value);
        break;
    case ANY_BYTES: {
        ASSERT(allocator);
        dst->bytes_value.n = src->bytes_value.n;
        dst->bytes_value.arr =
            mem_alloc_inplace(
                dst->allocator,
                src->bytes_value.n,
                src->bytes_value.arr);
    } break;
    case ANY_STR: {
        ASSERT(allocator);
        dst->str_value = str_view_dup(&src->str_value, dst->allocator);
    } break;
    case ANY_INT:
    case ANY_UINT:
    case ANY_U8:
    case ANY_U16:
    case ANY_U32:
    case ANY_U64:
    case ANY_I8:
    case ANY_I16:
    case ANY_I32:
    case ANY_I64:
    case ANY_F32:
    case ANY_F64:
    case ANY_BOOL:
    case ANY_V2:
    case ANY_V3:
    case ANY_V4:
    case ANY_V2I:
    case ANY_CHAR: {
        // optimization opportunity: don't use memcpy here, just assign
        const u8 *psrc = ((u8*) src) + offsets[src->type];
        u8 *pdst = ((u8*) dst) + offsets[dst->type];
        memcpy(pdst, psrc, sizes[dst->type]);
    } break;
    case ANY_NONE: break;
    default: unreachable();
    }
}

hash_t any_hash(const any_t *a) {
    hash_t h = 0x12345;

    switch (a->type) {
    case ANY_INT:  h = hash_add_int(h,  a->int_value);       break;
    case ANY_UINT: h = hash_add_u32(h,  a->uint_value);      break;
    case ANY_U8:   h = hash_add_u8(h,   a->u8_value);        break;
    case ANY_U16:  h = hash_add_u16(h,  a->u16_value);       break;
    case ANY_U32:  h = hash_add_u32(h,  a->u32_value);       break;
    case ANY_U64:  h = hash_add_u64(h,  a->u64_value);       break;
    case ANY_I8:   h = hash_add_i8(h,   a->i8_value);        break;
    case ANY_I16:  h = hash_add_i16(h,  a->i16_value);       break;
    case ANY_I32:  h = hash_add_i32(h,  a->i32_value);       break;
    case ANY_I64:  h = hash_add_i64(h,  a->i64_value);       break;
    case ANY_F32:  h = hash_add_f32(h,  a->f32_value);       break;
    case ANY_F64:  h = hash_add_f64(h,  a->f64_value);       break;
    case ANY_V2:   h = hash_add_v2(h,   a->v2_value);        break;
    case ANY_V3:   h = hash_add_v2(h,   a->v2_value);        break;
    case ANY_V4:   h = hash_add_v4(h,   a->v4_value);        break;
    case ANY_V2I:  h = hash_add_v2i(h,  a->v2i_value);       break;
    case ANY_CHAR: h = hash_add_char(h, a->char_value);      break;
    case ANY_STR:  h = hash_add_str(h,  a->str_value.start); break;
    case ANY_BOOL: h = hash_add_u8(h,   a->bool_value);      break;
    case ANY_BYTES:
        h = hash_add_bytes(h, a->bytes_value.arr, a->bytes_value.n);
        break;
    case ANY_KVSTORE:
        h = hash_combine(h, kvstore_hash(&a->kvstore_value));
        break;
    case ANY_LIST:
        h = hash_add_u8(h, ANY_LIST);
        dynlist_each(a->list_value, it) {
            h = hash_combine(h, any_hash(it.el));
        }
        break;
    case ANY_NONE:
        break;
    default:
        unreachable();
    }

    return h;
}

char *any_to_str(const any_t *a, allocator_t *al) {
    switch (a->type) {
    case ANY_INT:   return mem_strfmt(al, "%"   PRIi32, a->int_value);
    case ANY_UINT:  return mem_strfmt(al, "%"   PRIu32, a->uint_value);
    case ANY_U8:    return mem_strfmt(al, "%"   PRIu8,  a->u8_value);
    case ANY_U16:   return mem_strfmt(al, "%"   PRIu16, a->u16_value);
    case ANY_U32:   return mem_strfmt(al, "%"   PRIu32, a->u32_value);
    case ANY_U64:   return mem_strfmt(al, "%"   PRIu64, a->u64_value);
    case ANY_I8:    return mem_strfmt(al, "%"   PRIi8,  a->i8_value);
    case ANY_I16:   return mem_strfmt(al, "%"   PRIi16, a->i16_value);
    case ANY_I32:   return mem_strfmt(al, "%"   PRIi32, a->i32_value);
    case ANY_I64:   return mem_strfmt(al, "%"   PRIi64, a->i64_value);
    case ANY_F32:   return mem_strfmt(al, "%g", (f64) a->f32_value);
    case ANY_F64:   return mem_strfmt(al, "%g", (f64) a->f64_value);
    case ANY_V2:    return mem_strfmt(al, "%"   PRIv2,  FMTv2(a->v2_value));
    case ANY_V3:    return mem_strfmt(al, "%"   PRIv2,  FMTv2(a->v2_value));
    case ANY_V4:    return mem_strfmt(al, "%"   PRIv4,  FMTv4(a->v4_value));
    case ANY_V2I:   return mem_strfmt(al, "%"   PRIv2i, FMTv2i(a->v2i_value));
    case ANY_CHAR:  return mem_strfmt(al, "%c", a->char_value);
    case ANY_STR:   return mem_strfmt(al, "%s", a->str_value.start);
    case ANY_BOOL:
        return mem_strfmt(al, "%s", a->bool_value ? "true" : "false");
    case ANY_BYTES:;
        DYNLIST(char) chars =
            dynlist_create(
                char,
                tlscratch(),
                2 + (a->bytes_value.n * 5) + 1);

        *dynlist_push(chars) = '[';
        for (int i = 0; i < a->bytes_value.n; i++) {
            char hex[8];
            const int res =
                snprintf(hex, sizeof(hex), "0x%02X", a->bytes_value.arr[i]);
            ASSERT(res == 4);

            const int offset = dynlist_size(chars);
            dynlist_resize_no_contract(chars, offset + 4);
            memcpy(&chars[offset], hex, 4);

            if (i != a->bytes_value.n - 1) { *dynlist_push(chars) = ','; }
        }
        *dynlist_push(chars) = ']';
        *dynlist_push(chars) = '\0';
        return mem_strdup(al, chars);
    case ANY_KVSTORE:
        return kvstore_to_str(&a->kvstore_value, al);
    case ANY_LIST:;
        strbuf_t buf = strbuf_create(tlscratch());
        strbuf_ap_ch(&buf, '[');
        dynlist_each(a->list_value, it) {
            strbuf_ap_fmt(
                &buf,
                "%s%s",
                any_to_str(it.el, tlscratch()),
                it.i == dynlist_size(a->list_value) - 1 ? "" : ", ");
        }
        strbuf_ap_ch(&buf, ']');
        return strbuf_dump(&buf, al);
    case ANY_NONE: return mem_strdup(al, "none");
    default: unreachable();
    }
}

char *any_to_json(const any_t *a, allocator_t *al) {
    switch (a->type) {
    case ANY_INT:
    case ANY_UINT:
    case ANY_U8:
    case ANY_U16:
    case ANY_U32:
    case ANY_U64:
    case ANY_I8:
    case ANY_I16:
    case ANY_I32:
    case ANY_I64:
    case ANY_F32:
    case ANY_F64:
    case ANY_BOOL:
        return any_to_str(a, al);
    case ANY_V2:
        return mem_strfmt(al, "[%g, %g]", a->v2_value.x, a->v2_value.y);
    case ANY_V3:
        return
            mem_strfmt(
                al,
                "[%g, %g, %g]",
                a->v3_value.x,
                a->v3_value.y,
                a->v3_value.z);
    case ANY_V4:
        return
            mem_strfmt(
                al,
                "[%g, %g, %g, %g]",
                a->v4_value.x,
                a->v4_value.y,
                a->v4_value.z,
                a->v4_value.w);
    case ANY_V2I:
        return mem_strfmt(al, "[%d, %d]", a->v2i_value.x, a->v2i_value.y);
    case ANY_CHAR: {
        char str[2];
        str[0] = a->char_value;
        str[1] = '\0';
        const str_view_t escaped =
            str_view_escape(
                &(str_view_t) { &str[0], &str[1] },
                tlscratch());
        return mem_strfmt(al, "\"%s\"", str_view_dump(&escaped, tlscratch()));
    } break;
    case ANY_STR: {
        const str_view_t escaped =
            str_view_escape(
                &a->str_value,
                tlscratch());
        return mem_strfmt(al, "\"%s\"", str_view_dump(&escaped, tlscratch()));
    } break;
    case ANY_BYTES: {
        strbuf_t buf = strbuf_create(tlscratch());
        strbuf_ap_ch(&buf, '[');
        for (int i = 0; i < a->bytes_value.n; i++) {
            strbuf_ap_fmt(
                &buf, "%" PRIu8 "%s",
                a->bytes_value.arr[i],
                i == a->bytes_value.n - 1 ? "" : ",");
        }
        strbuf_ap_ch(&buf, ']');
        return strbuf_dump(&buf, al);
    } break;
    case ANY_KVSTORE:
        return kvstore_to_json(&a->kvstore_value, al);
    case ANY_LIST: {
        strbuf_t buf = strbuf_create(tlscratch());
        strbuf_ap_ch(&buf, '[');
        dynlist_each(a->list_value, it) {
            strbuf_ap_fmt(
                &buf,
                "%s%s",
                any_to_json(it.el, tlscratch()),
                it.i == dynlist_size(a->list_value) - 1 ? "" : ",");
        }
        strbuf_ap_ch(&buf, ']');
        return strbuf_dump(&buf, al);
    } break;
    case ANY_NONE: return mem_strdup(al, "null");
    default: unreachable();
    }
}

const char *any_from_json(
    any_t *dst, const any_type_e *opt_type, const str_view_t *view) {
    const char *p = view->start;
    ASSERT(dst->allocator);

    // set errmsg and goto fail for error
    const char *errmsg = NULL;

    // seek to first non-space character
    while (isspace(*p)) { p++; }

    if (*p == '[') {
        // list
        any_set_list(dst, NULL);

        // skip entry square bracket
        p++;

        while (*p && *p != ']') {
            if (isspace(*p)) {
                p++;
            } else {
                any_t el = any_create(dst->allocator);

                const char *old = p;
                p = any_from_json(&el, NULL, &(str_view_t) { p, view->end });

                if (!p || p <= old) {
                    errmsg = "error parsing list";
                    goto fail;
                }

                *dynlist_push(dst->list_value) = el;

                // seek to comma or end of list
                while (isspace(*p)) { p++; }

                // if comma (could also be end), continue
                if (*p == ',') { p++; }
            }
        }

        if (!*p) {
            errmsg = "unterminated list";
            goto fail;
        }

        ASSERT(*p == ']');
        return p + 1;
    } else if (*p == '{') {
        // kvstore
        any_set_kvstore(dst, NULL);

        const char *old = p;
        p =
            kvstore_from_json(
                &dst->kvstore_value,
                &(str_view_t) { p, view->end });

        if (!p || p <= old) {
            errmsg = "error parsing kvstore";
            goto fail;
        }

        return p;
    } else if (*p == '\"' || *p == '\'') {
        // string
        if (opt_type && *opt_type != ANY_CHAR && *opt_type != ANY_STR) {
            errmsg =
                mem_strfmt(
                    tlscratch(),
                    "json is object but opt_type is %s",
                    any_type_to_str(*opt_type));
            goto fail;
        }

        if (p + 1 >= view->end) {
            errmsg =
                mem_strfmt(
                    tlscratch(),
                    "malformatted string \"%s\"",
                    str_view_dump(view, tlscratch()));
            goto fail;
        }

        // accumulate view
        char quote = *p;
        const char *start = p + 1;

        // skip entry quote
        p++;

        // seek to end quote
        while (*p && !(*p == quote && *(p - 1) != '\\')) { p++; }

        if (!*p) {
            WARN(
                "unterminated string \"%s\"",
                str_view_dump(view, tlscratch()));
            return NULL;
        }

        // convert potentially escaped characters, allocating extra string
        // on tlscratch if necessary
        const str_view_t view =
            str_view_unescape(
                &(str_view_t) { start, p },
                tlscratch());

        if (opt_type && *opt_type == ANY_CHAR) {
            if (str_view_len(&view) != 1) {
                errmsg = "json is string but opt_type is char";
                goto fail;
            }

            any_set_char(dst, view.start[0]);
        } else {
            any_set_str_from_view(dst, &view);
        }

        return p + 1;
    }

    if (str_is_prefixed_by(view->start, "true")) {
        any_set_bool(dst, true);
        return view->start + 4;
    } else if (str_is_prefixed_by(view->start, "false")) {
        any_set_bool(dst, false);
        return view->start + 5;
    } else if (str_is_prefixed_by(view->start, "null")) {
        any_set_none(dst);
        return view->start + 4;
    }

    // try number
    char *endptr;
    const f64 d = strtod(view->start, &endptr);

    if (endptr != view->start) {
        f64 int_part;
        const f64 fract = modf(d, &int_part);

        any_type_e type;

        if (opt_type) {
            type = *opt_type;
        } else if (fract == 0.0) {
            type = ANY_I64;
        } else {
            type = ANY_F64;
        }

        i64 mi, ma;
        switch (type) {
        case ANY_INT:  mi = INT_MIN; ma = INT_MAX;  break;
        case ANY_UINT: mi = 0;       ma = UINT_MAX; break;
        case ANY_U8:   mi = 0;       ma = U8_MAX;   break;
        case ANY_U16:  mi = 0;       ma = U16_MAX;  break;
        case ANY_U32:  mi = 0;       ma = U32_MAX;  break;
        case ANY_U64:  mi = 0;       ma = U64_MAX;  break;
        case ANY_I8:   mi = I8_MIN;  ma = I8_MAX;   break;
        case ANY_I16:  mi = I16_MIN; ma = I16_MAX;  break;
        case ANY_I32:  mi = I32_MIN; ma = I32_MAX;  break;
        case ANY_I64:  mi = I64_MIN; ma = I64_MAX;  break;
        case ANY_F32:
            any_set_f32(dst, d);
            return endptr;
        case ANY_F64:
            any_set_f64(dst, d);
            return endptr;
        default:
            WARN(
                "bad target type %s json value \"%s\"",
                any_type_to_str(type),
                str_view_dump(view, tlscratch()));
            return endptr;
        }

        // store integer, will it get truncated?
        const i64 i = (i64) d;
        if (i < mi || i > ma) {
            WARN(
                "storing integer value %" PRIi64 " in type %s will truncate!",
                i,
                any_type_to_str(type));
        }

        // set as integer
        switch (type) {
        case ANY_INT:  any_set_int(dst,  (int)  (i)); break;
        case ANY_UINT: any_set_uint(dst, (uint) (i)); break;
        case ANY_U8:   any_set_u8(dst,   (u8)   (i)); break;
        case ANY_U16:  any_set_u16(dst,  (u16)  (i)); break;
        case ANY_U32:  any_set_u32(dst,  (u32)  (i)); break;
        case ANY_U64:  any_set_u64(dst,  (u64)  (i)); break;
        case ANY_I8:   any_set_i8(dst,   (i8)   (i)); break;
        case ANY_I16:  any_set_i16(dst,  (i16)  (i)); break;
        case ANY_I32:  any_set_i32(dst,  (i32)  (i)); break;
        case ANY_I64:  any_set_i64(dst,  (i64)  (i)); break;
        default:
            ASSERT(false);
        }

        return endptr;
    }

    errmsg = "invalid value";
fail:
    WARN(
        "could not convert json \"%s\" to any (error %s)",
        str_view_dump_trunc(view, tlscratch(), 1024),
        errmsg ? errmsg : "");
    return NULL;
}

int any_footprint(const any_t *a) {
    int sz = 0;

    if (a->allocator) {
        switch (a->type) {
        case ANY_STR:
            if (str_view_valid(&a->str_value)) {
                sz += str_view_len(&a->str_value) + 1;
            }
            break;
        case ANY_BYTES:
            if (a->bytes_value.arr) {
                sz += a->bytes_value.n;
            }
            break;
        case ANY_KVSTORE:
            extern int kvstore_footprint(const kvstore_t*);
            sz += kvstore_footprint(&a->kvstore_value);
            break;
        case ANY_LIST:;
            dynlist_each(a->list_value, it) {
                sz += any_footprint(it.el);
            }
            sz += dynlist_size_bytes(a->list_value);
            break;
        default:
        }
    }

    return sz;
}

#endif // ifdef UTIL_IMPL
