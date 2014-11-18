/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"
#include "granary/base/lock.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/code/metadata.h"  // For `StackMetaData`.

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

// The next meta-data description ID that we can assign. Every meta-data
// description has a unique, global ID.
static int gNextDescriptionId = 0;

}  // namespace

// Manages all block meta-data for the lifetime of an instrumentation session.
class MetaDataManager {
 public:
  // Initialize an empty meta-data manager.
  MetaDataManager(void);

  ~MetaDataManager(void);

  // Register some meta-data with Granary. This is a convenience method around
  // the `Register` method that operates directly on a meta-data description.
  template <typename T>
  inline void Register(void) {
    Add(const_cast<MetaDataDescription *>(
        GetMetaDataDescription<T>::Get()));
  }

  // Register some meta-data with Granary.
  void Add(MetaDataDescription *desc);

  // Allocate some meta-data. This lazily finalizes the meta-data allocator.
  void *Allocate(void);

  // Free some meta-data.
  void Free(BlockMetaData *meta);

  inline size_t Size(void) const {
    return size;
  }

  enum {
    // Upper bound on the number of registerable meta-data instances.
    MAX_NUM_MANAGED_METADATAS = 32
  };

  // Finalizes the meta-data structures, which determines the runtime layout
  // of the packed meta-data structure. Once
  void Finalize(void);

  // Initialize the allocator for meta-data managed by this manager.
  void InitAllocator(void);

  // Size and alignment of the overall meta-data structure managed by this
  // manager.
  size_t align;
  size_t size;

  // Whether or not this meta-data has been finalized.
  bool is_finalized;

  // Info on all registered meta-data within this manager. These are indexed
  // by the `MetaDataDescription::id` field.
  MetaDataDescription *descriptions[MAX_NUM_MANAGED_METADATAS];

  // Slab allocator for allocating meta-data objects.
  Container<internal::SlabAllocator> allocator;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(MetaDataManager);
};

// Initialize an empty meta-data manager.
MetaDataManager::MetaDataManager(void)
    : align(0),
      size(0),
      is_finalized(false),
      allocator() {
  gNextDescriptionId = 0;
  for (auto &desc : descriptions) {
    desc = nullptr;
  }
}

MetaDataManager::~MetaDataManager(void) {
  for (auto desc : descriptions) {
    if (desc) {
      desc->id = -1;
      desc->offset = std::numeric_limits<uintptr_t>::max();
    }
  }
  allocator->Destroy();
  allocator.Destroy();
}

// Register some meta-data with the meta-data manager.
void MetaDataManager::Add(MetaDataDescription *desc) {
  GRANARY_ASSERT(!is_finalized);
  GRANARY_ASSERT(std::numeric_limits<uintptr_t>::max() == desc->offset);
  if (-1 == desc->id) {
    desc->id = gNextDescriptionId++;
    GRANARY_ASSERT(MAX_NUM_MANAGED_METADATAS > desc->id);
  }
  descriptions[desc->id] = desc;
}

// Allocate some meta-data. If the manager hasn't been finalized then this
// returns `nullptr`.
void *MetaDataManager::Allocate(void) {
  if (GRANARY_UNLIKELY(!is_finalized)) {
    Finalize();
    InitAllocator();
  }
  auto meta_mem = allocator->Allocate();
  memset(meta_mem, 0, size);
  return meta_mem;
}

// Free some meta-data. This is a no-op if the manager hasn't been finalized.
void MetaDataManager::Free(BlockMetaData *meta) {
  GRANARY_ASSERT(is_finalized);
  allocator->Free(meta);
}

// Finalizes the meta-data structures, which determines the runtime layout
// of the packed meta-data structure.
void MetaDataManager::Finalize(void) {
  is_finalized = true;
  for (auto desc : descriptions) {
    if (desc) {
      align = std::max(desc->align, align);
      size += GRANARY_ALIGN_FACTOR(size, desc->align);
      desc->offset = size;
      size += desc->size;
    }
  }
  size += GRANARY_ALIGN_FACTOR(size, align);
}

// Initialize the allocator for meta-data managed by this manager.
void MetaDataManager::InitAllocator(void) {
  auto offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList), size);
  auto remaining_size = internal::SLAB_ALLOCATOR_SLAB_SIZE_BYTES - offset;
  auto max_num_allocs = (remaining_size - size + 1) / size;
  allocator.Construct(max_num_allocs, offset, align, size, size);
}

