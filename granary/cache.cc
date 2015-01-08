/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/container.h"
#include "granary/base/option.h"

#include "granary/code/edge.h"

#include "granary/cache.h"

#include "os/lock.h"
#include "os/memory.h"

GRANARY_DEFINE_positive_uint(code_cache_slab_size, 8,
    "The number of pages allocated at once to store code. The default value is "
    "`8` pages per slab.");

extern "C" {
extern const granary::CachePC granary_code_cache_begin;
extern const granary::CachePC granary_code_cache_end;
}  // extern C
namespace granary {

GRANARY_IMPLEMENT_NEW_ALLOCATOR(NativeAddress)

namespace arch {

// Generates the direct edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
//
// This code takes a pointer to the context so that the code generated will
// be able to pass the context pointer directly to `granary::EnterGranary`.
// This allows us to avoid saving the context pointer in the `DirectEdge`.
//
// Note: This has an architecture-specific implementation.
extern void GenerateDirectEdgeEntryCode(CachePC edge);

// Generates the direct edge code for a given `DirectEdge` structure.
//
// Note: This has an architecture-specific implementation.
extern void GenerateDirectEdgeCode(DirectEdge *edge);

// Generates the indirect edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
//
// This code takes a pointer to the context so that the code generated will
// be able to pass the context pointer directly to `granary::EnterGranary`.
// This allows us to avoid saving the context pointer in the `IndirectEdge`.
//
// Note: This has an architecture-specific implementation.
extern void GenerateIndirectEdgeEntryCode(CachePC edge);

// Generates code that disables interrupts.
//
// Note: This has an architecture-specific implementation.
extern void GenerateInterruptDisableCode(CachePC pc);

// Generates code that re-enables interrupts (if they were disabled by the
// interrupt disabling routine).
//
// Note: This has an architecture-specific implementation.
extern void GenerateInterruptEnableCode(CachePC pc);

}  // namespace arch
namespace {

class CodeSlab {
 public:
  CodeSlab(CachePC begin_, const CodeSlab *next_)
      : begin(begin_),
        next(next_) {}

  const CachePC begin;
  const CodeSlab *next;

  GRANARY_DEFINE_NEW_ALLOCATOR(CodeSlab, {
    kAlignment = 1
  })

 private:
  CodeSlab(void) = delete;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeSlab);
};

static const CodeSlab *AllocateSlab(size_t num_pages, const CodeSlab *next) {
  return new CodeSlab(os::AllocateCodePages(num_pages), next);
}

// Implementation of Granary's code caches.
class CodeCache {
 public:
  explicit CodeCache(size_t slab_size_);
  ~CodeCache(void);

  // Allocate a block of code from this code cache.
  CachePC AllocateCode(size_t size);

 private:
  // The size of a slab.
  const size_t slab_num_pages;
  const size_t slab_num_bytes;

  // The offset into the current slab that's serving allocations.
  size_t slab_byte_offset;

  // Lock around the whole code cache, which prevents multiple people from
  // reading/writing to the cache at once.
  SpinLock slab_list_lock;

  // Allocator used to allocate blocks from this code cache.
  const CodeSlab *slab_list;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeCache);
};

CodeCache::CodeCache(size_t slab_size_)
    : slab_num_pages(slab_size_),
      slab_num_bytes(slab_size_ * arch::PAGE_SIZE_BYTES),
      slab_byte_offset(0),
      slab_list_lock(),
      slab_list(AllocateSlab(slab_num_pages, nullptr)) {}

CodeCache::~CodeCache(void) {
  auto slab = slab_list;
  for (const CodeSlab *next_slab(nullptr); slab; slab = next_slab) {
    next_slab = slab->next;
    delete slab;
  }
  slab_byte_offset = 0;
}

