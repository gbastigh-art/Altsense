#pragma once

#include "util/types.h" /* IWYU pragma: keep */
#include "util/macros.h" /* IWYU pragma: keep */
#include "util/assert.h" /* IWYU pragma: keep */
#include "util/hash.h" /* IWYU pragma: keep */

// create an enum with auto-generated helper functions and values:
//
// values:
// *_DISTINCT: number of elements in enum list
// *_COUNT: last element + 1
// *_MASK: all elements OR'd with each other (useful for flag sets)
// *_FIRST: first element in list
// *_LAST: last element in list
//
// functions:
// *_is_valid(int): returns true if int is valid value for enum
// *_to_str(T): T -> const char*
// *_nth(int): nth value for enum
// *_nth_value(int): nth value as integer
// *_raw_index(int): opposite of nth value, given enum X, gets its occurrence
//                   index
// *_desc(): all fancy functions bundled into a struct
//
// usage:
// ENUM_<NAME>(_F, ...) <backslash>
// _F(SOMETHING, <value>, __VA_ARGS__) <backslash>
// _F(ANOTHER, <value>, __VA_ARGS__) <backslash>
//
// ENUM_DECL(type_name, PREFIX, ENUM_<NAME>, [backing type?])
#define _ENUM_FUNC_MASK(UPPER, VALUE, NAME) | (VALUE)
#define _ENUM_FUNC_DISTINCT(...) + 1
#define _ENUM_FUNC_COUNT(UPPER, VALUE, NAME) * 0) + ((VALUE)
#define _ENUM_FUNC_FIRST(UPPER, VALUE, NAME) * (VALUE)) + (0
#define _ENUM_FUNC_LAST(UPPER, VALUE, NAME) * 0) + ((VALUE)
#define _ENUM_FUNC_NAME(UPPER, VALUE, NAME) NAME##_##UPPER = (VALUE),
#define _ENUM_FUNC_STRINGS(UPPER, ...) #UPPER,
#define _ENUM_FUNC_VALUES(UPPER, VALUE, ...) (VALUE),

#define _ENUM_DECL_FUNCS(T)                                   \
    bool T##_is_valid(int value);                             \
    const char *T##_to_str(enum T##_e x);                     \
    const char *T##_value_to_str(int x);                      \
    enum T##_e T##_nth(int n);                                \
    int T##_nth_value(int n);                                 \
    int T##_raw_index(int x);                                 \
    const enum_desc_t *T##_desc();

