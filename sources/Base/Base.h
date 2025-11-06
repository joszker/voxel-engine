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
#if !defined(COMPILER_MSVC) && !defined(COMPILER_CLANG) && !defined(COMPILER_GCC) && !defined(COMPILER_TCC)
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




#include <array>
#include <vector>
#include <span>
#include <string_view>
#include <stdint.h>


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



namespace Debug
{
    bool ReportAssert(const char *expr, const char *file, const char *func, size_t line);
};