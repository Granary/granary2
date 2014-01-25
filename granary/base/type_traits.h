/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_TYPE_TRAITS_H_
#define GRANARY_BASE_TYPE_TRAITS_H_


namespace granary {

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
struct IsArray {
  enum {
    RESULT = false
  };
};

template <typename T, unsigned long kLen>
struct IsArray<T[kLen]> {
  enum {
    RESULT = true
  };
};

template <typename T>
struct IsArray<T[]> {
  enum {
    RESULT = true
  };
};

template <const bool Condition, typename TrueType=int, typename FalseType=void>
struct EnableIf;

template <typename TrueType, typename FalseType>
struct EnableIf<true, TrueType, FalseType> {
  typedef TrueType Type;
};

template <typename TrueType, typename FalseType>
struct EnableIf<false, TrueType, FalseType> {
  typedef FalseType Type;
};

template <typename A, typename B>
struct TypesAreEqual {
  enum {
    RESULT = false
  };
};

template <typename A>
struct TypesAreEqual<A, A> {
  enum {
    RESULT = true
  };
};

template <typename A>
struct IsPointer {
  enum {
    RESULT = false
  };
};

template <typename A>
struct IsPointer<A *> {
  enum {
    RESULT = true
  };
};

template <typename A>
struct IsPointer<A &> {
  enum {
    RESULT = IsPointer<A>::RESULT
  };
};

template <typename A>
struct IsPointer<A &&> {
  enum {
    RESULT = IsPointer<A>::RESULT
  };
};

template <typename A>
struct IsInteger {
  enum {
    RESULT = false
  };
};

template <typename A>
struct IsInteger<A &> {
  enum {
    RESULT = IsInteger<A>::RESULT
  };
};

template <typename A>
struct IsInteger<A &&> {
  enum {
    RESULT = IsInteger<A>::RESULT
  };
};

#define GRANARY_DEFINE_IS_INTEGRAL(type) \
  template <> \
  struct IsInteger<type> { \
    enum { \
      RESULT = true \
    }; \
  }
GRANARY_DEFINE_IS_INTEGRAL(unsigned char);
GRANARY_DEFINE_IS_INTEGRAL(signed char);
GRANARY_DEFINE_IS_INTEGRAL(unsigned short);
GRANARY_DEFINE_IS_INTEGRAL(signed short);
GRANARY_DEFINE_IS_INTEGRAL(unsigned int);
GRANARY_DEFINE_IS_INTEGRAL(signed int);
GRANARY_DEFINE_IS_INTEGRAL(unsigned long);
GRANARY_DEFINE_IS_INTEGRAL(signed long);
#undef GRANARY_DEFINE_IS_INTEGRAL

template <typename T>
struct RemoveConst {
  typedef T Type;
};

template <typename T>
struct RemoveConst<const T> {
  typedef T Type;
};

}  // namespace granary


#endif  // GRANARY_BASE_TYPE_TRAITS_H_
