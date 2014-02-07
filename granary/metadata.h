/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_METADATA_H_
#define GRANARY_METADATA_H_

#include "granary/base/base.h"
#include "granary/base/new.h"
#include "granary/base/hash.h"
#include "granary/base/type_traits.h"

#include "granary/module.h"

namespace granary {

// Forward declarations.
class GenericMetaData;
class InstrumentedBasicBlock;

// Serializable meta-data (i.e. immutable once committed to the code cache)
// must implement the `Hash` and `Equals` methods, and extend this template
// using CRTP.
template <typename T>
struct IndexableMetaData {
 public:
  void Hash(HashFunction *hasher) const;
  bool Equals(const T *that) const;
};

// Mutable meta-data (i.e. mutable even after committed to the code cache)
// must extend this base class.
struct MutableMetaData {};

enum class UnificationStatus {
  ACCEPT = 0,  // Unifies perfectly.
  ADAPT = 2,  // Does not unify perfectly, but can be adapted.
  REJECT = 1  // Cannot be unified / adapted.
};

// Unifiable meta-data, i.e. meta-data that behaves a bit like indexable meta-
// data, but doesn't directly participate in the indexing process. The idea here
// is that sometimes we want to generate new versions of basic blocks, and other
// times we want to be able to re-use old versions, but the old versions aren't
// necessarily perfectly suited, so we need to adapt to them.
template <typename T>
struct UnifiableMetaData {
 public:
  UnificationStatus CanUnifyWith(const T *that) const;
};

// TODO(pag): How to eventually handle static instrumentation with mutable
//            meta-data?

// Meta-data that Granary maintains about all basic blocks. This meta-data
// guides the translation process.
//
// This meta-data is registered in `granary::InitMetaData`.
struct TranslationMetaData : IndexableMetaData<TranslationMetaData> {

  // The module from which this block originates.
  GRANARY_CONST ModuleOffset source;

  // The program counter.
  GRANARY_CONST AppProgramCounter native_pc;

  // Initialize Granary's internal translation meta-data.
  TranslationMetaData(void);

  // Hash the translation meta-data.
  void Hash(HashFunction *hasher) const;

  // Compare two translation meta-data objects for equality.
  bool Equals(const TranslationMetaData *meta) const;
} __attribute__((packed));

namespace detail {
namespace meta {

// Describes some generic meta-data in a way that Granary understands.
struct MetaDataInfo {
  GRANARY_CONST MetaDataInfo * GRANARY_CONST next;

  // Where in the generic meta-data is this specific meta-data.
  const size_t size;
  const size_t align;
  GRANARY_CONST size_t offset;
  GRANARY_CONST bool is_registered;

  // Generic ways for Granary to interact with this meta-data.
  void (* const initialize)(void *);
  void (* const copy_initialize)(void *, const void *);
  void (* const destroy)(void *);
  void (* const hash)(HashFunction *, const void *);
  bool (* const compare_equals)(const void *, const void *);
  UnificationStatus (* const can_unify)(const void *, const void *);

} __attribute__((packed));

// Initialize some meta-data.
template <typename T>
void Initialize(void *mem) {
  new (mem) T;
}

// Initialize some meta-data.
template <typename T>
void CopyInitialize(void *mem, const void *that) {
  new (mem) T(*reinterpret_cast<const T *>(that));
}

// Destroy some meta-data.
template <typename T>
void Destroy(void *mem) {
  reinterpret_cast<T *>(mem)->~T();
}

// Hash some meta-data.
template <typename T>
void Hash(HashFunction *hasher, const void *mem) {
  reinterpret_cast<const T *>(mem)->Hash(hasher);
}

// Compare some meta-data for equality.
template <typename T>
bool CompareEquals(const void *a, const void *b) {
  return reinterpret_cast<const T *>(a)->Equals(reinterpret_cast<const T *>(b));
}

// Compare some meta-data for equality.
template <typename T>
UnificationStatus CanUnify(const void *a, const void *b) {
  return reinterpret_cast<const T *>(a)->CanUnifyWith(
      reinterpret_cast<const T *>(b));
}

// Describes whether some type is an indexable meta-data type.
template <typename T>
struct IsIndexableMetaData {
  enum {
    RESULT = !!std::is_convertible<T *, IndexableMetaData<T> *>::value
  };
};

// Describes whether some type is an indexable meta-data type.
template <typename T>
struct IsMutableMetaData {
  enum {
    RESULT = !!std::is_convertible<T *, MutableMetaData *>::value
  };
};

// Describes whether some type is an indexable meta-data type.
template <typename T>
struct IsUnifiableMetaData {
  enum {
    RESULT = !!std::is_convertible<T *, IsUnifiableMetaData<T> *>::value
  };
};

// Describes whether some type is a meta-data type.
template <typename T>
struct IsMetaData {
  enum {
    RESULT = IsIndexableMetaData<T>::RESULT ||
             IsMutableMetaData<T>::RESULT ||
             IsUnifiableMetaData<T>::RESULT
  };
};

// Describes whether some pointer is a pointer to some meta-data.
template <typename T>
struct IsMetaDataPointer {
  static_assert(
      IsPointer<T>::RESULT,
      "`MetaDataCast` can only cast to pointer types.");
  typedef typename RemovePointer<T>::Type PointedT0;
  typedef typename RemoveConst<PointedT0>::Type PointedT;