#define _ENUM_DECL_IMPL(T, NAME, FUNC, BACKING_TYPE)                       \
    typedef enum T##_e : BACKING_TYPE {                                    \
        FUNC(_ENUM_FUNC_NAME, NAME)                                        \
    } T##_e;                                                               \
    enum : BACKING_TYPE { NAME##_DISTINCT = 0 FUNC(_ENUM_FUNC_DISTINCT) }; \
    enum : BACKING_TYPE { NAME##_MASK = 0 FUNC(_ENUM_FUNC_MASK) };         \
    enum : BACKING_TYPE { NAME##_COUNT = (0 FUNC(_ENUM_FUNC_COUNT) + 1) }; \
    enum : BACKING_TYPE { NAME##_FIRST = (1 FUNC(_ENUM_FUNC_FIRST)) };     \
    enum : BACKING_TYPE { NAME##_LAST = (0 FUNC(_ENUM_FUNC_LAST)) };       \
    _ENUM_DECL_FUNCS(T)

#define _ENUM_DECL_NO_SPECIAL_IMPL(T, NAME, FUNC, BACKING_TYPE)            \
    typedef enum T##_e : BACKING_TYPE {                                    \
        FUNC(_ENUM_FUNC_NAME, NAME)                                        \
    } T##_e;                                                               \
    _ENUM_DECL_FUNCS(T)
    
#define _ENUM_DECL4(T, NAME, FUNC, BACKING_TYPE)                           \
    _ENUM_DECL_IMPL(T, NAME, FUNC, BACKING_TYPE)

#define _ENUM_DECL3(T, NAME, FUNC) _ENUM_DECL4(T, NAME, FUNC, int)

#define ENUM_DECL(...) VMACRO(_ENUM_DECL, __VA_ARGS__)

#define _ENUM_DECL_NO_SPECIAL4(T, NAME, FUNC, BACKING_TYPE)                \
    _ENUM_DECL_NO_SPECIAL_IMPL(T, NAME, FUNC, BACKING_TYPE)

#define _ENUM_DECL_NO_SPECIAL3(T, NAME, FUNC)                              \
    _ENUM_DECL_NO_SPECIAL4(T, NAME, FUNC, int)

#define ENUM_DECL_NO_SPECIAL(...) VMACRO(_ENUM_DECL_NO_SPECIAL, __VA_ARGS__)

// packed struct of all of the above functions, get with T##_desc()
typedef struct {
    int size, count, distinct, mask;
    const char **names;
    bool (*is_valid)(int);
    const char *(*to_str)(int);
    int (*nth_value)(int);
    int (*raw_index)(int);
} enum_desc_t;

// defines some ENUM_DECL helper functions
//
// *_names:
// returns table of names in the order that they appear in the enum declaration
//
// *_is_valid:
// returns true if value is a valid enum member
//
// *_to_str:
// construct a hash table of string elements to back *_to_str functions
// hashtable is used over simple array so that sparse enums don't take up extra
// space
//
// *_nth:
// gets nth distinct value of enum
//
// *_nth_value:
// gets nth distinct value of enum as int
//
// *_raw_index:
// gets raw index in sequence of enum
#define ENUM_DEFINE(T, NAME, FUNC)                                               \
    enum { _##NAME##_DISTINCT_internal = (0 FUNC(_ENUM_FUNC_DISTINCT)) };        \
    enum { _##NAME##_COUNT_internal = (0 FUNC(_ENUM_FUNC_COUNT) + 1) };          \
    enum { _##NAME##_MASK_internal = (0 FUNC(_ENUM_FUNC_MASK)) };                \
    enum { _##NAME##_FIRST_internal = (1 FUNC(_ENUM_FUNC_FIRST)) };              \
    enum { _##NAME##_LAST_internal = (0 FUNC(_ENUM_FUNC_LAST)) };                \
    static enum T##_e T##_VALS[_##NAME##_DISTINCT_internal] =                    \
        { FUNC(_ENUM_FUNC_VALUES) };                                             \
    static const char *T##_##NAMES[_##NAME##_DISTINCT_internal] =                \
        { FUNC(_ENUM_FUNC_STRINGS) };                                            \
    bool T##_is_valid(int value) {                                               \
        for (int i = 0; i < _##NAME##_DISTINCT_internal; i++) {                  \
            if (((int) T##_VALS[i]) == value) { return true; }                   \
        }                                                                        \
        return false;                                                            \
    }                                                                            \
    const char *T##_to_str(T##_e x) {                                            \
        static bool T##_LOOKUP_INIT;                                             \
        static struct {                                                          \
            const char *s;                                                       \
            enum T##_e e;                                                        \
        } T##_LOOKUP[_##NAME##_DISTINCT_internal];                               \
        if (!T##_LOOKUP_INIT) {                                                  \
            T##_LOOKUP_INIT = true;                                              \
            for (int i = 0; i < _##NAME##_DISTINCT_internal; i++) {              \
                enum T##_e v = T##_VALS[i];                                      \
                uint pos =                                                       \
                    hash_add_bytes(                                              \
                        1,                                                       \
                        (u8*) &v, sizeof(v)) % _##NAME##_DISTINCT_internal;      \
                while (T##_LOOKUP[pos].s) {                                      \
                    pos = (pos + 1) % _##NAME##_DISTINCT_internal;               \
                }                                                                \
                T##_LOOKUP[pos].s = T##_##NAMES[i];                              \
                T##_LOOKUP[pos].e = v;                                           \
            }                                                                    \
        }                                                                        \
        uint pos =                                                               \
            hash_add_bytes(1, (u8*) &x, sizeof(x)) % _##NAME##_DISTINCT_internal;\
        while (T##_LOOKUP[pos].e != x) {                                         \
            pos = (pos + 1) % _##NAME##_DISTINCT_internal;                       \
        }                                                                        \
        return T##_LOOKUP[pos].s;                                                \
    }                                                                            \
    const char *T##_value_to_str(int x) { return T##_to_str(x); }                \
    enum T##_e T##_nth(int n) {                                                  \
        ASSERT(n >= 0 && n <= _##NAME##_DISTINCT_internal);                      \
        return T##_VALS[n];                                                      \
    }                                                                            \
    int T##_nth_value(int n) {                                                   \
        ASSERT(n >= 0 && n <= _##NAME##_DISTINCT_internal);                      \
        return (int) T##_VALS[n];                                                \
    }                                                                            \
    int T##_raw_index(int x) {                                                   \
        static bool T##_LOOKUP_INIT;                                             \
        static struct { enum T##_e e; int i; bool present; }                     \
            T##_LOOKUP[_##NAME##_DISTINCT_internal];                             \
        if (!T##_LOOKUP_INIT) {                                                  \
            T##_LOOKUP_INIT = true;                                              \
            for (int i = 0; i < _##NAME##_DISTINCT_internal; i++) {              \
                enum T##_e v = T##_VALS[i];                                      \
                uint pos =                                                       \
                    hash_add_bytes(                                              \
                        1,                                                       \
                        (u8*) &v, sizeof(v)) % _##NAME##_DISTINCT_internal;      \
                while (T##_LOOKUP[pos].present) {                                \
                    pos = (pos + 1) % _##NAME##_DISTINCT_internal;               \
                }                                                                \
                T##_LOOKUP[pos].i = i;                                           \
                T##_LOOKUP[pos].e = v;                                           \
                T##_LOOKUP[pos].present = true;                                  \
            }                                                                    \
        }                                                                        \
        const uint start =                                                       \
            hash_add_bytes(1, (u8*) &x, sizeof(x)) % _##NAME##_DISTINCT_internal;\
        uint pos = start;                                                        \
        while (((int) T##_LOOKUP[pos].e) != x) {                                 \
            pos = (pos + 1) % _##NAME##_DISTINCT_internal;                       \
            if (pos == start) {                                                  \
                return 0; }                                                      \
        }                                                                        \
        return T##_LOOKUP[pos].i;                                                \
    }                                                                            \
    const enum_desc_t *T##_desc() {                                              \
        static enum_desc_t desc;                                                 \
        if (!desc.names) {                                                       \
            desc = (enum_desc_t) {                                               \
                .size = sizeof(T##_e),                                           \
                .count = _##NAME##_COUNT_internal,                               \
                .distinct = _##NAME##_DISTINCT_internal,                         \
                .mask = _##NAME##_MASK_internal,                                 \
                .names = T##_##NAMES,                                            \
                .is_valid = T##_is_valid,                                        \
                .to_str = T##_value_to_str,                                      \
                .nth_value = T##_nth_value,                                      \
                .raw_index = T##_raw_index,                                      \
            };                                                                   \
        }                                                                        \
        return &desc;                                                            \
    }
