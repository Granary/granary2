/* Copyright 2012-2013 Peter Goodman, all rights reserved. */
/*
 * unsafe-cast.h
 *
 *  Created on: 2013-12-27
 *    Author: Peter Goodman
 */

#ifndef GRANARY_BASE_CAST_H_
#define GRANARY_BASE_CAST_H_

#include "granary/base/base.h"
#include "granary/base/type_traits.h"

namespace granary {

// Non-integral, non-pointer type to something else.
//
// Note: `__builtin_memcpy` is used instead of `memcpy`, mostly for the
//     sake of kernel code where it sometimes seems that the optimisation
//     of inlining a normal `memcpy` is not done.
template <
  typename ToT,
  typename FromT,
  typename EnableIf<
    IsPointer<FromT>::RESULT || IsInteger<FromT>::RESULT,
    void,
    int
  >::Type = 0
>
inline ToT UnsafeCast(const FromT v) {
  static_assert(sizeof(FromT) == sizeof(ToT),
    "Dangerous unsafe cast between two types of different sizes.");

  ToT dest;
  __builtin_memcpy(&dest, &v, sizeof(ToT));
  return dest;
}


// Pointer to integral type.
template <
  typename ToT,
  typename FromT,
  typename EnableIf<
    IsPointer<FromT>::RESULT && IsInteger<ToT>::RESULT,
    int
  >::Type = 0
>
inline ToT UnsafeCast(const FromT v) {
  return static_cast<ToT>(reinterpret_cast<uintptr_t>(v));
}


// Pointer to pointer type.
template <
  typename ToT,
  typename FromT,
  typename EnableIf<
    IsPointer<FromT>::RESULT && IsPointer<ToT>::RESULT,
    int
  >::Type = 0
>
inline ToT UnsafeCast(const FromT v) {
  return reinterpret_cast<ToT>(reinterpret_cast<uintptr_t>(v));
}


// Integral to pointer type.
template <
  typename ToT,
  typename FromT,
  typename EnableIf<
  IsInteger<FromT>::RESULT && IsPointer<ToT>::RESULT,
    int
  >::Type = 0
>
inline ToT UnsafeCast(const FromT v) {
  return reinterpret_cast<ToT>(static_cast<uintptr_t>(v));
}

#ifdef GRANARY_INTERNAL

// Declare that a class is the base class of a single-inheritance class
// hierarchy.
# define GRANARY_BASE_CLASS(base_type) \
  static bool IsDerivedFrom(const base_type *) { \
    return true; \
  } \
  virtual int IdOf(void) const { \
    return GRANARY_CAT(kIdOf, base_type); \
  }

// Declare that a class is a derived class of a single-inheritance class
// hierarchy. The base type is the base class of the entire class hierarchy,
// not just the base class of the derived type.
# define GRANARY_DERIVED_CLASS_OF(base_type, derived_type) \
  static bool IsDerivedFrom(const derived_type *) { \
    return true; \
  } \
  static bool IsDerivedFrom(const base_type *base) { \
    return base->IdOf() == GRANARY_CAT(kIdOf, derived_type); \
  } \
  virtual int IdOf(void) const { \
    return GRANARY_CAT(kIdOf, derived_type); \
  }

// Helper macro for declaring class id enumeration constants.
# define GRANARY_DECLARE_CLASS_ID(class_name) \
  GRANARY_CAT(kIdOf, class_name)

// Define an enum that assigns unique (within the single-inheritance class
// hierarchy) numeric IDs for each class within the class hierarchy.
# define GRANARY_DECLARE_CLASS_HEIRARCHY(...) \
  enum : int { \
    GRANARY_APPLY_EACH(GRANARY_DECLARE_CLASS_ID, GRANARY_COMMA, ##__VA_ARGS__) \
  }

#else
# define GRANARY_BASE_CLASS(base_type) \
  static bool IsDerivedFrom(const base_type *); \
  virtual int IdOf(void) const;

# define GRANARY_DERIVED_CLASS_OF(base_type, derived_type) \
  static bool IsDerivedFrom(const derived_type *); \
  static bool IsDerivedFrom(const base_type *base); \
  virtual int IdOf(void) const;

# define GRANARY_DECLARE_CLASS_HEIRARCHY(...)
#endif  // GRANARY_INTERNAL

// Base type to derived type cast.
template <
  typename PointerT,
  typename BaseT,
  typename EnableIf<IsPointer<PointerT>::RESULT, int>::Type = 0
>
inline PointerT DynamicCast(const BaseT *ptr) {
  if (!ptr) {
    return nullptr;
  }
  typedef typename RemoveConst<
      typename RemovePointer<PointerT>::Type>::Type DerivedT;
  if (DerivedT::IsDerivedFrom(ptr)) {
    return UnsafeCast<PointerT>(ptr);
  }
  return nullptr;
}

// Base type to derived type cast.
template <
  typename PointerT,
  typename BaseT,
  typename EnableIf<IsPointer<PointerT>::RESULT, int>::Type = 0
>
inline bool IsA(const BaseT *ptr) {
  typedef typename RemoveConst<
      typename RemovePointer<PointerT>::Type>::Type DerivedT;
  return ptr && DerivedT::IsDerivedFrom(ptr);
}

}  // namespace granary

#endif  // GRANARY_BASE_CAST_H_