  enum {
    RESULT = IsMetaData<PointedT>::RESULT
  };
};

template <typename T, bool kIsIndexable, bool kIsMutable, bool kIsUnifiable>
struct MetaDataInfoStorage;

template <typename T>
struct MetaDataInfoStorage<T, true, false, false> {
 public:
  static MetaDataInfo kInfo;
};

template <typename T>
struct MetaDataInfoStorage<T, false, true, false> {
 public:
  static MetaDataInfo kInfo;
};

template <typename T>
struct MetaDataInfoStorage<T, false, false, true> {
 public:
  static MetaDataInfo kInfo;
};

// Indexable.
template <typename T>
MetaDataInfo MetaDataInfoStorage<T, true, false, false>::kInfo = {
    nullptr,
    sizeof(T),
    alignof(T),
    0xDEADBEEFUL,
    false,
    &(Initialize<T>),
    &(CopyInitialize<T>),
    &(Destroy<T>),
    &(Hash<T>),
    &(CompareEquals<T>),
    nullptr
};

// Mutable.
template <typename T>
MetaDataInfo MetaDataInfoStorage<T, false, true, false>::kInfo = {
    nullptr,
    sizeof(T),
    alignof(T),
    0xDEADBEEFUL,
    false,
    &(Initialize<T>),
    &(CopyInitialize<T>),
    &(Destroy<T>),
    nullptr,
    nullptr,
    nullptr
};

// Unifyable.
template <typename T>
MetaDataInfo MetaDataInfoStorage<T, false, false, true>::kInfo = {
    nullptr,
    sizeof(T),
    alignof(T),
    0xDEADBEEFUL,
    false,
    &(Initialize<T>),
    &(CopyInitialize<T>),
    &(Destroy<T>),
    nullptr,
    nullptr,
    &(CanUnify<T>)
};

// Get the meta-data info for some indexable meta-data.
template <typename T>
const MetaDataInfo *GetInfo(void) {
  return &(MetaDataInfoStorage<
      T,
      IsIndexableMetaData<T>::RESULT,
      IsMutableMetaData<T>::RESULT,
      IsUnifiableMetaData<T>::RESULT
  >::kInfo);
}

// Register some meta-data with Granary.
void RegisterMetaData(const MetaDataInfo *meta);

// Get some specific meta-data from some generic meta-data.
void *GetMetaData(const MetaDataInfo *info, GenericMetaData *meta);

}  // namespace meta
}  // namespace detail

// Register some meta-data with Granary.
template <typename T>
inline void RegisterMetaData(void) {
  detail::meta::RegisterMetaData(detail::meta::GetInfo<T>());
}

// Cast some generic meta-data into some specific meta-data.
template <
  typename T,
  typename EnableIf<detail::meta::IsMetaDataPointer<T>::RESULT>::Type=0
>
inline T MetaDataCast(GenericMetaData *meta) {
  return reinterpret_cast<T>(
      detail::meta::GetMetaData(
          detail::meta::GetInfo<typename RemovePointer<T>::Type>(), meta));
}

#ifdef GRANARY_INTERNAL
// Meta-data about a basic block.
class GenericMetaData {
 public:
  GRANARY_INTERNAL_DEFINITION
  // Initialize a new meta-data instance. This involves separately initializing
  // the contained meta-data within this generic meta-data.
  explicit GenericMetaData(AppProgramCounter pc);

  // Destroy a meta-data instance. This involves separately destroying the
  // contained meta-data within this generic meta-data.
  ~GenericMetaData(void);

  // Create a copy of some meta-data and return a new instance of the copied
  // meta-data.
  GenericMetaData *Copy(void) const;

  // Hash all serializable meta-data contained within this generic meta-data.
  void Hash(HashFunction *hasher) const;

  // Compare the serializable components of two generic meta-data instances for
  // strict equality.
  bool Equals(const GenericMetaData *meta) const;

  // Check to see if this meta-data can unify with some other generic meta-data.
  UnificationStatus CanUnifyWith(const GenericMetaData *meta) const;

  // Dynamically allocate and free meta-data. These operations are only
  // valid *after* `granary::InitMetaData`, as that sets up the meta-data
  // allocator based on the available meta-data descriptions.
  static void *operator new(std::size_t);
  static void operator delete(void *);

 private:
  GenericMetaData(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(GenericMetaData);
};

// Initialize all meta-data. This finalizes the meta-data structures, which
// determines the runtime layout of the packed meta-data structure.
void InitMetaData(void);

#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_METADATA_H_