// Allocate a block of code from this code cache.
CachePC CodeCache::AllocateCode(size_t size) {
  SpinLockedRegion locker(&slab_list_lock);
  auto old_offset = slab_byte_offset;
  auto aligned_offset = GRANARY_ALIGN_TO(old_offset, arch::CODE_ALIGN_BYTES);
  auto new_offset = aligned_offset + size;
  if (GRANARY_UNLIKELY(new_offset >= slab_num_bytes)) {
    slab_list = AllocateSlab(slab_num_pages, slab_list);
    slab_byte_offset = 0;
    aligned_offset = 0;
    new_offset = size;
    GRANARY_ASSERT(new_offset < slab_num_bytes);
  }
  auto addr = &(slab_list->begin[aligned_offset]);
  slab_byte_offset = new_offset;
  GRANARY_ASSERT(nullptr != addr);
  return addr;
}

// Lock around all code cache transactions.
static os::Lock gCodeCacheLock;

// Code caches.
static Container<CodeCache> gCodeCaches[kNumCodeCacheKinds];

}  // namespace

// Used to allocate code from a code cache.
CachePC AllocateCode(CodeCacheKind kind, size_t num_bytes) {
  if (!num_bytes) return nullptr;
  return gCodeCaches[kind]->AllocateCode(num_bytes);
}

// Begin a transaction that will read or write to the code cache.
//
// Note: Transactions are distinct from allocations. Therefore, many threads/
//       cores can simultaneously allocate from a code cache, but only one
//       should be able to read/write data to the cache at a given time.
CodeCacheTransaction::CodeCacheTransaction(void) {
  gCodeCacheLock.Acquire();
}

// End a transaction that will read or write to the code cache.
CodeCacheTransaction::~CodeCacheTransaction(void) {
  gCodeCacheLock.Release();
}

namespace {

template <typename T>
static CachePC GenerateCode(T generator, size_t size) {
  auto code = AllocateCode(kCodeCacheKindEdge, size);
  CodeCacheTransaction transaction;
  generator(code);
  return code;
}

// Generated functions.
static CachePC gDirectExitFunction = nullptr;
static CachePC gIndirectExitFunction = nullptr;
static CachePC gDisableInterruptsFunction = nullptr;
static CachePC gEnableInterruptsFunction = nullptr;

}  // namespace

// Returns the address of the code that exits the code cache via a direct edge.
CachePC DirectExitFunction(void) {
  return gDirectExitFunction;
}

// Returns the address of the code that exits the code cache via an indirect
// edge.
CachePC IndirectExitFunction(void) {
  return gIndirectExitFunction;
}

// Returns the address of the code that disables the interrupts.
CachePC DisableInterruptsFunction(void) {
  return gDisableInterruptsFunction;
}

// Returns the address of the code that enables the interrupts.
CachePC EnableInterruptsFunction(void) {
  return gEnableInterruptsFunction;
}

// Initialize the code caches.
void InitCodeCache(void) {
  for (auto &cache : gCodeCaches) {
    cache.Construct(FLAG_code_cache_slab_size);
  }
  gDirectExitFunction = GenerateCode(
      arch::GenerateDirectEdgeEntryCode,
      arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES);
  gIndirectExitFunction = GenerateCode(
      arch::GenerateIndirectEdgeEntryCode,
      arch::INDIRECT_EDGE_ENTRY_CODE_SIZE_BYTES);
  gDisableInterruptsFunction = GenerateCode(
      arch::GenerateInterruptDisableCode,
      arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES);
  gEnableInterruptsFunction = GenerateCode(
      arch::GenerateInterruptEnableCode,
      arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES);
}

// Exit the code caches.
void ExitCodeCache(void) {
  for (auto &cache : gCodeCaches) {
    cache.Destroy();
  }
}

// Provides a good estimation of the location of the code cache. This is used
// by all code that computes whether or not an address is too far away from the
// code cache.
CachePC EstimatedCachePC(void) {
  auto diff = (granary_code_cache_end - granary_code_cache_begin) / 2;
  return granary_code_cache_begin + diff;
}

// Initialize Granary's internal translation cache meta-data.
CacheMetaData::CacheMetaData(void)
    : start_pc(nullptr),
      native_addresses(nullptr) {}

// Clean up the cache meta-data, and any data structures tied in to the cached
// code.
CacheMetaData::~CacheMetaData(void) {
  NativeAddress *next_addr(nullptr);
  for (auto addr = native_addresses; addr; addr = next_addr) {
    next_addr = addr->next;
    delete addr;
  }
}

}  // namespace granary
