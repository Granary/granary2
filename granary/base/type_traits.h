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

#define GRANARY_DEFINE_TRAIT_REFERENCES(trait_name) \
  template <typename A> \
  struct trait_name<A &> { \
    enum { \
      RESULT = trait_name<A>::RESULT \
    }; \
  }; \
  template <typename A> \
  struct trait_name<A &&> { \
    enum { \
      RESULT = trait_name<A>::RESULT \
    }; \
  }

template <typename A>
struct IsPointer {
  enum {
    RESULT = false
  };
};

GRANARY_DEFINE_TRAIT_REFERENCES(IsPointer);

template <typename A>
struct IsPointer<A *> {
  enum {
    RESULT = true
  };
};

template <typename A>
struct IsInteger {
  enum {
    RESULT = false
  };
};

GRANARY_DEFINE_TRAIT_REFERENCES(IsInteger);

template <typename A>
struct IsSignedInteger {
  enum {
    RESULT = false
  };
};

template <typename A>
struct IsUnsignedInteger {
  enum {
    RESULT = false
  };
};

GRANARY_DEFINE_TRAIT_REFERENCES(IsSignedInteger);
GRANARY_DEFINE_TRAIT_REFERENCES(IsUnsignedInteger);

#define GRANARY_DEFINE_IS_INTEGRAL(type, is_signed) \
  template <> \
  struct IsInteger<type> { \
    enum { \
      RESULT = true \
    }; \
  }; \
  template <> \
  struct IsSignedInteger<type> { \
    enum { \
      RESULT = is_signed \
    }; \
  }; \
  template <> \
  struct IsUnsignedInteger<type> { \
    enum { \
      RESULT = !is_signed \
    }; \
  }

GRANARY_DEFINE_IS_INTEGRAL(unsigned char, false);
GRANARY_DEFINE_IS_INTEGRAL(signed char, true);
GRANARY_DEFINE_IS_INTEGRAL(unsigned short, false);
GRANARY_DEFINE_IS_INTEGRAL(signed short, true);
GRANARY_DEFINE_IS_INTEGRAL(unsigned int, false);
GRANARY_DEFINE_IS_INTEGRAL(signed int, true);
GRANARY_DEFINE_IS_INTEGRAL(unsigned long, false);
GRANARY_DEFINE_IS_INTEGRAL(signed long, true);
#undef GRANARY_DEFINE_IS_INTEGRAL

template <typename T>
struct RemoveConst {
  typedef T Type;
};

template <typename T>
struct RemoveConst<const T> {
  typedef T Type;
};

#undef GRANARY_DEFINE_TRAIT_REFERENCES

}  // namespace granary


#endif  // GRANARY_BASE_TYPE_TRAITS_H_
