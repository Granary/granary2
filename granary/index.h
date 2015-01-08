/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INDEX_H_
#define GRANARY_INDEX_H_

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/pc.h"

#include "granary/metadata.h"

#include "os/lock.h"

namespace granary {

#ifdef GRANARY_INTERNAL

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
  void Join(const IndexMetaData &) {}

  // The next meta-data chunk stored in the same spot in the code cache index.
  //
  // Note: If this is non-null, then this block is stored in the code cache
  //       index. This works because some of the `next` pointers will be
  //       tombstones.
  mutable const BlockMetaData *next;
};

typedef MetaDataLinkedListIterator<IndexMetaData> IndexMetaDataIterator;

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
  const BlockMetaData *meta;
};

// Initialize the code cache index.
void InitIndex(void);

// Exit the code cache index.
void ExitIndex(void);

// Perform a lookup operation in the code cache index. Lookup operations might
// not return exact matches, as hinted at by the `status` field of the
// `IndexFindResponse` structure. This has to do with block unification.
IndexFindResponse FindMetaDataInIndex(const BlockMetaData *meta);

// Insert a block into the code cache index.
void AddMetaDataToIndex(BlockMetaData *meta);

// Insert a block's meta-data into the global list of all meta-data.
void AddMetaDataToLog(BlockMetaData *meta);

#endif  // GRANARY_INTERNAL

enum IndexedStatus {
  kMetaDataIndexed,
  kMetaDataUnindexed
};

namespace detail {

// Iterates over all meta-data.
void ForEachMetaData(
    const std::function<void(const BlockMetaData *, IndexedStatus)> &func);

}  // namespace detail

// Iterates over all meta-data.
template <typename FuncT>
inline static void ForEachMetaData(FuncT func) {
  detail::ForEachMetaData(std::cref(func));
}

}  // namespace granary

#endif  // GRANARY_INDEX_H_
