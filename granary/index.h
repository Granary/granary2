/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INDEX_H_
#define GRANARY_INDEX_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "arch/base.h"

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

  // When an indirect CFI targets a translated block, don't copy over its
  // various `next_*` pointer links otherwise that would lead to disastrous
  // behavior.
  void Join(const IndexMetaData *) {}

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

namespace internal {
enum {
  NUM_POINTERS_PER_PAGE = arch::PAGE_SIZE_BYTES / sizeof(void *)
};

// Memory management for any index-related class.
class IndexArrayMem {
 public:
  static void *operator new(std::size_t);
  static void operator delete(void *address);

  static void *operator new[](std::size_t) = delete;
  static void operator delete[](void *) = delete;
};

class MetaDataArray;

}  // namespace internal

// Implements Granary's code cache.
//
// Note: This class does not handle concurrent access/modification. That must
//       be handled at a higher layer.
class Index : public internal::IndexArrayMem {
 public:
  Index(void) = default;

  // Deletes all meta-data arrays and the various stored meta-data.
  ~Index(void);

  // Perform a lookup operation in the code cache index. Lookup operations might
  // not return exact matches, as hinted at by the `status` field of the
  // `IndexFindResponse` structure. This has to do with block unification.
  IndexFindResponse Request(BlockMetaData *meta);

  // Insert a block into the code cache index.
  void Insert(BlockMetaData *meta);

  // Remove all meta-data (from the index) associated with any application
  // code falling in the address range `[begin, end)`. Returns a pointer to
  // a linked list (via `IndexMetaData`) of all removed block meta-data.
  BlockMetaData *RemoveRange(AppPC begin, AppPC end);

 private:
  internal::MetaDataArray *arrays[internal::NUM_POINTERS_PER_PAGE];

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
  explicit LockedIndex(Index *index_)
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
  Index * const index;

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

  inline BlockMetaData *RemoveRange(AppPC begin, AppPC end) {
    return index->RemoveRange(begin, end);
  }

  inline void Insert(BlockMetaData *meta) {
    index->Insert(meta);
  }

  inline ~LockedIndexTransaction(void) {
    lock->WriteRelease();
  }

 private:
  LockedIndexTransaction(void) = delete;

  Index * const index;
  ReaderWriterLock * const lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(LockedIndexTransaction);
};

}  // namespace granary

#endif  // GRANARY_INDEX_H_
