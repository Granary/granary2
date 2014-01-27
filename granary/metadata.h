/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_METADATA_H_
#define GRANARY_METADATA_H_

#include "granary/base/base.h"
#include "granary/base/new.h"
#include "granary/base/hash.h"

namespace granary {

// Forward declarations.
class GenericMetaData;

// Serializable meta-data (i.e. immutable once committed to the code cache)
// must implement the `Hash` and `Equals` methods, and extend this template
// using CRTP.
template <typename T>
struct SerializableMetaData {
 public:
  void Hash(HashFunction *hasher) const;
  bool Equals(const T *that) const;
};

// Mutable meta-data (i.e. mutable even after committed to the code cache)
// must extend this base class.
struct MutableMetaData {};

// TODO(pag): How to eventually handle static instrumentation with mutable
//            meta-data?

// Meta-data that Granary maintains about all basic blocks. This meta-data
// guides the translation process.
//
// This meta-data is registered in `granary::InitMetaData`.
struct TranslatioMetaData : SerializableMetaData<TranslatioMetaData> {
  union {
    struct {
      // Should function returns be translated or run natively. This is related
      // to transparency and comprehensiveness, but can also be used to
      // implement fast function returns when instrumentation isn't being
      // transparent.
      bool native_return:1;

      // Should this basic block be run natively? I.e. should be just run the
      // app code instead of instrumenting it?
      bool run_natively:1;

      // Should we expect that the target is not decodable? For example, the
      // Linux kernel's `BUG_ON` macro generates `ud2` instructions. We treat
      // these as dead ends, and go native on them so that we can see the right
      // debugging info. Similarly, debugger breakpoints inject `int3`s into
      // the code. In order to properly trigger those breakpoints, we go native
      // before executing those breakpoints.
      bool cant_decode:1;

      // Should this block's address be committed to the code cache index? If
      // a block is marked as private then it can be specially treated by tools,
      // e.g. for performing trace-specific optimizations.
      bool is_private:1;
    };

    uint8_t raw_bits;
  };

  // Hash the translation meta-data.
  void Hash(HashFunction *hasher) const;

  // Compare two translation meta-data objects for equality.
  bool Equals(const TranslatioMetaData *meta) const;
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

  // Is this meta-data serializable (and so is treated as immutable once
  // committed to the code cache) or is it mutable, and potentially changing
  // over time.
  bool is_serializable;

  // Generic ways for Granary to interact with this meta-data.
  void (* const initialize)(void *);
  void (* const copy_initialize)(void *, const void *);
  void (* const destroy)(void *);
  void (* const hash)(HashFunction *, const void *);
  bool (* const compare_equals)(const void *, const void *);

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

// Get the meta-data info for some serializable meta-data.
template <
  typename T,
  typename EnableIf<
    std::is_convertible<T *, SerializableMetaData<T> *>::value &&
    !std::is_convertible<T *, MutableMetaData *>::value
  >::Type=0
>
const MetaDataInfo *GetInfo(void) {
  static MetaDataInfo kInfo = {
      nullptr,
      sizeof(T),
      alignof(T),
      0xDEADBEEFUL,
      false,
      true,
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
    !std::is_convertible<T *, SerializableMetaData<T> *>::value &&
    std::is_convertible<T *, MutableMetaData *>::value
  >::Type=0
>
const MetaDataInfo *GetInfo(void) {
  static MetaDataInfo kInfo = {
      nullptr,
      sizeof(T),
      alignof(T),
      0xDEADBEEFUL,
      false,
      false,
      &(Initialize<T>),
      &(CopyInitialize<T>),
      &(Destroy<T>),
      nullptr,
      nullptr
  };
  return &kInfo;
}

// Register some meta-data with Granary.
void RegisterMetaData(const MetaDataInfo *meta);

}  // namespace meta
}  // namespace detail

// Register some meta-data with Granary.
template <typename T>
inline void RegisterMetaData(void) {
  detail::meta::RegisterMetaData(detail::meta::GetInfo<T>());
}

#ifdef GRANARY_INTERNAL
// Meta-data about a basic block.
class GenericMetaData {
 public:
  // Initialize a new meta-data instance. This involves separately initializing
  // the contained meta-data within this generic meta-data.
  GenericMetaData(void);

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

  // Dynamically allocate and free meta-data. These operations are only
  // valid *after* `granary::InitMetaData`, as that sets up the meta-data
  // allocator based on the available meta-data descriptions.
  static void *operator new(std::size_t);
  static void operator delete(void *);

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(GenericMetaData);
};

// Initialize all meta-data. This finalizes the meta-data structures, which
// determines the runtime layout of the packed meta-data structure.
void InitMetaData(void);

#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_METADATA_H_
