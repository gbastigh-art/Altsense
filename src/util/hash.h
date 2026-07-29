#pragma once

#include "util/types.h"
#include "util/math/linalg.h"

#define DEFINE_HASH_ADD(T, t_name)                                             \
    M_INLINE hash_t hash_add_##t_name(hash_t hash, T x) {                      \
        return                                                                 \
            (hash ^                                                            \
                (((hash_t) (x)) + 0x9E3779B9u + (hash << 6) + (hash >> 2)));   \
    }                                                                          \
    
DEFINE_HASH_ADD(bool, bool)
DEFINE_HASH_ADD(u8, u8)
DEFINE_HASH_ADD(u16, u16)
DEFINE_HASH_ADD(u32, u32)
DEFINE_HASH_ADD(u64, u64)

DEFINE_HASH_ADD(i8, i8)
DEFINE_HASH_ADD(i16, i16)
DEFINE_HASH_ADD(i32, i32)
DEFINE_HASH_ADD(i64, i64)

DEFINE_HASH_ADD(usize, usize)
DEFINE_HASH_ADD(isize, isize)

DEFINE_HASH_ADD(char, char)
DEFINE_HASH_ADD(int, int)
DEFINE_HASH_ADD(uintptr_t, uintptr)

M_INLINE hash_t hash_combine(hash_t a, hash_t b) {
    STATIC_ASSERT(sizeof(hash_t) == sizeof(u64));
    return hash_add_u64(a, b);
}

M_INLINE hash_t hash_add_str(hash_t hash, const char *s) {
    while (*s) { hash = hash_add_char(hash, *s); s++; }
    return hash;
}

M_INLINE hash_t hash_add_f32(hash_t hash, f32 x) {
    union { f32 f; u32 i; } u = { .f = x };
    return hash_add_u32(hash, u.i);
}

M_INLINE hash_t hash_add_f64(hash_t hash, f64 x) {
    union { f64 f; u64 i; } u = { .f = x };
    return hash_add_u64(hash, u.i);
}

M_INLINE hash_t hash_add_v2(hash_t hash, v2 v) {
    hash = hash_add_f32(hash, v.x);
    hash = hash_add_f32(hash, v.y);
    return hash;
}

M_INLINE hash_t hash_add_v2i(hash_t hash, v2i v) {
    hash = hash_add_i32(hash, v.x);
    hash = hash_add_i32(hash, v.y);
    return hash;
}

M_INLINE hash_t hash_add_v3(hash_t hash, v3 v) {
    hash = hash_add_f32(hash, v.x);
    hash = hash_add_f32(hash, v.y);
    hash = hash_add_f32(hash, v.z);
    return hash;
}

M_INLINE hash_t hash_add_v4(hash_t hash, v4 v) {
    hash = hash_add_f32(hash, v.x);
    hash = hash_add_f32(hash, v.y);
    hash = hash_add_f32(hash, v.z);
    hash = hash_add_f32(hash, v.w);
    return hash;
}

#define DEFINE_HASH_ADD_N(T, t_name)                                           \
    M_INLINE hash_t hash_add_##t_name##s(hash_t hash, T *xs, int n) {          \
        for (int i = 0; i < n; i++) { hash = hash_add_##t_name(hash, xs[i]); } \
        return hash;                                                           \
    }                                                                          \

DEFINE_HASH_ADD_N(bool, bool)
DEFINE_HASH_ADD_N(u8, u8)
DEFINE_HASH_ADD_N(u16, u16)
DEFINE_HASH_ADD_N(u32, u32)
DEFINE_HASH_ADD_N(u64, u64)

DEFINE_HASH_ADD_N(i8, i8)
DEFINE_HASH_ADD_N(i16, i16)
DEFINE_HASH_ADD_N(i32, i32)
DEFINE_HASH_ADD_N(i64, i64)

DEFINE_HASH_ADD_N(usize, usize)
DEFINE_HASH_ADD_N(isize, isize)

DEFINE_HASH_ADD_N(char, char)
DEFINE_HASH_ADD_N(int, int)
DEFINE_HASH_ADD_N(uintptr_t, uintptr)

DEFINE_HASH_ADD_N(v2, v2)
DEFINE_HASH_ADD_N(v3, v3)
DEFINE_HASH_ADD_N(v4, v4)

M_INLINE hash_t hash_add_ptr(hash_t hash, void *ptr) {
    return hash_add_uintptr(hash, (uintptr_t) ptr);
}

M_INLINE hash_t hash_add_ptrs(hash_t hash, void **ptrs, int n) {
    for (int i = 0; i < n; i++) {
        hash = hash_add_ptr(hash, ptrs[i]);
    }
    return hash;
}

M_INLINE hash_t hash_add_bytes(hash_t hash, const void *ptr, int n) {
    for (int i = 0; i < n; i++) {
        hash = hash_add_u8(hash, ((u8*) ptr)[i]);
    }
    return hash;
}
