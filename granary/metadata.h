/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_METADATA_H_
#define GRANARY_METADATA_H_

#include "granary/base/base.h"
#include "granary/base/new.h"
#include "granary/base/hash.h"

namespace granary {

// Forward declarations.
class GenericMetaData;

// Interfaces that meta-data must follow.
class SerializableMetaData {
 public:
  void Hash(HashFunction *hasher) const;
  bool Equals(const GenericMetaData *meta) const;
};

class MutableMetaData {};

namespace detail {
namespace meta {

// Describes some generic meta-data in a way that Granary understands.
struct MetaDataInfo {
  const MetaDataInfo * const next;

  // Where in the generic meta-data is this specific meta-data.
  const size_t size;
  const size_t align;
  const int offset;

  // Is this meta-data serializable (and so is treated as immutable once
  // committed to the code cache) or is it mutable, and potentially changing
  // over time.
  enum {
    MUTABLE,
    SERIALIZABLE
  } const kind;

  // Generic ways for Granary to interact with this meta-data.
  void (* const initialize)(void *);
  void (* const copy_initialize)(void *, void *);
  void (* const destroy)(void *);
  void (* const hash)(HashFunction *, const void *);
  bool (* const compare_equals)(const void *, const void *);
};

// Initialize some meta-data.
template <typename T>
void Initialize(void *mem) {
  new (mem) T;
}

// Initialize some meta-data.
template <typename T>
void CopyInitialize(void *mem, void *that) {
  new (mem) T(*reinterpret_cast<T *>(that));
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

// Assume that stateful meta-data is equivalent, which can be expressed as
// not contributing any new information to the hasher.
void FakeHash(HashFunction *, const void *);

// Assume all stateful meta-data is equivalent.
bool FakeCompareEquals(const void *, const void *);

// Get the meta-data info for some serializable meta-data.
template <
  typename T,
  typename EnableIf<
    std::is_convertible<T *, SerializableMetaData *>::value &&
    !std::is_convertible<T *, MutableMetaData *>::value
  >::Type=0
>
const MetaDataInfo *GetInfo(void) {
  static MetaDataInfo kInfo = {
      nullptr,
      sizeof(T),
      alignof(T),
      -1,
      MetaDataInfo::SERIALIZABLE,
      &(Initialize<T>),
      &(CopyInitialize<T>),
      &(Destroy<T>),
      &(Hash<T>),
      &(CompareEquals<T>)
  };
  return &kInfo;
}

// Get the meta-data info for some mutable meta-data.
template <
  typename T,
  typename EnableIf<
    !std::is_convertible<T *, SerializableMetaData *>::value &&
    std::is_convertible<T *, MutableMetaData *>::value
  >::Type=0
>
const MetaDataInfo *GetInfo(void) {
  static MetaDataInfo kInfo = {
      nullptr,
      sizeof(T),
      alignof(T),
      -1,
      MetaDataInfo::MUTABLE,
      &(Initialize<T>),
      &(CopyInitialize<T>),
      &(Destroy<T>),
      &(FakeHash),
      &(FakeCompareEquals)
  };
  return &kInfo;
}

}  // namespace meta
}  // namespace detail


#ifdef GRANARY_INTERNAL
// Meta-data about a basic block.
class GenericMetaData {
 public:
  GRANARY_INTERNAL_DEFINITION
  GenericMetaData(void) = default;

  GenericMetaData *Copy(void) const;
  void Hash(HashFunction *hasher) const;
  bool Equals(const GenericMetaData *meta) const;

  GRANARY_INTERNAL_DEFINITION
  static GenericMetaData *CopyOrCreate(const GenericMetaData *meta);

  GRANARY_INTERNAL_DEFINITION
  static void *operator new(std::size_t) {
    return nullptr;  // TODO(pag): Implement this.
  }

  GRANARY_INTERNAL_DEFINITION
  static void operator delete(void *) {
    // TODO(pag): Implement this.
  }
 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(GenericMetaData);
};
#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_METADATA_H_
