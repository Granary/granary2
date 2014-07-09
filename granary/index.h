/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INDEX_H_
#define GRANARY_INDEX_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/pc.h"

#include "granary/metadata.h"

namespace granary {

// Code cache index-specific meta-data.
class IndexMetaData : public MutableMetaData<IndexMetaData> {
 public:
  inline IndexMetaData(void)
      : next(nullptr) {}

  // Don't copy anything over.
  inline IndexMetaData(const IndexMetaData &)
      : next(nullptr) {}

  // The next meta-data chunk stored in the same spot in the code cache index.
  //
  // Note: If this is non-null, then this block is stored in the code cache
  //       index. This works because some of the `next` pointers will be
  //       tombstones.
  BlockMetaData *next;
};

// Response returned from a lookup request in the code cache index.
struct IndexFindResponse {
 public:
  // What type of match was this?
  //    1) ACCEPT     - Exact match.
  //    2) ADAOT      - Close enough that we can make it work via "compensation
  //                    code".
  //    3) REJECT     - No matches. No matches does not imply that no other
  //                    versions of this block exist, merely that no other
  //                    versions with meta-data that is suitable exist.
  UnificationStatus status;

  // Meta-data that we found for our query.
  BlockMetaData *meta;
};

// Interface for operating on a code cache index.
class IndexInterface {
 public:
  virtual ~IndexInterface(void);

  // Perform a lookup operation in the code cache index. Lookup operations might
  // not return exact matches, as hinted at by the `status` field of the
  // `IndexFindResponse` structure. This has to do with block unification.
  virtual IndexFindResponse Request(BlockMetaData *meta) = 0;

  // Insert a block into the code cache index.
  virtual void Insert(BlockMetaData *meta) = 0;
};

namespace internal {

enum {
  NUM_POINTERS_PER_PAGE = arch::PAGE_SIZE_BYTES / sizeof(void *)
};

class MetaDataArray {
 public:
  // Deletes all meta-data linked into this array.
  ~MetaDataArray(void);

  BlockMetaData *metas[NUM_POINTERS_PER_PAGE];

  static void *operator new(std::size_t);
  static void operator delete(void *address);
};

static_assert(sizeof(MetaDataArray) == arch::PAGE_SIZE_BYTES,
              "The size of `Index` must be exactly one page.");

}  // namespace internal

// Implements Granary's code cache.
//
// Note: This class does not handle concurrent access/modification. That must
//       be handled at a higher layer.
class Index : public IndexInterface {
 public:
  Index(void) = default;

  // Deletes all meta-data arrays and the various stored meta-data.
  virtual ~Index(void);

  // Perform a lookup operation in the code cache index. Lookup operations might
  // not return exact matches, as hinted at by the `status` field of the
  // `IndexFindResponse` structure. This has to do with block unification.
  virtual IndexFindResponse Request(BlockMetaData *meta) override;

  // Insert a block into the code cache index.
  virtual void Insert(BlockMetaData *meta) override;

  static void *operator new(std::size_t);
  static void operator delete(void *address);

  static void *operator new[](std::size_t) = delete;
  static void operator delete[](void *) = delete;

 private:
  // The `- 1` is to account for a vtable pointer.
  internal::MetaDataArray *arrays[internal::NUM_POINTERS_PER_PAGE - 1];

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Index);
};

static_assert(sizeof(Index) == arch::PAGE_SIZE_BYTES,
              "The size of `Index` must be exactly one page.");

// Forward declaration.
class LockedIndexTransaction;

// Represents a locked code cache index that is safe to use in a multi-threaded
// environment.
class LockedIndex {
 public:
  explicit LockedIndex(IndexInterface *index_)
      : index(index_),
        index_lock() {}

  ~LockedIndex(void) {
    delete index;
  }

  // Perform a lookup in the index. Lookups can execute concurrently.
  inline IndexFindResponse Request(BlockMetaData *meta) {
    ReadLocked locker(&index_lock);
    return index->Request(meta);
  }

 private:
  friend class LockedIndexTransaction;

  LockedIndex(void) = delete;

  // Index backing this `LockedIndex`.
  IndexInterface * const index;

  // Reader/write lock that guards the index. This allows concurrent reads, but
  // gives writers mutual exclusion.
  ReaderWriterLock index_lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(LockedIndex);
};

// Allows multiple operations to be "atomically" performed on a code cache
// index.
class LockedIndexTransaction {
 public:
  explicit LockedIndexTransaction(LockedIndex *index_)
      : index(index_->index),
        lock(&(index_->index_lock)) {
    lock->WriteAcquire();
  }

  inline IndexFindResponse Request(BlockMetaData *meta) {
    return index->Request(meta);
  }

  inline void Insert(BlockMetaData *meta) {
    index->Insert(meta);
  }

  inline ~LockedIndexTransaction(void) {
    lock->WriteRelease();
  }

 private:
  LockedIndexTransaction(void) = delete;

  IndexInterface * const index;
  ReaderWriterLock * const lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(LockedIndexTransaction);
};

}  // namespace granary

#endif  // GRANARY_INDEX_H_
