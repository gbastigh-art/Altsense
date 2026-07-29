#pragma once

// usage: STATIC_ASSERT(<expr>, <message>)
#if __STDC_VERSION__ >= 202311L
    #define STATIC_ASSERT static_assert
#else
    #define STATIC_ASSERT _Static_assert
#endif

#ifdef __GNUC__
    #define M_LIKELY(x_) __builtin_expect((x_), 1)
    #define M_UNLIKELY(x_) __builtin_expect((x_), 0)
#else
    #define M_LIKELY(x_)
    #define M_UNLIKELY(x_)
#endif // ifdef __GNUC__

#include <stdint.h>
#include <limits.h>

// check platform is ok
STATIC_ASSERT(CHAR_BIT == 8, "byte is not 8 bits");
STATIC_ASSERT(sizeof(int) == 4, "int is not 32-bit");

#ifndef __BYTE_ORDER__
    #error "system does not define a byte order"
#endif // ifndef __BYTE_ORDER__

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    #error "system is not little endian"
#endif // if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__

#if UINTPTR_MAX == 0xffffffff
    #define M_BITS_32
    #define M_BITS 32
    #define MAX_ALIGN 8
#elif UINTPTR_MAX == 0xffffffffffffffff
    #define M_BITS_64
    #define M_BITS 64
    #define MAX_ALIGN 16
#else
    #error "system is not 32- or 64-bit"
#endif

#ifdef __OBJC__
    #define M_THREAD_LOCAL _Thread_local
#else
    #define M_THREAD_LOCAL thread_local
#endif

#ifdef EMSCRIPTEN
    STATIC_ASSERT(M_BITS == 32)
#endif

// convert preprocessor value x to string
#define _STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) _STRINGIFY_IMPL(x)

// cross-platform pragma
#define PRAGMA(x) _Pragma(#x)

// concat two preprocessor variables
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b

// loop unroll
#if defined(__clang__)
    #define UNROLL(n) PRAGMA(clang loop unroll_count(n))
#elif defined(__GNU__)
    #define UNROLL(n) PRAGMA(GCC unroll n)
#else
    #define UNROLL(n) PRAGMA(unroll)
#endif

// type "const t" -> "t"
#define unconst(t) typeof(({ t x_; __auto_type y_ = x_; y_; }))

// get type of field _f on type _t
#define typeof_field(_t, _f) typeof(((_t*) (NULL))->_f)

// get type of pointer to T
#define typeof_pointer_to(T) typeof((&((T) {})))

// get type of *T
#define typeof_deref(T) typeof(*((T)(NULL)))

// get size of field _f on type _t
#define sizeof_field(_t, _f) sizeof(((_t*) NULL)->_f)

#define M_UNUSED __attribute__((unused))
#define M_PACKED __attribute__((packed))

#define M_ALIGNED(N) __attribute__((aligned(N)))

#define M_PACKED_ALIGNED(N) __attribute__((packed, aligned(N)))

// force inlining
#define M_INLINE static inline

// force not inlining
#define M_NO_INLINE __attribute__((noinline))

// generates a warning if not the same type
#define CHECK_TYPE(_T, _a) ((void) (((typeof(_a)*) (NULL)) == (_T*)(NULL)))

// generates error if field does not exist
#define CHECK_FIELD(_T, _f) ((void) ((_T) {})._f)

// see stackoverflow.com/questions/11761703
// get number of arguments with NARG
#define NARG_SEQ()                            \
     63,62,61,60,                             \
     59,58,57,56,55,54,53,52,51,50,           \
     49,48,47,46,45,44,43,42,41,40,           \
     39,38,37,36,35,34,33,32,31,30,           \
     29,28,27,26,25,24,23,22,21,20,           \
     19,18,17,16,15,14,13,12,11,10,           \
     9,8,7,6,5,4,3,2,1,0
#define NARG_N(                               \
     _1, _2, _3, _4, _5, _6, _7, _8, _9,_10,  \
     _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
     _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
     _31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
     _41,_42,_43,_44,_45,_46,_47,_48,_49,_50, \
     _51,_52,_53,_54,_55,_56,_57,_58,_59,_60, \
     _61,_62,_63,N,...) N
#define NARG_I(...) NARG_N(__VA_ARGS__)
#define NARG(...) NARG_I(__VA_ARGS__, NARG_SEQ())

// general definition for any function name
#define _VMACRO(name, n) CONCAT(name, n)
#define VMACRO(func, ...) _VMACRO(func, NARG(__VA_ARGS__))(__VA_ARGS__)

#define _NTH_ARG0(A0, ...) A0
#define _NTH_ARG1(A0, A1, ...) A1
#define _NTH_ARG2(A0, A1, A2, ...) A2
#define _NTH_ARG3(A0, A1, A2, A3, ...) A3
#define _NTH_ARG4(A0, A1, A2, A3, A4, ...) A4
#define _NTH_ARG5(A0, A1, A2, A3, A4, A5, ...) A5
#define _NTH_ARG6(A0, A1, A2, A3, A4, A5, A6, ...) A6
#define _NTH_ARG7(A0, A1, A2, A3, A4, A5, A6, A7, ...) A7
#define _NTH_ARG8(A0, A1, A2, A3, A4, A5, A6, A7, A8, ...) A8

// get NTH_ARG
#define _NTH_ARG(N) CONCAT(_NTH_ARG, N)
#define NTH_ARG(N, ...) _NTH_ARG(NARG(__VA_ARGS__))(__VA_ARGS__)

