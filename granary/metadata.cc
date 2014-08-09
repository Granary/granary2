/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"
#include "granary/metadata.h"

GRANARY_DEFINE_bool(debug_trace_metadata, false,
    "Trace the meta-data that is committed to the code cache index. The default "
    "is `no`.");

namespace granary {
namespace {

// The next meta-data description ID that we can assign. Every meta-data
// description has a unique, global ID.
static std::atomic<int> next_description_id(ATOMIC_VAR_INIT(0));

}  // namespace

// Cast some generic meta-data into some specific meta-data.
void *BlockMetaData::Cast(MetaDataDescription *desc) {
  GRANARY_ASSERT(-1 != desc->id);
  GRANARY_ASSERT(nullptr != manager->descriptions[desc->id]);
  auto meta_ptr = reinterpret_cast<uintptr_t>(this);
  return reinterpret_cast<void *>(meta_ptr + manager->offsets[desc->id]);
}

// Initialize a new meta-data instance. This involves separately initializing
// the contained meta-data within this generic meta-data.
BlockMetaData::BlockMetaData(MetaDataManager *manager_)
    : manager(manager_) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto desc : manager->descriptions) {
    if (desc) {
      auto offset = manager->offsets[desc->id];
      desc->initialize(reinterpret_cast<void *>(this_ptr + offset));
    }
  }
}

// Destroy a meta-data instance. This involves separately destroying the
// contained meta-data within this generic meta-data.
BlockMetaData::~BlockMetaData(void) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto desc : manager->descriptions) {
    if (desc) {
      auto offset = manager->offsets[desc->id];
      desc->destroy(reinterpret_cast<void *>(this_ptr + offset));
    }
  }
}

// Create a copy of some meta-data and return a new instance of the copied
// meta-data.
BlockMetaData *BlockMetaData::Copy(void) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(manager->Allocate());
  for (auto desc : manager->descriptions) {
    if (desc) {
      auto offset = manager->offsets[desc->id];
      desc->copy_initialize(reinterpret_cast<void *>(that_ptr + offset),
                            reinterpret_cast<const void *>(this_ptr + offset));
    }
  }

  return reinterpret_cast<BlockMetaData *>(that_ptr);
}

// Compare the serializable components of two generic meta-data instances for
// strict equality.
bool BlockMetaData::Equals(const BlockMetaData *that) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto desc : manager->descriptions) {
    if (desc && desc->compare_equals) {  // Indexable.
      auto offset = manager->offsets[desc->id];
      auto this_meta = reinterpret_cast<const void *>(this_ptr + offset);
      auto that_meta = reinterpret_cast<const void *>(that_ptr + offset);
      if (!desc->compare_equals(this_meta, that_meta)) {
        return false;
      }
    }
  }
  return true;
}

// Check to see if this meta-data can unify with some other generic meta-data.
UnificationStatus BlockMetaData::CanUnifyWith(
    const BlockMetaData *that) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  auto can_unify = UnificationStatus::ACCEPT;
  for (auto desc : manager->descriptions) {
    if (desc && desc->can_unify) {  // Unifiable.
      auto offset = manager->offsets[desc->id];
      auto this_meta = reinterpret_cast<const void *>(this_ptr + offset);
      auto that_meta = reinterpret_cast<const void *>(that_ptr + offset);
      auto local_can_unify = desc->can_unify(this_meta, that_meta);
      can_unify = GRANARY_MAX(can_unify, local_can_unify);
    }
  }
  return can_unify;
}

// Combine this meta-data with some other meta-data.
void BlockMetaData::JoinWith(const BlockMetaData *that) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto desc : manager->descriptions) {
    if (desc) {
      auto offset = manager->offsets[desc->id];
      auto this_meta = reinterpret_cast<void *>(this_ptr + offset);
      auto that_meta = reinterpret_cast<const void *>(that_ptr + offset);
      desc->join(this_meta, that_meta);
    }
  }
}

