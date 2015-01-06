/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/new.h"
#include "granary/base/string.h"

#include "granary/app.h"
#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/index.h"
#include "granary/metadata.h"

#include "os/memory.h"

namespace granary {
namespace {

// Forward declaration.
class MetaDataArray;

#if 0
// Linked list of un-indexed meta-data.
static BlockMetaData *gUnindexedMeta = nullptr;

// Linked list of invalidated block meta-data.
static BlockMetaData *gInvalidatedMeta = nullptr;
#endif

enum : uintptr_t {
  kDeallocatedMemoryPoison = 0xFA,

  kNumIgnoredBits = 3,
  kMaxFirstIndex = 4096,
  kNumBitsFirstIndex = __builtin_popcount(kMaxFirstIndex - 1),

  kMaxSecondIndex = arch::PAGE_SIZE_BYTES / sizeof(void *),
  kNumBitsSecondIndex = __builtin_popcount(kMaxSecondIndex - 1)
};

// Top-level code cache index. The code cache index is a high-arity, two-level
// radix tree, where indexes into each level are formed by the `AddrToIndex`
// function.
static MetaDataArray * volatile gIndex[kMaxFirstIndex] = {nullptr};

// Top-level locks of code cache sub-levels.
static os::Lock gSecondLevelLocks[kMaxSecondIndex];

// Represents the index levels for some meta-data.
struct MetaDataIndex {
  uintptr_t first;
  uintptr_t second;

  inline bool operator!=(const MetaDataIndex &that) const {
    return first != that.first || second != that.second;
  }
};

// Converts an program counter into a two-tiered index into the code cache.
static MetaDataIndex IndexOf(AppPC pc) {
  const auto addr = reinterpret_cast<uintptr_t>(pc);
  return {
    (addr >> (kNumIgnoredBits + kNumBitsSecondIndex)) % kMaxFirstIndex,
    (addr >> kNumIgnoredBits) % kMaxSecondIndex
  };
}

// Returns the application program counter associated with some block
// meta-data.
static AppPC AppPCOf(const BlockMetaData *meta) {
  return MetaDataCast<const AppMetaData *>(meta)->start_pc;
}

// Second-level index of meta-data. This is an array of buckets.
class MetaDataArray {
 public:
  // Deletes all meta-data linked into this array.
  ~MetaDataArray(void) {
    for (auto meta : metas) {
      while (meta) {
        auto index_meta = MetaDataCast<IndexMetaData *>(meta);
        auto next_meta = index_meta->next;
        delete meta;
        meta = next_meta;
      }
    }
  }

  static void *operator new(std::size_t) {
    return memset(os::AllocateDataPages(1), 0, arch::PAGE_SIZE_BYTES);
  }

  static void operator delete(void *address) {
    memset(address, kDeallocatedMemoryPoison, arch::PAGE_SIZE_BYTES);
    return os::FreeDataPages(address, 1);
  }

  // Array of meta-data buckets. Marked as `volatile` because we support
  // concurrent reads and writers, where readers *don't* synchronize with
  // writers, and so some inconsistencies might be seen.
  const BlockMetaData * volatile metas[kMaxSecondIndex];
};

static_assert(sizeof(MetaDataArray) == arch::PAGE_SIZE_BYTES,
              "The size of `MetaDataArray` must be exactly one page.");

// Match some meta-data that we are search for (`search`) against a linked
// list of potential meta-data.
static IndexFindResponse MatchMetaData(const BlockMetaData *ls,
                                       const BlockMetaData *search) {
  IndexFindResponse response = {kUnificationStatusReject, nullptr};
  for (auto meta : IndexMetaDataIterator(ls)) {
    if (!search->Equals(meta)) continue;
    switch (search->CanUnifyWith(ls)) {
      case kUnificationStatusAccept:
        response.status = kUnificationStatusAccept;
        response.meta = meta;
        return response;

      case kUnificationStatusAdapt:
        if (kUnificationStatusAdapt != response.status) {
          response.status = kUnificationStatusAdapt;
          response.meta = meta;
        }
        break;

      case kUnificationStatusReject:
        break;
    }
  }
  return response;
}

}  // namespace

// Initialize the code cache index.
void InitIndex(void) {

}

// Exit the code cache index.
void ExitIndex(void) {
  for (auto &array : gIndex) {
    if (array) {
      delete array;
      array = nullptr;
    }
  }
}

// Perform a lookup operation in the code cache index. Lookup operations might
// not return exact matches, as hinted at by the `status` field of the
// `IndexFindResponse` structure. This has to do with block unification.
IndexFindResponse FindMetaDataInIndex(const BlockMetaData *meta) {
  if (GRANARY_UNLIKELY(!meta)) return {kUnificationStatusReject, nullptr};

  auto index_meta = MetaDataCast<const IndexMetaData *>(meta);
  GRANARY_ASSERT(!index_meta->next);

  auto pc = AppPCOf(meta);
  GRANARY_ASSERT(nullptr != pc);

  auto indices = IndexOf(pc);
  if (auto array = gIndex[indices.first]) {
    if (auto metas = array->metas[indices.second]) {
      return MatchMetaData(metas, meta);
    }
  }
  return {kUnificationStatusReject, nullptr};  // Not in the index
}

// Insert a block into the code cache index.
void AddMetaDataToIndex(BlockMetaData *meta) {
  GRANARY_ASSERT(nullptr != meta);

  auto index_meta = MetaDataCast<const IndexMetaData *>(meta);
  GRANARY_ASSERT(nullptr == index_meta->next);

  auto pc = AppPCOf(meta);
  GRANARY_ASSERT(nullptr != pc);

  auto indices = IndexOf(pc);
  os::LockedRegion locker(&(gSecondLevelLocks[indices.second]));

  auto &array(gIndex[indices.first]);
  if (GRANARY_UNLIKELY(!array)) array = new MetaDataArray;

  auto &metas(array->metas[indices.second]);

  index_meta->next = metas;
  metas = meta;
}

}  // namespace granary
