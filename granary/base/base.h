/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_BASE_H_
#define GRANARY_BASE_BASE_H_

// A bit of a trick to make sure that when generating export headers, we don't
// accidentally include any system headers in the export.
#if (defined(GRANARY_INTERNAL) || !defined(GRANARY_EXTERNAL)) && \
    !defined(GRANARY_ASSEMBLY)
# include "granary/base/abi.h"
# include "granary/base/cstring.h"

# include <algorithm>
# include <atomic>
# include <cstdarg>
# include <cstddef>
# include <cstdint>
# include <functional>
# include <initializer_list>
# include <limits>
# include <memory>
# include <new>
# include <type_traits>
#endif

#define GRANARY_EARLY_GLOBAL __attribute__((init_priority(102)))
#define GRANARY_GLOBAL __attribute__((init_priority(103)))
#define GRANARY_UNPROTECTED_GLOBAL __attribute__((section(".bss.granary_unprotected")))

// Useful for Valgrind-based debugging.
#if !defined(GRANARY_EXTERNAL) && !defined(GRANARY_ASSEMBLY)
# ifdef GRANARY_WITH_VALGRIND
#   include <valgrind/valgrind.h>
#   include <valgrind/memcheck.h>
#   define GRANARY_IF_VALGRIND(...) __VA_ARGS__
# else
#   define VALGRIND_MALLOCLIKE_BLOCK(addr, sizeB, rzB, is_zeroed)
#   define VALGRIND_FREELIKE_BLOCK(addr, rzB)
#   define VALGRIND_CREATE_MEMPOOL(addr, rzB, is_zeroed)
#   define VALGRIND_MEMPOOL_ALLOC(pool, addr, size)
#   define VALGRIND_MEMPOOL_FREE(pool, addr)
#   define VALGRIND_MAKE_MEM_UNDEFINED(addr,size)
#   define VALGRIND_MAKE_MEM_DEFINED(addr,size)
#   define VALGRIND_MAKE_MEM_NOACCESS(addr,size)
#   define VALGRIND_CHECK_MEM_IS_DEFINED(addr,size)
#   define GRANARY_IF_VALGRIND(...)
# endif
#endif

// For namespace-based `using` declarations without triggering the linter.
#define GRANARY_USING_NAMESPACE using namespace  // NOLINT

#define GRANARY_UNIQUE_SYMBOL \
  GRANARY_CAT( \
    GRANARY_CAT( \
      GRANARY_CAT(_, __LINE__), \
      GRANARY_CAT(_, __INCLUDE_LEVEL__)), \
    GRANARY_CAT(_, __COUNTER__))

// For use only when editing text with Eclipse CDT (my version doesn't handle
// `decltype` or `alignof` well)
#ifdef GRANARY_ECLIPSE
# define alignas(...)
# define alignof(...) 16
# define GRANARY_ENABLE_IF(...) int
#else
# define GRANARY_ENABLE_IF(...) __VA_ARGS__
#endif

#ifdef GRANARY_TARGET_debug
# define GRANARY_IF_DEBUG(...) __VA_ARGS__
# define GRANARY_IF_DEBUG_(...) __VA_ARGS__ ,
# define _GRANARY_IF_DEBUG(...) , __VA_ARGS__
# define GRANARY_IF_DEBUG_ELSE(a, b) a
# define GRANARY_ASSERT(...) granary_break_on_fault_if(!(__VA_ARGS__))
#else
# define GRANARY_IF_DEBUG(...)
# define GRANARY_IF_DEBUG_(...)
# define _GRANARY_IF_DEBUG(...)
# define GRANARY_IF_DEBUG_ELSE(a, b) b
# define GRANARY_ASSERT(...)
#endif  // GRANARY_TARGET_debug

#ifdef GRANARY_TARGET_test
# define GRANARY_TEST_VIRTUAL virtual
# define GRANARY_IF_TEST(...) __VA_ARGS__
# define _GRANARY_IF_TEST(...) , __VA_ARGS__
#else
# define GRANARY_TEST_VIRTUAL
# define GRANARY_IF_TEST(...)
# define _GRANARY_IF_TEST(...)
# ifndef ContextInterface
#   define ContextInterface Context  // Minor hack!
# endif
#endif  // GRANARY_TARGET_test

#ifdef GRANARY_ARCH_INTERNAL
# define GRANARY_ARCH_PUBLIC public
#else
# define GRANARY_ARCH_PUBLIC private
#endif

