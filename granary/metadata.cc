/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"
#include "granary/base/lock.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/app.h"  // For `AppMetaData`.
#include "granary/breakpoint.h"
#include "granary/cache.h"  // For `CacheMetaData`.
#include "granary/index.h"  // For `IndexMetaData`.
#include "granary/metadata.h"

GRANARY_DEFINE_bool(debug_trace_meta, false,
    "Trace the meta-data that is committed to the code cache index. The "
    "default is `no`.\n"
    "\n"
    "The meta-data trace can be inspected from GDB by issuing the "
    "`print-meta-entry` command. For example, `print-meta-entry 0` will print "
    "the most recently indexed blocked meta-data.\n"
    "\n"
    "A printed meta-data entry attempts to dump the fields of the individual "
    "data structures embedded within the meta-data, as well as the translation "
    "group to which the block associated with the meta-data belongs. Each time "
    "a context switch into Granary leads to the translation of some code, the "
    "group number is incremented. The value is therefore a lower bound for the "
    "number of context switches in/out of Granary.\n"
    "\n"
    "Multiple blocks (and therefore block meta-datas) can belong to a single "
    "translation group. This is typical, as some tools (and even Granary "
    "itself) will request the more than one blocks be translated during a "
    "single request.");

namespace granary {
namespace {

enum {
  // Upper bound on the number of registerable meta-data instances.
  kMaxNumManagedMetaData = 32
};

// The next meta-data description ID that we can assign. Every meta-data
// description has a unique, global ID.
static int gNextDescriptionId = 0;

// Size and alignment of the overall meta-data structure managed by this
// manager.
static size_t gAlign = 0;
static size_t gSize = 0;

// Whether or not this meta-data has been finalized.
static bool gIsFinalized = false;

// Info on all registered meta-data within this manager. These are indexed
// by the `MetaDataDescription::id` field.
static MetaDataDescription *gDescriptions[kMaxNumManagedMetaData];

// Slab allocator for allocating meta-data objects.
static Container<internal::SlabAllocator> gAllocator;

// Finalizes the meta-data structures, which determines the runtime layout
// of the packed meta-data structure.
static void Finalize(void) {
  gIsFinalized = true;
  for (auto desc : gDescriptions) {
    if (!desc) break;
    gAlign = std::max(desc->align, gAlign);
    gSize += GRANARY_ALIGN_FACTOR(gSize, desc->align);
    desc->offset = gSize;
    gSize += desc->size;
  }
  gSize += GRANARY_ALIGN_FACTOR(gSize, gAlign);
}

// Initialize the allocator for meta-data managed by this manager.
static void InitAllocator(void) {
  auto offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList), gSize);
  auto remaining_size = internal::kNewAllocatorNumBytesPerSlab - offset;
  auto max_num_allocs = (remaining_size - gSize + 1) / gSize;
  auto max_offset = offset + max_num_allocs * gSize;
  GRANARY_ASSERT(internal::kNewAllocatorNumBytesPerSlab >= max_offset);
  gAllocator.Construct(offset, max_offset, gSize, gSize);
}

}  // namespace

// Initialize a new meta-data instance. This involves separately initializing
// the contained meta-data within this generic meta-data.
BlockMetaData::BlockMetaData(void) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto desc : gDescriptions) {
    if (!desc) break;
    GRANARY_ASSERT(std::numeric_limits<uintptr_t>::max() != desc->offset);
    desc->initialize(reinterpret_cast<void *>(this_ptr + desc->offset));
  }
}

// Initialize a new meta-data instance. This initializes the `AppMetaData`
// as well.
BlockMetaData::BlockMetaData(AppPC app_pc) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto desc : gDescriptions) {
    if (!desc) break;
    GRANARY_ASSERT(std::numeric_limits<uintptr_t>::max() != desc->offset);
    desc->initialize(reinterpret_cast<void *>(this_ptr + desc->offset));
  }
  MetaDataCast<AppMetaData *>(this)->start_pc = app_pc;
}

// Destroy a meta-data instance. This involves separately destroying the
// contained meta-data within this generic meta-data.
BlockMetaData::~BlockMetaData(void) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto desc : gDescriptions) {
    if (!desc) break;
    desc->destroy(reinterpret_cast<void *>(this_ptr + desc->offset));
  }
}

// Create a copy of some meta-data and return a new instance of the copied
// meta-data.
BlockMetaData *BlockMetaData::Copy(void) const {
  auto that = new BlockMetaData;
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto desc : gDescriptions) {
    if (!desc) break;
    const auto offset = desc->offset;
    desc->copy_initialize(reinterpret_cast<void *>(that_ptr + offset),
                          reinterpret_cast<const void *>(this_ptr + offset));
  }
  return that;
}

