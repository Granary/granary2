/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"

#include "granary/index.h"
#include "granary/cache.h"
#include "granary/memory.h"
#include "granary/metadata.h"
#include "granary/module.h"

namespace granary {
namespace {

// Tombstone pointer representing the last meta-data block in a meta-data array.
static BlockMetaData * const META_ARRAY_END = \
    reinterpret_cast<BlockMetaData *>(1ULL);

}  // namespace

BlockMetaData *IndexMetaData::Next(void) const {
  if (GRANARY_UNLIKELY(!next_rel32)) return nullptr;
  if (GRANARY_UNLIKELY(1 == next_rel32)) return META_ARRAY_END;
  return reinterpret_cast<BlockMetaData *>(
      reinterpret_cast<intptr_t>(this) + next_rel32);
}

BlockMetaData *IndexMetaData::NextShadow(void) const {
  if (GRANARY_UNLIKELY(!next_shadow_rel32)) return nullptr;
  if (GRANARY_UNLIKELY(1 == next_shadow_rel32)) return META_ARRAY_END;
  return reinterpret_cast<BlockMetaData *>(
      reinterpret_cast<intptr_t>(this) + next_shadow_rel32);
}

void IndexMetaData::SetNext(BlockMetaData *next) {
  if (META_ARRAY_END == next) {
    next_rel32 = 1;
  } else {
    auto diff = reinterpret_cast<intptr_t>(next) -
                reinterpret_cast<intptr_t>(this);
    next_rel32 = static_cast<int32_t>(diff);
  }
}

void IndexMetaData::SetNextShadow(BlockMetaData *next) {
  if (META_ARRAY_END == next) {
    next_shadow_rel32 = 1;
  } else {
    auto diff = reinterpret_cast<intptr_t>(next) -
                reinterpret_cast<intptr_t>(this);
    next_shadow_rel32 = static_cast<int32_t>(diff);
  }
}

IndexInterface::~IndexInterface(void) {}

namespace internal {

enum {
  DEALLOCATED_MEMORY_POISON = 0xBC
};

void *IndexArrayMem::operator new(std::size_t) {
  return memset(AllocatePages(1), 0, arch::PAGE_SIZE_BYTES);
}

void IndexArrayMem::operator delete(void *address) {
  memset(address, DEALLOCATED_MEMORY_POISON, arch::PAGE_SIZE_BYTES);
  return FreePages(address, 1);
}


// Deletes all meta-data linked into this array.
MetaDataArray::~MetaDataArray(void) {
  for (auto meta : metas) {
    while (meta && meta != META_ARRAY_END) {
      auto index_meta = MetaDataCast<IndexMetaData *>(meta);
      auto next_meta = index_meta->Next();
      delete meta;
      meta = next_meta;
    }
  }
}

}  // namespace internal
namespace {

enum {
  NUM_IGNORED_BITS = 3,
  MAX_FIRST_INDEX = internal::NUM_POINTERS_PER_PAGE - 1,
  NUM_BITS_PER_ARRAY = __builtin_popcount(MAX_FIRST_INDEX)
};

// Represents the index levels for some meta-data.
struct MetaDataIndex {
  uintptr_t first;
  uintptr_t second;
};

// Returns the index into the code cache for a given piece of meta-data.
static MetaDataIndex GetIndex(BlockMetaData *meta) {
  const auto module_meta = MetaDataCast<ModuleMetaData *>(meta);
  const auto app_addr = reinterpret_cast<uintptr_t>(module_meta->start_pc);
  return {
    (app_addr >> (NUM_IGNORED_BITS + NUM_BITS_PER_ARRAY)) % MAX_FIRST_INDEX,
    (app_addr >> NUM_IGNORED_BITS) % internal::NUM_POINTERS_PER_PAGE
  };
}

// Returns the index into the code cache for a given piece of meta-data.
static MetaDataIndex GetShadowIndex(CacheMetaData *meta) {
  const auto cache_addr = reinterpret_cast<uintptr_t>(meta->start_pc);
  return {
    (cache_addr >> (NUM_IGNORED_BITS + NUM_BITS_PER_ARRAY)) % MAX_FIRST_INDEX,
    (cache_addr >> NUM_IGNORED_BITS) % internal::NUM_POINTERS_PER_PAGE
  };
}

// Match some meta-data that we are search for (`search`) against a linked
// list of potential meta-data.
static IndexFindResponse MatchMetaData(BlockMetaData *ls,
                                       BlockMetaData *search) {
  IndexFindResponse response = {
    UnificationStatus::REJECT,
    nullptr
  };
  while (ls != META_ARRAY_END) {
    if (search->Equals(ls)) {
      switch (search->CanUnifyWith(ls)) {
        case UnificationStatus::ACCEPT:
          response.status = UnificationStatus::ACCEPT;
          response.meta = ls;
          return response;

        case UnificationStatus::ADAPT:
          if (UnificationStatus::ADAPT != response.status) {
            response.status = UnificationStatus::ADAPT;
            response.meta = ls;
          }
          break;

        case UnificationStatus::REJECT:
          break;
      }
    }
    ls = MetaDataCast<IndexMetaData *>(ls)->Next();
  }
  return response;
}

// Look for an exact match of some "shadow" meta-data. What this really means
// is go look for some meta-data whose `CacheMetaData::start_pc` exactly
// matches that of `search`.
static IndexFindResponse MatchShadowMetaData(BlockMetaData *ls,
                                             const CacheMetaData *search) {
  while (ls != META_ARRAY_END) {
    auto cache_ls = MetaDataCast<CacheMetaData *>(ls);
    if (cache_ls->start_pc == search->start_pc) {
      return {UnificationStatus::ACCEPT, ls};
    }
    ls = MetaDataCast<IndexMetaData *>(ls)->Next();
  }
  return {UnificationStatus::REJECT, nullptr};
}

}  // namespace

Index::~Index(void) {
  for (auto array : arrays) {
    delete array;
  }
}

// Perform a lookup operation in the code cache index. Lookup operations might
// not return exact matches, as hinted at by the `status` field of the
// `IndexFindResponse` structure. This has to do with block unification.
IndexFindResponse Index::Request(BlockMetaData *meta) {
  if (GRANARY_UNLIKELY(!meta)) {
    return {UnificationStatus::REJECT, meta};
  }

  auto index_meta = MetaDataCast<IndexMetaData *>(meta);
  if (index_meta->Next()) {
    return {UnificationStatus::ACCEPT, meta};  // It must be in the index.
  }

  const auto index = GetIndex(meta);
  if (auto array = arrays[index.first]) {
    if (auto metas = array->metas[index.second]) {
      return MatchMetaData(metas, meta);
    }
  }
  return {UnificationStatus::REJECT, nullptr};  // Not in the index
}

// Insert a block into the code cache index.
void Index::Insert(BlockMetaData *meta) {
  auto index_meta = MetaDataCast<IndexMetaData *>(meta);
  if (index_meta->Next()) return;

  const auto index = GetIndex(meta);
  auto &array(arrays[index.first]);
  if (GRANARY_UNLIKELY(!array)) array = new internal::MetaDataArray;

  auto &metas(array->metas[index.second]);
  if (GRANARY_UNLIKELY(!metas)) metas = META_ARRAY_END;

  index_meta->SetNext(metas);  // Add it in.
  metas = meta;
}

ShadowIndex::~ShadowIndex(void) {
  for (auto array : arrays) {
    delete array;
  }
}

// Perform a lookup operation in the shadow code cache index. Lookup
// operations in the shadow index either return exact matches or rejections,
// but never adaptations.
IndexFindResponse ShadowIndex::Request(BlockMetaData *meta) {
  if (GRANARY_UNLIKELY(!meta)) {
    return {UnificationStatus::REJECT, meta};
  }

  auto index_meta = MetaDataCast<IndexMetaData *>(meta);
  if (index_meta->NextShadow()) {
    return {UnificationStatus::ACCEPT, meta};  // It must be in the index.
  }

  const auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  const auto index = GetShadowIndex(cache_meta);
  if (auto array = arrays[index.first]) {
    if (auto metas = array->metas[index.second]) {
      return MatchShadowMetaData(metas, cache_meta);
    }
  }
  return {UnificationStatus::REJECT, nullptr};  // Not in the index
}

// Insert a block into the code cache index.
void ShadowIndex::Insert(BlockMetaData *meta) {
  auto index_meta = MetaDataCast<IndexMetaData *>(meta);
  if (index_meta->NextShadow()) return;

  const auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  const auto index = GetShadowIndex(cache_meta);
  auto &array(arrays[index.first]);
  if (GRANARY_UNLIKELY(!array)) array = new internal::ShadowMetaDataArray;

  auto &metas(array->metas[index.second]);
  if (GRANARY_UNLIKELY(!metas)) metas = META_ARRAY_END;

  index_meta->SetNextShadow(metas);  // Add it in.
  metas = meta;
}

}  // namespace granary