// Dynamically free meta-data.
void BlockMetaData::operator delete(void *address) {
  auto self = reinterpret_cast<BlockMetaData *>(address);
  self->manager->Free(self);
}

// Initialize an empty metadata manager.
MetaDataManager::MetaDataManager(void)
    : size(sizeof(BlockMetaData)),
      is_finalized(false),
      allocator() {
  memset(&(descriptions[0]), 0, sizeof(descriptions));
  memset(&(offsets[0]), 0, sizeof(offsets));
}

MetaDataManager::~MetaDataManager(void) {
  allocator->Destroy();
  allocator.Destroy();
}

// Register some meta-data with the meta-data manager. This is a no-op if the
// meta-data has already been registered.
void MetaDataManager::Register(MetaDataDescription *desc) {
  if (GRANARY_UNLIKELY(!is_finalized)) {
    if (-1 == desc->id) {
      desc->id = next_description_id.fetch_add(1);
      GRANARY_ASSERT(MAX_NUM_MANAGED_METADATAS > desc->id);
    }
    descriptions[desc->id] = desc;
  }
}

// Allocate some meta-data. If the manager hasn't been finalized then this
// returns `nullptr`.
BlockMetaData *MetaDataManager::Allocate(void) {
  if (GRANARY_UNLIKELY(!is_finalized)) {
    Finalize();
    InitAllocator();
  }
  auto meta_mem = allocator->Allocate();
  memset(meta_mem, 0, size);
  auto meta = new (meta_mem) BlockMetaData(this);
  VALGRIND_MALLOCLIKE_BLOCK(meta, size, 0, 0);
  return meta;
}

// Free some meta-data. This is a no-op if the manager hasn't been finalized.
void MetaDataManager::Free(BlockMetaData *meta) {
  GRANARY_ASSERT(is_finalized);
  GRANARY_ASSERT(this == meta->manager);
  allocator->Free(meta);
  VALGRIND_FREELIKE_BLOCK(meta, size);
}


// Finalizes the meta-data structures, which determines the runtime layout
// of the packed meta-data structure.
void MetaDataManager::Finalize(void) {
  is_finalized = true;
  for (auto desc : descriptions) {
    if (desc) {
      size += GRANARY_ALIGN_FACTOR(size, desc->align);
      offsets[desc->id] = size;
      size += desc->size;
    }
  }
  size += GRANARY_ALIGN_FACTOR(size, alignof(BlockMetaData));
}

// Initialize the allocator for meta-data managed by this manager.
void MetaDataManager::InitAllocator(void) {
  auto offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList), size);
  auto remaining_size = internal::SLAB_ALLOCATOR_SLAB_SIZE_BYTES - offset;
  auto max_num_allocs = (remaining_size - size + 1) / size;
  allocator.Construct(max_num_allocs, offset, size, size);
}

extern "C" {

// Represents a trace entry containing some meta-data.
struct TracedMetaData {
  uint64_t group;
  const BlockMetaData *meta;
};

enum {
  GRANARY_META_LOG_LENGTH = 1024
};

// The recorded entries in the trace. This is a global variable so that GDB
// can see it.
TracedMetaData granary_meta_log[GRANARY_META_LOG_LENGTH];

// The index into Granary's trace log. Also a global variable so that GDB can
// easily see it.
unsigned granary_meta_log_index = 0;

}  // extern C

// Adds this meta-data to a trace log of recently translated meta-data blocks.
// This is useful for GDB-based debugging, because it lets us see the most
// recently translated blocks (in terms of their meta-data).
void TraceMetaData(uint64_t group, const BlockMetaData *meta) {
  if (GRANARY_LIKELY(!FLAG_debug_trace_metadata)) return;
  auto i = __sync_fetch_and_add(&granary_meta_log_index, 1);
  auto &entry(granary_meta_log[i % GRANARY_META_LOG_LENGTH]);
  entry.group = group;
  entry.meta = meta;
}

}  // namespace granary