#define LAST_ARG0(A) A 
#define LAST_ARG1(A,  ...) LAST_ARG0(__VA_ARGS__)
#define LAST_ARG2(A,  ...) LAST_ARG1(__VA_ARGS__)
#define LAST_ARG3(A,  ...) LAST_ARG2(__VA_ARGS__)
#define LAST_ARG4(A,  ...) LAST_ARG3(__VA_ARGS__)
#define LAST_ARG5(A,  ...) LAST_ARG4(__VA_ARGS__)
#define LAST_ARG6(A,  ...) LAST_ARG5(__VA_ARGS__)
#define LAST_ARG7(A,  ...) LAST_ARG6(__VA_ARGS__)
#define LAST_ARG8(A,  ...) LAST_ARG7(__VA_ARGS__)
#define LAST_ARG9(A,  ...) LAST_ARG8(__VA_ARGS__)
#define LAST_ARG10(A, ...) LAST_ARG9(__VA_ARGS__)
#define LAST_ARG11(A, ...) LAST_ARG10(__VA_ARGS__)
#define LAST_ARG12(A, ...) LAST_ARG11(__VA_ARGS__)
#define LAST_ARG13(A, ...) LAST_ARG12(__VA_ARGS__)
#define LAST_ARG14(A, ...) LAST_ARG13(__VA_ARGS__)
#define LAST_ARG15(A, ...) LAST_ARG14(__VA_ARGS__)
#define LAST_ARG16(A, ...) LAST_ARG15(__VA_ARGS__)
#define LAST_ARG17(A, ...) LAST_ARG16(__VA_ARGS__)
#define LAST_ARG18(A, ...) LAST_ARG17(__VA_ARGS__)
#define LAST_ARG19(A, ...) LAST_ARG18(__VA_ARGS__)
#define LAST_ARG20(A, ...) LAST_ARG19(__VA_ARGS__)
#define LAST_ARG21(A, ...) LAST_ARG20(__VA_ARGS__)
#define LAST_ARG22(A, ...) LAST_ARG21(__VA_ARGS__)
#define LAST_ARG23(A, ...) LAST_ARG22(__VA_ARGS__)
#define LAST_ARG24(A, ...) LAST_ARG23(__VA_ARGS__)
#define LAST_ARG25(A, ...) LAST_ARG24(__VA_ARGS__)
#define LAST_ARG26(A, ...) LAST_ARG25(__VA_ARGS__)
#define LAST_ARG27(A, ...) LAST_ARG26(__VA_ARGS__)
#define LAST_ARG28(A, ...) LAST_ARG27(__VA_ARGS__)
#define LAST_ARG29(A, ...) LAST_ARG28(__VA_ARGS__)
#define LAST_ARG30(A, ...) LAST_ARG29(__VA_ARGS__)
#define LAST_ARG31(A, ...) LAST_ARG30(__VA_ARGS__)
#define LAST_ARG32(A, ...) LAST_ARG31(__VA_ARGS__)
#define LAST_ARG33(A, ...) LAST_ARG32(__VA_ARGS__)
#define LAST_ARG34(A, ...) LAST_ARG33(__VA_ARGS__)
#define LAST_ARG35(A, ...) LAST_ARG34(__VA_ARGS__)
#define LAST_ARG36(A, ...) LAST_ARG35(__VA_ARGS__)
#define LAST_ARG37(A, ...) LAST_ARG36(__VA_ARGS__)
#define LAST_ARG38(A, ...) LAST_ARG37(__VA_ARGS__)
#define LAST_ARG39(A, ...) LAST_ARG38(__VA_ARGS__)
#define LAST_ARG40(A, ...) LAST_ARG39(__VA_ARGS__)
#define LAST_ARG41(A, ...) LAST_ARG40(__VA_ARGS__)
#define LAST_ARG42(A, ...) LAST_ARG41(__VA_ARGS__)
#define LAST_ARG43(A, ...) LAST_ARG42(__VA_ARGS__)
#define LAST_ARG44(A, ...) LAST_ARG43(__VA_ARGS__)
#define LAST_ARG45(A, ...) LAST_ARG44(__VA_ARGS__)
#define LAST_ARG46(A, ...) LAST_ARG45(__VA_ARGS__)
#define LAST_ARG47(A, ...) LAST_ARG46(__VA_ARGS__)
#define LAST_ARG48(A, ...) LAST_ARG47(__VA_ARGS__)
#define LAST_ARG49(A, ...) LAST_ARG48(__VA_ARGS__)
#define LAST_ARG50(A, ...) LAST_ARG49(__VA_ARGS__)
#define LAST_ARG51(A, ...) LAST_ARG50(__VA_ARGS__)
#define LAST_ARG52(A, ...) LAST_ARG51(__VA_ARGS__)
#define LAST_ARG53(A, ...) LAST_ARG52(__VA_ARGS__)
#define LAST_ARG54(A, ...) LAST_ARG53(__VA_ARGS__)
#define LAST_ARG55(A, ...) LAST_ARG54(__VA_ARGS__)
#define LAST_ARG56(A, ...) LAST_ARG55(__VA_ARGS__)
#define LAST_ARG57(A, ...) LAST_ARG56(__VA_ARGS__)
#define LAST_ARG58(A, ...) LAST_ARG57(__VA_ARGS__)
#define LAST_ARG59(A, ...) LAST_ARG58(__VA_ARGS__)
#define LAST_ARG60(A, ...) LAST_ARG59(__VA_ARGS__)
#define LAST_ARG61(A, ...) LAST_ARG60(__VA_ARGS__)
#define LAST_ARG62(A, ...) LAST_ARG61(__VA_ARGS__)
#define LAST_ARG63(A, ...) LAST_ARG62(__VA_ARGS__)
#define LAST_ARG64(A, ...) LAST_ARG63(__VA_ARGS__)
