/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_BASE_BASE_H_
#define GRANARY_BASE_BASE_H_

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
// From: http://efesx.com/2010/07/17/variadic-macro-to-count-number-of-arguments/#comment-256
#define GRANARY_NUM_PARAMS_(_0,_1,_2,_3,_4,_5,_6,_7,N,...) N
#define GRANARY_NUM_PARAMS(...) \
  GRANARY_NUM_PARAMS_(, ##__VA_ARGS__,7,6,5,4,3,2,1,0)


// Spits back out the arguments passed into the macro function.
#define GRANARY_PARAMS(...) __VA_ARGS__


// Try to make sure that a function is not optimized.
#define GRANARY_DISABLE_OPTIMIZER __attribute__((used, noinline))


// Determine how much should be added to a value `x` in order to align `x` to
// an `align`-byte boundary.
#define GRANARY_ALIGN_FACTOR(x, align) \
  (((x) % (align)) ? ((x) - ((x) % (align))) : 0)


// Align a value `x` to an `align`-byte boundary.
#define GRANARY_ALIGN_TO(x, align) \
  ((x) + GRANARY_ALIGN_FACTOR(x, align))


// Disallow copying of a specific class.
#define GRANARY_DISALLOW_COPY(cls) \
  cls(const cls &) = delete; \
  cls(const cls &&) = delete


// Disallow assigning of instances of a specific class.
#define GRANARY_DISALLOW_ASSIGN(cls) \
  cls &operator=(const cls &) = delete; \
  cls &operator=(const cls &&) = delete


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
  cls<GRANARY_PARAMS params> &operator=(const cls<GRANARY_PARAMS params> &) = delete; \
  cls<GRANARY_PARAMS params> &operator=(const cls<GRANARY_PARAMS params> &&) = delete


// Mark a result / variable as being used.
#define GRANARY_UNUSED(x) (void) x

#endif  // GRANARY_BASE_BASE_H_
