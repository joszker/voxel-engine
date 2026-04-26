#pragma once


/* CONFIGURATION */
#if !(RELEASE_MODE) && !(DEBUG_MODE)
#   ifdef NDEBUG
#       define RELEASE_MODE 1
#       undef DEBUG_MODE
#   else
#       define DEBUG_MODE 1
#       undef RELEASE_MODE
#   endif
#endif


/* COMPILER */
#if !defined(COMPILER_MSVC) && !defined(COMPILER_CLANG) && !defined(COMPILER_GCC)
#   if defined(_MSC_VER)
#       define COMPILER_MSVC 1
#   elif defined(__clang__)
#       define COMPILER_CLANG 1
#   elif defined(__GNUC__) || defined(__GNUG__)
#       define COMPILER_GCC 1
#   endif
#endif



#ifndef TRAP
#   if COMPILER_MSVC
#       define TRAP()   __debugbreak()
#   elif COMPILER_CLANG || COMPILER_GCC
#       define TRAP()   __builtin_trap()
#   else
#       define TRAP()   do { *(int*)0 = 0; } while (0)
#   endif
#endif


#ifndef ASSERT
#   if DEBUG_MODE
#       define ASSERT(expression) do { if (!(expression)) { Debug::ReportAssert(#expression, __FILE__, __func__, __LINE__); TRAP(); }} while (0)
#   else
#       if COMPILER_MSVC
#           define ASSERT(expression)	__assume(expression)
#       elif COMPILER_CLANG
#           define ASSERT(expression)	__builtin_assume(expression)
#       elif __GNUC__ >= 13
#           define ASSERT(expression)	__attribute__((__assume__(expression)))
#       else
#           define ASSERT(expression)
#       endif
#   endif
#endif


#ifndef NOT_IMPLEMENTED
#   define NOT_IMPLEMENTED()    ASSERT(!"Not implemented!")
#endif


#if COMPILER_MSVC
#   define UNUSED(X)	(__pragma(warning(suppress:4100))(X))
#else
#   define UNUSED(X)	__attribute__((__unused__))(X)
#endif


#ifndef INLINE 
#   if COMPILER_MSVC
#       define INLINE __forceinline
#   else
#       define INLINE __attribute__((__always_inline__))
#   endif
#endif


#ifndef NO_INLINE
#   if COMPILER_MSVC
#       define NO_INLINE __declspec(noinline)
#   else
#       define NO_INLINE __attribute__((noinline))
#   endif
#endif


#ifndef PACK_BEGIN
#   if COMPILER_MSVC
#       define PACK_BEGIN       __pragma(pack(push, 1))
#       define PACK_END         __pragma(pack(pop))
#   else
#       define PACK_BEGIN       _Pragma("pack(push, 1)")
#       define PACK_END         _Pragma("pack(pop)")
#   endif
#endif




// Compiler & platform specific includes
#if COMPILER_MSVC
#   include <intrin.h>
#endif


// STL includes
#include <array>
#include <vector>
#include <span>
#include <string_view>
#include <stdint.h>
#include <bit>
#include <optional>


// Integer types
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using byte = unsigned char;
using uint = unsigned int;


// Boolean types
using b8 = bool;
using b16 = u16;
using b32 = u32;
static_assert(sizeof(b8) == sizeof(u8));


// Floating-point types
using f32 = float;      // IEEE 754 single precision 32-bit float
using f64 = double;     // IEEE 754 double precision 64-bit float


template <typename T>
using Opt = std::optional<T>;

namespace Debug
{
    bool ReportAssert(const char *expr, const char *file, const char *func, size_t line);
};


#include <glm/matrix.hpp>


using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;

using Vec2i = glm::vec<2, s32, glm::defaultp>;
using Vec3i = glm::vec<3, s32, glm::defaultp>;
using Vec4i = glm::vec<4, s32, glm::defaultp>;
using Vec2u = glm::vec<2, u32, glm::defaultp>;
using Vec3u = glm::vec<3, u32, glm::defaultp>;
using Vec4u = glm::vec<4, u32, glm::defaultp>;


inline constexpr u32 Dot(Vec3u const& a, Vec3u const& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}



/* Counts the number of bits set to 1 (bit population count). */
inline u64 popcnt64(u64 x)
{
#if defined(_MSC_VER) && defined(_M_X64)
    return __popcnt64(x);
#elif COMPILER_GCC || COMPILER_CLANG
    return __builtin_popcountll(x)
#else
    #error "popcnt64: Not implemented!"
#endif
}

/* Computes `int(log2(x))` */
inline u32 log2i(u32 x)
{
    ASSERT(x != 0);
    return std::bit_width(x) - 1;
}

/* Computes `4^x` */
inline u32 pow4(u32 x)
{
    return 1 << (2 * x);
}


/* Returns `2^k` with the smallest `k`, such that `2^k >= x` */
inline u32 NextPowerOf2(u32 x)
{
#if COMPILER_MSVC
    unsigned long index;
    _BitScanReverse(&index, x - 1);
    return (1u << (index + 1));
#else
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
#endif
}