// Marks some pointers as being internal, and convertible to void for exports.
#ifdef GRANARY_INTERNAL
# define GRANARY_MUTABLE mutable
# define GRANARY_POINTER(type) type
# define GRANARY_UINT32(type) type
# define GRANARY_PROTECTED public
# define GRANARY_PUBLIC public
# define GRANARY_CONST
# define GRANARY_MUTABLE mutable
# define GRANARY_IF_EXTERNAL(...)
# define _GRANARY_IF_EXTERNAL(...)
# define GRANARY_IF_INTERNAL(...) __VA_ARGS__
# define _GRANARY_IF_INTERNAL(...) , __VA_ARGS__
# define GRANARY_EXTERNAL_DELETE

// Not defined if `GRANARY_INTERNAL` isn't defined.
# define GRANARY_INTERNAL_DEFINITION
# define GRANARY_EXPORT_HEADER
#else
# define GRANARY_MUTABLE
# define GRANARY_POINTER(type) void
# define GRANARY_UINT32(type) uint32_t
# define GRANARY_PROTECTED private
# define GRANARY_PUBLIC protected
# define GRANARY_CONST const
# define GRANARY_MUTABLE
# define GRANARY_IF_EXTERNAL(...)  __VA_ARGS__
# define _GRANARY_IF_EXTERNAL(...) , __VA_ARGS__
# define GRANARY_IF_INTERNAL(...)
# define _GRANARY_IF_INTERNAL(...)
# define GRANARY_EXTERNAL_DELETE = delete
#endif

#ifdef GRANARY_EXTERNAL
# define GRANARY_EXTERNAL_SHARED extern
# define GRANARY_EXTERNAL_FRIEND GRANARY_EXPORTED_friend
#else
# define GRANARY_EXTERNAL_SHARED
# define GRANARY_EXTERNAL_FRIEND friend
#endif

// Name of the granary binary.
#ifndef GRANARY_NAME
# define GRANARY_NAME granary
#endif
#define GRANARY_NAME_STRING GRANARY_TO_STRING(GRANARY_NAME)

// Static branch prediction hints.
#define GRANARY_LIKELY(x) __builtin_expect((x),1)
#define GRANARY_UNLIKELY(x) __builtin_expect((x),0)

// Inline assembly.
#define GRANARY_INLINE_ASSEMBLY(...) __asm__ __volatile__ ( __VA_ARGS__ )

// Convert a sequence of symbols into a string literal.
#define GRANARY_TO_STRING__(x) #x
#define GRANARY_TO_STRING_(x) GRANARY_TO_STRING__(x)
#define GRANARY_TO_STRING(x) GRANARY_TO_STRING_(x)

// Concatenate two symbols into one.
#define GRANARY_CAT__(x, y) x ## y
#define GRANARY_CAT_(x, y) GRANARY_CAT__(x, y)
#define GRANARY_CAT(x, y) GRANARY_CAT_(x, y)

// Expand out into nothing.
#define GRANARY_NOTHING__
#define GRANARY_NOTHING_ GRANARY_NOTHING__
#define GRANARY_NOTHING GRANARY_NOTHING_

// Determine the number of arguments in a variadic macro argument pack.
// From: http://efesx.com/2010/07/17/variadic-macro-to-count-number-of-\
// arguments/#comment-256
#define GRANARY_NUM_PARAMS_(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,N,...) N
#define GRANARY_NUM_PARAMS(...) \
  GRANARY_NUM_PARAMS_(, ##__VA_ARGS__,9,8,7,6,5,4,3,2,1,0)

// Spits back out the arguments passed into the macro function.
#define GRANARY_PARAMS(...) __VA_ARGS__

// Try to make sure that a function is not optimized.
#define GRANARY_DISABLE_OPTIMIZER __attribute__((used, noinline, \
                                                 visibility ("default")))

// Export some function to instrumentation code. Only exported code can be
// directly invoked by instrumented code.
//
// Note: While these functions can be invoked by instrumented code, their
//       code *is not* instrumented.
#define GRANARY_EXPORT_TO_INSTRUMENTATION \
  __attribute__((noinline, used, section(".text.inst_exports")))

// Determine how much should be added to a value `x` in order to align `x` to
// an `align`-byte boundary.
#define GRANARY_ALIGN_FACTOR(x, align) \
  (((x) % (align)) ? ((align) - ((x) % (align))) : 0)


