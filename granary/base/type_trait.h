/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_TYPE_TRAIT_H_
#define GRANARY_BASE_TYPE_TRAIT_H_

namespace granary {

template <typename T>
struct Identity {
  typedef T Type;
};

class TrueType : public std::true_type {
 public:
  enum {
    RESULT = true
  };
};
class FalseType : public std::false_type {
 public:
  enum {
    RESULT = false
  };
};

template <typename T>
struct RemoveReference {
  typedef T Type;
};

template <typename T>
struct RemoveReference<T &> {
  typedef T Type;
};

template <typename T>
struct RemoveReference<T &&> {
  typedef T Type;
};

template <typename T>
struct RemovePointer {
  typedef T Type;
};

template <typename T>
struct RemovePointer<T *> {
  typedef T Type;
};

template <typename T>
struct IsArray : public FalseType {};

template <typename T, unsigned long kLen>
struct IsArray<T[kLen]> : public TrueType {};

template <typename T>
struct IsArray<T[]> : public TrueType {};

template <const bool Condition, typename IfTrueType=int,
                                typename IfFalseType=void>
struct EnableIf;

template <typename IfTrueType, typename IfFalseType>
struct EnableIf<true, IfTrueType, IfFalseType> {
  typedef IfTrueType Type;
};

template <typename IfTrueType, typename IfFalseType>
struct EnableIf<false, IfTrueType, IfFalseType> {
  typedef IfFalseType Type;
};

template <typename A, typename B>
struct TypesAreEqual : public FalseType {};

template <typename A>
struct TypesAreEqual<A, A> : public TrueType {};

static_assert(sizeof(unsigned long) == sizeof(unsigned long long),
    "Unrecognized architecture.");

static_assert(sizeof(unsigned long) == sizeof(uintptr_t),
    "Unrecognized architecture.");

static_assert(sizeof(long) == sizeof(long long),
    "Unrecognized architecture.");

static_assert(sizeof(long) == sizeof(intptr_t),
    "Unrecognized architecture.");

#define GRANARY_DEFINE_TRAIT_REFERENCES(trait_name) \
  template <typename A> \
  struct trait_name<A &> : public trait_name<A> {}; \
  template <typename A> \
  struct trait_name<A &&> : public trait_name<A> {}

template <typename A>
struct IsPointer : public FalseType {};

template <typename Ret, typename... Args>
struct IsPointer<Ret (*)(Args...)> : public TrueType {};

template <typename A>
struct IsPointer<A *> : public TrueType {};

template <typename A>
struct IsPointer<const A *> : public TrueType {};

template <typename A>
struct IsPointer<volatile A *> : public TrueType {};

template <typename A>
struct IsPointer<const volatile A *> : public TrueType {};

GRANARY_DEFINE_TRAIT_REFERENCES(IsPointer);

template <typename A>
struct IsInteger : public std::is_enum<A> {};

template <typename A>
struct IsSignedInteger : public FalseType {};

template <typename A>
struct IsUnsignedInteger : public FalseType {};

#define GRANARY_DEFINE_IS_INTEGRAL(type, signed_base_type, unsigned_base_type) \
  template <> \
  struct IsInteger<type> : public TrueType {}; \
  template <> \
  struct IsSignedInteger<type> : public signed_base_type {}; \
  template <> \
  struct IsUnsignedInteger<type> : public unsigned_base_type {}

GRANARY_DEFINE_IS_INTEGRAL(unsigned char, FalseType, TrueType);
GRANARY_DEFINE_IS_INTEGRAL(signed char, TrueType, FalseType);
GRANARY_DEFINE_IS_INTEGRAL(unsigned short, FalseType, TrueType);
GRANARY_DEFINE_IS_INTEGRAL(signed short, TrueType, FalseType);
GRANARY_DEFINE_IS_INTEGRAL(unsigned int, FalseType, TrueType);
GRANARY_DEFINE_IS_INTEGRAL(signed int, TrueType, FalseType);
GRANARY_DEFINE_IS_INTEGRAL(unsigned long, FalseType, TrueType);
GRANARY_DEFINE_IS_INTEGRAL(signed long, TrueType, FalseType);
#undef GRANARY_DEFINE_IS_INTEGRAL

GRANARY_DEFINE_TRAIT_REFERENCES(IsInteger);
GRANARY_DEFINE_TRAIT_REFERENCES(IsSignedInteger);
GRANARY_DEFINE_TRAIT_REFERENCES(IsUnsignedInteger);

template <typename T>
struct RemoveConst {
  typedef T Type;
};

template <typename T>
struct RemoveConst<const T> {
  typedef T Type;
};

template <typename T>
struct IsConst : public FalseType {};

template <typename T>
struct IsConst<const T> : public TrueType {};

template <typename T>
struct IsConst<const volatile T> : public TrueType {};

#undef GRANARY_DEFINE_TRAIT_REFERENCES

}  // namespace granary


#endif  // GRANARY_BASE_TYPE_TRAIT_H_
