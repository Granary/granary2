/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_METADATA_H_
#define GRANARY_METADATA_H_

#include "granary/base/base.h"
#include "granary/base/container.h"
#include "granary/base/new.h"
#include "granary/base/hash.h"
#include "granary/base/operator.h"
#include "granary/base/type_trait.h"
#include "granary/base/pc.h"

// Used to explicitly instantiate this so that it is available to shared
// libraries.
#ifdef GRANARY_EXTERNAL
# define GRANARY_SHARE_METADATA(meta_class) \
    template <> \
    meta_class *MetaDataCast<meta_class *>(BlockMetaData *meta);
#else
# define GRANARY_SHARE_METADATA(meta_class)
#endif

namespace granary {

// Forward declarations.
class BlockMetaData;
GRANARY_INTERNAL_DEFINITION class MetaDataManager;

// All types of meta-data.
template <typename T>
class ToolMetaData {
 public:

  // Join some meta-data associated with an existing basic block (`existing`)
  // with the meta-data template associated with some indirect basic block
  // (`indirect`). The default behavior here to to inherit all information from
  // the existing block's meta-data.
  void Join(const T *existing, const T *indirect) {
    GRANARY_UNUSED(indirect);
    CopyConstruct<T>(this, existing);
  }
};

// Serializable meta-data (i.e. immutable once committed to the code cache)
// must implement the `Hash` and `Equals` methods, and extend this template
// using CRTP.
template <typename T>
class IndexableMetaData : public ToolMetaData<T> {
 public:
  void Hash(HashFunction *hasher) const;
  bool Equals(const T *that) const;
};

// Mutable meta-data (i.e. mutable even after committed to the code cache)
// must extend this base class.
template <typename T>
class MutableMetaData : public ToolMetaData<T> {};

// Used to decide whether two pieces of unifiable meta-data can unify.
enum class UnificationStatus {
  ACCEPT = 0, // Unifies perfectly.
  ADAPT = 2,  // Does not unify perfectly, but can be adapted.
  REJECT = 1  // Cannot be unified / adapted.
};

// Unifiable meta-data, i.e. meta-data that behaves a bit like indexable meta-
// data, but doesn't directly participate in the indexing process. The idea here
// is that sometimes we want to generate new versions of basic blocks, and other
// times we want to be able to re-use old versions, but the old versions aren't
// necessarily perfectly suited, so we need to adapt to them.
template <typename T>
class UnifiableMetaData : public ToolMetaData<T> {
 public:
  UnificationStatus CanUnifyWith(const T *that) const;
};

#ifdef GRANARY_INTERNAL
// Meta-data that Granary maintains about all basic blocks that are committed to
// the code cache. This is meta-data is private to Granary and therefore not
// exposed (directly) to tools.
struct CacheMetaData : public MutableMetaData<CacheMetaData> {

  // Initialize Granary's internal translation cache meta-data.
  CacheMetaData(void);

  // Where this block is located in the code cache.
  CachePC cache_pc;

  // TODO(pag): Encoded size?
  // TODO(pag): Interrupt delay regions? Again: make this a command-line
  //            option, that registers separate meta-data.
  // TODO(pag): Cache PCs to native PCs? If doing this, perhaps make it a
  //            separate kind of meta-data that is only registered if a certain
  //            command-line option is specified. That way, the overhead of
  //            recording the extra info is reduced. Also, consider a delta
  //            encoding, (e.g. https://docs.google.com/document/d/
  //            1lyPIbmsYbXnpNj57a261hgOYVpNRcgydurVQIyZOz_o/pub).
  // TODO(pag): Things that are kernel-specific (e.g. exc. table, delay regions)
  //            should go in their own cache data structures.
};
#endif  // GRANARY_INTERNAL

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
    RESULT = !!std::is_convertible<T *, MutableMetaData<T> *>::value
  };
};

// Describes whether some type is an indexable meta-data type.
template <typename T>
struct IsUnifiableMetaData {
  enum {
    RESULT = !!std::is_convertible<T *, UnifiableMetaData<T> *>::value
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
      IsPointer<T>(),
      "`MetaDataCast` can only cast to pointer types.");
  typedef typename RemovePointer<T>::Type PointedT0;
  typedef typename RemoveConst<PointedT0>::Type PointedT;

  enum {
    RESULT = IsMetaData<PointedT>::RESULT
  };
};

template <typename T, bool kIsIndexable, bool kIsMutable, bool kIsUnifiable>
struct MetaDataDescriptor;

// Describes some generic meta-data in a way that Granary understands.
class MetaDataDescription {
 public:
  // Globally unique ID for this meta-data description. Granary internally
  // uses this ID to operate with the same meta-data, but registered within
  // different environments.
  GRANARY_CONST int id;

  // Where in the generic meta-data is this specific meta-data.
  const size_t size;
  const size_t align;

  // Virtual table of operations on the different classes of meta-data.
  void (* const initialize)(void *);
  void (* const copy_initialize)(void *, const void *);
  void (* const destroy)(void *);
  void (* const hash)(HashFunction *, const void *);
  bool (* const compare_equals)(const void *, const void *);
  UnificationStatus (* const can_unify)(const void *, const void *);

  template <typename T>
  static constexpr MetaDataDescription *Get(void) {
    return &(MetaDataDescriptor<
      T,
      IsIndexableMetaData<T>::RESULT,
      IsMutableMetaData<T>::RESULT,
      IsUnifiableMetaData<T>::RESULT
    >::kDescription);
  }
} __attribute__((packed));