// Compare the indexable components of two generic meta-data instances for
// strict equality.
bool BlockMetaData::Equals(const BlockMetaData *that) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto desc : gDescriptions) {
    if (!desc) break;
    if (!desc->compare_equals) continue;  // Not indexable.

    const auto offset = desc->offset;
    auto this_meta = reinterpret_cast<const void *>(this_ptr + offset);
    auto that_meta = reinterpret_cast<const void *>(that_ptr + offset);
    if (!desc->compare_equals(this_meta, that_meta)) return false;
  }
  return true;
}

// Check to see if this meta-data can unify with some other generic meta-data.
UnificationStatus BlockMetaData::CanUnifyWith(
    const BlockMetaData *that) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  auto can_unify = kUnificationStatusAccept;
  for (auto desc : gDescriptions) {
    if (!desc) break;
    if (!desc->can_unify) continue;  // Not unifiable.

    const auto offset = desc->offset;
    auto this_meta = reinterpret_cast<const void *>(this_ptr + offset);
    auto that_meta = reinterpret_cast<const void *>(that_ptr + offset);
    auto local_can_unify = desc->can_unify(this_meta, that_meta);
    can_unify = GRANARY_MAX(can_unify, local_can_unify);
  }
  return can_unify;
}

// Combine this meta-data with some other meta-data.
void BlockMetaData::JoinWith(const BlockMetaData *that) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto desc : gDescriptions) {
    if (!desc) break;
    const auto offset = desc->offset;
    auto this_meta = reinterpret_cast<void *>(this_ptr + offset);
    auto that_meta = reinterpret_cast<const void *>(that_ptr + offset);
    desc->join(this_meta, that_meta);
  }
}

// Dynamically free meta-data.
void *BlockMetaData::operator new(size_t) {
  if (GRANARY_UNLIKELY(!gIsFinalized)) {
    Finalize();
    InitAllocator();
  }
  auto meta_mem = gAllocator->Allocate();
  memset(meta_mem, 0, gSize);
  return meta_mem;
}

// Dynamically free meta-data.
void BlockMetaData::operator delete(void *address) {
  GRANARY_ASSERT(gIsFinalized);
  gAllocator->Free(address);
}

#ifndef GRANARY_RECURSIVE

extern "C" {
enum {
  GRANARY_META_LOG_LENGTH = 4096
};

// The recorded entries in the trace. This is a global variable so that GDB
// can see it.
const BlockMetaData *granary_meta_log[GRANARY_META_LOG_LENGTH];

// The index into Granary's trace log. Also a global variable so that GDB can
// easily see it.
alignas(arch::CACHE_LINE_SIZE_BYTES) unsigned granary_meta_log_index = 0;

}  // extern C

// Initialize the meta-data trace.
void InitMetaDataTracer(void) {
  memset(granary_meta_log, 0, sizeof granary_meta_log);
}

// Adds this meta-data to a trace log of recently translated meta-data blocks.
// This is useful for GDB-based debugging, because it lets us see the most
// recently translated blocks (in terms of their meta-data).
void TraceMetaData(const BlockMetaData *meta) {
  if (GRANARY_UNLIKELY(FLAG_debug_trace_meta)) {
    auto i = __sync_fetch_and_add(&granary_meta_log_index, 1);
    granary_meta_log[i % GRANARY_META_LOG_LENGTH] = meta;
  }
}

#else
void InitMetaDataTracer(void) {}
void TraceMetaData(uint64_t, const BlockMetaData *) {}
#endif  // GRANARY_RECURSIVE

// Initialize the global meta-data manager.
void InitMetaData(void) {
  gNextDescriptionId = 0;
  gAlign = 0;
  gSize = 0;
  gIsFinalized = false;
  AddMetaData<AppMetaData>();
  AddMetaData<CacheMetaData>();
  AddMetaData<IndexMetaData>();
  InitMetaDataTracer();
}

// Destroy the global meta-data manager.
void ExitMetaData(void) {
  for (auto &desc : gDescriptions) {
    if (desc) {
      desc->id = -1;
      desc->offset = std::numeric_limits<uintptr_t>::max();
      desc = nullptr;
    }
  }
  if (gIsFinalized) {
    gAllocator.Destroy();
  }
}

// Register some meta-data with the meta-data manager.
void AddMetaData(MetaDataDescription *desc) {
  GRANARY_ASSERT(!gIsFinalized);
  GRANARY_ASSERT(std::numeric_limits<uintptr_t>::max() == desc->offset);
  if (-1 == desc->id) {
    desc->id = gNextDescriptionId++;
    GRANARY_ASSERT(kMaxNumManagedMetaData > desc->id);
  }
  gDescriptions[desc->id] = desc;
}

}  // namespace granary
