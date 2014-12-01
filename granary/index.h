/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INDEX_H_
#define GRANARY_INDEX_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/pc.h"

#include "granary/metadata.h"

#include "os/lock.h"

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
class MetaDataArray;
}  // namespace internal

// Implements Granary's code cache.
//
// Note: This class does allows readers to execute concurrently with respect
class Index {
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

  static void *operator new(std::size_t);
  static void operator delete(void *address);

  static void *operator new[](std::size_t) = delete;
  static void operator delete[](void *) = delete;

 private:
  // Array of arrays of lists of meta-data.
  internal::MetaDataArray *arrays[internal::NUM_POINTERS_PER_PAGE];

  // Array of locks for *modifying* the last-level lists of meta-data.
  os::Lock second_level_locks[internal::NUM_POINTERS_PER_PAGE];

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Index);
};

static_assert(sizeof(Index) <= 2 * arch::PAGE_SIZE_BYTES,
              "The size of `Index` must be exactly one page.");
}  // namespace granary

#endif  // GRANARY_INDEX_H_