namespace {

// The global meta-data manager instance.
GRANARY_EARLY_GLOBAL static Container<MetaDataManager> gMetaManager;

}  // namespace

// Initialize a new meta-data instance. This involves separately initializing
// the contained meta-data within this generic meta-data.
BlockMetaData::BlockMetaData(void) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto desc : gMetaManager->descriptions) {
    if (desc) {
      GRANARY_ASSERT(std::numeric_limits<uintptr_t>::max() != desc->offset);
      desc->initialize(reinterpret_cast<void *>(this_ptr + desc->offset));
    }
  }
}

// Destroy a meta-data instance. This involves separately destroying the
// contained meta-data within this generic meta-data.
BlockMetaData::~BlockMetaData(void) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto desc : gMetaManager->descriptions) {
    if (desc) {
      desc->destroy(reinterpret_cast<void *>(this_ptr + desc->offset));
    }
  }
}

// Create a copy of some meta-data and return a new instance of the copied
// meta-data.
BlockMetaData *BlockMetaData::Copy(void) const {
  auto that = new BlockMetaData;
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto desc : gMetaManager->descriptions) {
    if (desc) {
      const auto offset = desc->offset;
      desc->copy_initialize(reinterpret_cast<void *>(that_ptr + offset),
                            reinterpret_cast<const void *>(this_ptr + offset));
    }
  }
  return that;
}

// Compare the serializable components of two generic meta-data instances for
// strict equality.
bool BlockMetaData::Equals(const BlockMetaData *that) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto desc : gMetaManager->descriptions) {
    if (desc && desc->compare_equals) {  // Indexable.
      const auto offset = desc->offset;
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
  for (auto desc : gMetaManager->descriptions) {
    if (desc && desc->can_unify) {  // Unifiable.
      const auto offset = desc->offset;
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
  for (auto desc : gMetaManager->descriptions) {
    if (desc) {
      const auto offset = desc->offset;
      auto this_meta = reinterpret_cast<void *>(this_ptr + offset);
      auto that_meta = reinterpret_cast<const void *>(that_ptr + offset);
      desc->join(this_meta, that_meta);
    }
  }
}

// Dynamically free meta-data.
void *BlockMetaData::operator new(size_t) {
  return gMetaManager->Allocate();
}

// Dynamically free meta-data.
void BlockMetaData::operator delete(void *address) {
  gMetaManager->Free(reinterpret_cast<BlockMetaData *>(address));
}

#ifndef GRANARY_RECURSIVE

extern "C" {

// Represents a trace entry containing some meta-data.
struct TracedMetaData {
  uint64_t group;
  const BlockMetaData *meta;
};

enum {
  GRANARY_META_LOG_LENGTH = 4096
};

// The recorded entries in the trace. This is a global variable so that GDB
// can see it.
TracedMetaData granary_meta_log[GRANARY_META_LOG_LENGTH];

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
void TraceMetaData(uint64_t group, const BlockMetaData *meta) {
  if (GRANARY_UNLIKELY(FLAG_debug_trace_meta)) {
    auto i = __sync_fetch_and_add(&granary_meta_log_index, 1);
    auto &entry(granary_meta_log[i % GRANARY_META_LOG_LENGTH]);
    entry.group = group;
    entry.meta = meta;
  }
}

#else
void InitMetaDataTracer(void) {}
void TraceMetaData(uint64_t, const BlockMetaData *) {}
#endif  // GRANARY_RECURSIVE

// Initialize the global meta-data manager.
void InitMetaData(void) {
  gNextDescriptionId = 0;
  gMetaManager.Construct();
  gMetaManager->Register<AppMetaData>();
  gMetaManager->Register<CacheMetaData>();
  gMetaManager->Register<IndexMetaData>();
  gMetaManager->Register<StackMetaData>();
  InitMetaDataTracer();
}

// Destroy the global meta-data manager.
void ExitMetaData(void) {
  gMetaManager.Destroy();
}

// Register some meta-data with the meta-data manager.
void AddMetaData(MetaDataDescription *desc) {
  gMetaManager->Add(desc);
}

}  // namespace granary