// Descriptor for some indexable meta-data.
template <typename T>
struct MetaDataDescriptor<T, true, false, false> {
 public:
  static MetaDataDescription kDescription GRANARY_EARLY_GLOBAL;
};

// Descriptor for some mutable meta-data.
template <typename T>
struct MetaDataDescriptor<T, false, true, false> {
 public:
  static MetaDataDescription kDescription GRANARY_EARLY_GLOBAL;
};

// Descriptor for some unifiable meta-data.
template <typename T>
struct MetaDataDescriptor<T, false, false, true> {
 public:
  static MetaDataDescription kDescription GRANARY_EARLY_GLOBAL;
};

namespace detail {

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
}  // namespace detail

// Indexable.
template <typename T>
MetaDataDescription MetaDataDescriptor<T, true, false, false>::kDescription = {
    -1,
    sizeof(T),
    alignof(T),
    &(Construct<T>),
    &(CopyConstruct<T>),
    &(Destruct<T>),
    &(detail::Hash<T>),
    &(detail::CompareEquals<T>),
    nullptr
};

// Mutable.
template <typename T>
MetaDataDescription MetaDataDescriptor<T, false, true, false>::kDescription = {
    -1,
    sizeof(T),
    alignof(T),
    &(Construct<T>),
    &(CopyConstruct<T>),
    &(Destruct<T>),
    nullptr,
    nullptr,
    nullptr
};

// Unifyable.
template <typename T>
MetaDataDescription MetaDataDescriptor<T, false, false, true>::kDescription = {
    -1,
    sizeof(T),
    alignof(T),
    &(Construct<T>),
    &(CopyConstruct<T>),
    &(Destruct<T>),
    nullptr,
    nullptr,
    &(detail::CanUnify<T>)
};


// Meta-data about a basic block.
class BlockMetaData {
 public:
  BlockMetaData(void) = delete;

  // Initialize a new meta-data instance. This involves separately initializing
  // the contained meta-data within this generic meta-data.
  GRANARY_INTERNAL_DEFINITION BlockMetaData(MetaDataManager *manager_);

  // Destroy a meta-data instance. This involves separately destroying the
  // contained meta-data within this generic meta-data.
  ~BlockMetaData(void) GRANARY_EXTERNAL_DELETE;

  // Create a copy of some meta-data and return a new instance of the copied
  // meta-data.
  GRANARY_INTERNAL_DEFINITION BlockMetaData *Copy(void) const;

  // Hash all serializable meta-data contained within this generic meta-data.
  GRANARY_INTERNAL_DEFINITION void Hash(HashFunction *hasher) const;

  // Compare the serializable components of two generic meta-data instances for
  // strict equality.
  GRANARY_INTERNAL_DEFINITION bool Equals(const BlockMetaData *meta) const;

  // Check to see if this meta-data can unify with some other generic meta-data.
  GRANARY_INTERNAL_DEFINITION
  UnificationStatus CanUnifyWith(const BlockMetaData *meta) const;

  // Cast some generic meta-data into some specific meta-data.
  void *Cast(MetaDataDescription *desc);

  // Free this metadata.
  GRANARY_INTERNAL_DEFINITION static void operator delete(void *address);

  // Manager for this meta-data instance.
  GRANARY_INTERNAL_DEFINITION MetaDataManager * const manager;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BlockMetaData);
};

// Cast some generic meta-data into some specific meta-data.
template <typename T, typename EnableIf<IsMetaDataPointer<T>::RESULT>::Type=0>
inline T MetaDataCast(BlockMetaData *meta) {
  typedef typename RemovePointer<T>::Type M;
  return reinterpret_cast<T>(meta->Cast(MetaDataDescription::Get<M>()));
}

#ifdef GRANARY_INTERNAL
// Manages all metadata within a particular environment.
class MetaDataManager {
 public:
  // Initialize an empty meta-data manager.
  MetaDataManager(void);

  ~MetaDataManager(void);

  // Register some meta-data with Granary. This is a convenience method around
  // the `Register` method that operates directly on a meta-data description.
  template <typename T>
  inline void Register(void) {
    Register(const_cast<MetaDataDescription *>(MetaDataDescription::Get<T>()));
  }

  // Register some meta-data with Granary.
  void Register(MetaDataDescription *desc);

  // Allocate some meta-data. If the manager hasn't been finalized then this
  // returns nullptr.
  BlockMetaData *Allocate(void);

  // Free some metadata.
  void Free(BlockMetaData *meta);

 private:
  friend class BlockMetaData;

  enum {
    // Upper bound on the number of registerable meta-data instances.
    MAX_NUM_MANAGED_METADATAS = 32
  };

  // Finalizes the meta-data structures, which determines the runtime layout
  // of the packed meta-data structure. Once
  void Finalize(void);

  // Initialize the allocator for meta-data managed by this manager.
  void InitAllocator(void);

  // Size of the overall metadata structure managed by this manager.
  size_t size;

  // Whether or not this metadata has been finalized.
  bool is_finalized;

  // Info on all registered meta-data within this manager. These are indexed
  // by the `MetaDataDescription::id` field.
  MetaDataDescription *descriptions[MAX_NUM_MANAGED_METADATAS];

  // Offsets of each meta-data object within the block meta-data block. These
  // are indexed by the `MetaDataDescription::id` field.
  size_t offsets[MAX_NUM_MANAGED_METADATAS];

  // Slab allocator for allocating meta-data objects.
  Container<internal::SlabAllocator> allocator;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(MetaDataManager);
};

#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_METADATA_H_