// Align a value `x` to an `align`-byte boundary.
#define GRANARY_ALIGN_TO(x, align) \
  ((x) + GRANARY_ALIGN_FACTOR(x, align))


// Return the maximum or minimum of two values.
#define GRANARY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GRANARY_MAX(a, b) ((a) < (b) ? (b) : (a))

// Disallow copying of a specific class.
#define GRANARY_DISALLOW_COPY(cls) \
  cls(const cls &) = delete; \
  cls(const cls &&) = delete


// Disallow assigning of instances of a specific class.
#define GRANARY_DISALLOW_ASSIGN(cls) \
  void operator=(const cls &) = delete; \
  void operator=(const cls &&) = delete


// Disallow copying and assigning of instances of a specific class.
#define GRANARY_DISALLOW_COPY_AND_ASSIGN(cls) \
  GRANARY_DISALLOW_COPY(cls); \
  GRANARY_DISALLOW_ASSIGN(cls)


// Disallow copying of instances of a class generated by a specific
// class template.
#define GRANARY_DISALLOW_COPY_TEMPLATE(cls, params) \
  cls(const cls<GRANARY_PARAMS params> &) = delete; \
  cls(const cls<GRANARY_PARAMS params> &&) = delete


// Disallow copying and assigning of instances of a class generated by a
// specific class template.
#define GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(cls, params) \
  GRANARY_DISALLOW_COPY_TEMPLATE(cls, params); \
  void operator=(const cls<GRANARY_PARAMS params> &) = delete; \
  void operator=(const cls<GRANARY_PARAMS params> &&) = delete


// Mark a result / variable as being used.
#define GRANARY_UNUSED(...) (void) __VA_ARGS__
#define GRANARY_USED(var) \
  do { \
    GRANARY_INLINE_ASSEMBLY("" :: "m"(var)); \
    GRANARY_UNUSED(var); \
  } while (0)


#define GRANARY_COMMA() ,

// Apply a macro `pp` to each of a variable number of arguments. Separate the
// results of the macro application with `sep`.
#define GRANARY_APPLY_EACH(pp, sep, ...) \
  GRANARY_CAT(GRANARY_APPLY_EACH_, GRANARY_NUM_PARAMS(__VA_ARGS__))( \
      pp, sep, ##__VA_ARGS__)

#define GRANARY_APPLY_EACH_1(pp, sep, a0) \
  pp(a0)

#define GRANARY_APPLY_EACH_2(pp, sep, a0, a1) \
  pp(a0) sep() \
  pp(a1)

#define GRANARY_APPLY_EACH_3(pp, sep, a0, a1, a2) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2)

#define GRANARY_APPLY_EACH_4(pp, sep, a0, a1, a2, a3) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3)

#define GRANARY_APPLY_EACH_5(pp, sep, a0, a1, a2, a3, a4) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4)

#define GRANARY_APPLY_EACH_6(pp, sep, a0, a1, a2, a3, a4, a5) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5)

#define GRANARY_APPLY_EACH_7(pp, sep, a0, a1, a2, a3, a4, a5, a6) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6)

#define GRANARY_APPLY_EACH_8(pp, sep, a0, a1, a2, a3, a4, a5, a6, a7) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6) sep() \
  pp(a7)

#define GRANARY_APPLY_EACH_9(pp, sep, a0, a1, a2, a3, a4, a5, a6, a7, a8) \
  pp(a0) sep() \
  pp(a1) sep() \
  pp(a2) sep() \
  pp(a3) sep() \
  pp(a4) sep() \
  pp(a5) sep() \
  pp(a6) sep() \
  pp(a7) sep() \
  pp(a8)

// Mark a symbol as exported.
#define GRANARY_EXPORT __attribute__((visibility("default")))

// Switches between user and kernel space code.
#ifdef GRANARY_WHERE_user
# define GRANARY_IF_USER_ELSE(a, b) a
# define GRANARY_IF_KERNEL_ELSE(a, b) b
# define GRANARY_IF_USER(...) __VA_ARGS__
# define GRANARY_IF_KERNEL(...)
#else
# define GRANARY_IF_USER_ELSE(a, b) b
# define GRANARY_IF_KERNEL_ELSE(a, b) a
# define GRANARY_IF_USER(...)
# define GRANARY_IF_KERNEL(...) __VA_ARGS__
#endif

#endif  // GRANARY_BASE_BASE_H_
