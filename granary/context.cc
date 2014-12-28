/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"
#include "arch/context.h"

#include "granary/base/container.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/cfg/block.h"

#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/inline_assembly.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/context.h"
#include "granary/index.h"
#include "granary/metadata.h"

#include "os/module.h"

GRANARY_DEFINE_positive_int(block_cache_slab_size, 512,
    "The number of pages allocated at once to store basic block code. Each "
    "context maintains its own block code allocator. The default value is "
    "`512` pages per slab (2MiB).");

GRANARY_DEFINE_positive_int(edge_cache_slab_size, 256,
    "The number of pages allocated at once to store edge code. Each "
    "context maintains its own edge code allocator. The default value is "
    "`256` pages per slab (1MiB).");

namespace granary {
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
extern void GenerateDirectEdgeCode(DirectEdge *edge, CachePC edge_entry_code);

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

// Generates the wrapper code for a context callback.
//
// Note: This has an architecture-specific implementation.
extern Callback *GenerateContextCallback(CodeCache *cache, AppPC func_pc);

// Generates the wrapper code for an outline callback.
//
// Note: This has an architecture-specific implementation.
extern Callback *GenerateInlineCallback(CodeCache *cache,
                                        InlineFunctionCall *call);

}  // namespace arch
namespace {

template <typename T>
static CachePC GenerateCode(CodeCache *cache, T generator, size_t size) {
  auto code = cache->AllocateBlock(size);
  CodeCacheTransaction transaction(code, code + size);
  generator(code);
  return code;
}

// Free a linked list of edges.
template <typename EdgeT>
static void FreeEdgeList(EdgeT *edge) {
  EdgeT *next_edge = nullptr;
  for (; edge; edge = next_edge) {
    next_edge = edge->next;
    delete edge;
  }
}

// Unlink the block meta-data pointers in an a un/patched edge list.
template <typename EdgeT>
static void UnlinkEdgeList(EdgeT *edge) {
  EdgeT *next_edge = nullptr;
  for (; edge; edge = next_edge) {
    next_edge = edge->next_patchable;
    edge->dest_meta = nullptr;
  }
}

template <typename T>
static void FreeCallbacks(const T &callback_map) {
  for (auto cb : callback_map.Values()) {
    delete cb;
  }
}

}  // namespace

Context::Context(void)
    : block_code_cache(static_cast<size_t>(FLAG_block_cache_slab_size)),
      edge_code_cache(static_cast<size_t>(FLAG_edge_cache_slab_size)),
      direct_edge_entry_code(
          GenerateCode(&edge_code_cache, arch::GenerateDirectEdgeEntryCode,
                       arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES)),
      indirect_edge_entry_code(
          GenerateCode(&edge_code_cache, arch::GenerateIndirectEdgeEntryCode,
                       arch::INDIRECT_EDGE_ENTRY_CODE_SIZE_BYTES)),
      disable_interrupts_code(
          GenerateCode(&edge_code_cache, arch::GenerateInterruptDisableCode,
                       arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES)),
      enable_interrupts_code(
          GenerateCode(&edge_code_cache, arch::GenerateInterruptEnableCode,
                       arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES)),
      edge_list_lock(),
      edge_list(nullptr),
      unpatched_edge_list(nullptr),
      patched_edge_list(nullptr),
      indirect_edge_list_lock(),
      indirect_edge_list(nullptr),
      code_cache_index(new Index),
      context_callbacks_lock(),
      context_callbacks(),
      inline_callbacks_lock(),
      inline_callbacks() {}

Context::~Context(void) {
  UnlinkEdgeList(unpatched_edge_list);
  UnlinkEdgeList(patched_edge_list);
  FreeEdgeList(edge_list);
  FreeCallbacks(context_callbacks);
  FreeCallbacks(inline_callbacks);
}

// Allocate and initialize some `BlockMetaData`. This will also set-up the
// `AppMetaData` within the `BlockMetaData`.
BlockMetaData *Context::AllocateBlockMetaData(AppPC start_pc) {
  auto meta = new BlockMetaData;
  MetaDataCast<AppMetaData *>(meta)->start_pc = start_pc;
  return meta;
}

// Allocate and initialize some `BlockMetaData`, based on some existing
// meta-data `meta`.
BlockMetaData *Context::InstantiateBlockMetaData(
    const BlockMetaData *meta_template, AppPC start_pc) {
  auto meta = meta_template->Copy();
  MetaDataCast<AppMetaData *>(meta)->start_pc = start_pc;
  return meta;
}

// Allocates a direct edge data structure, as well as the code needed to
// back the direct edge.
DirectEdge *Context::AllocateDirectEdge(BlockMetaData *dest_block_meta) {
  DirectEdge *edge(nullptr);

  // Try to re-use an existing `DirectEdge` data structure, along with its
  // associated edge code.
  if (patched_edge_list) {  // Racy read.
    SpinLockedRegion locker(&edge_list_lock);
    if ((edge = patched_edge_list)) {
      patched_edge_list = edge->next_patched;
      new (edge) DirectEdge(dest_block_meta, edge->edge_code_pc, edge->next);
    }
  }

  // Allocate a new edge and chain it into the global list of edges.
  if (!edge) {
    auto edge_code = edge_code_cache.AllocateBlock(
        arch::DIRECT_EDGE_CODE_SIZE_BYTES);
    edge = new DirectEdge(dest_block_meta, edge_code);
    SpinLockedRegion locker(&edge_list_lock);
    edge->next = edge_list;
    edge_list = edge;
  }

  // Generate a small stub of code specific to this `DirectEdge`.
  CodeCacheTransaction transaction(
      edge->edge_code_pc,
      edge->edge_code_pc + arch::DIRECT_EDGE_CODE_SIZE_BYTES);
  arch::GenerateDirectEdgeCode(edge, direct_edge_entry_code);

  return edge;
}

// Prepare a direct edge for patching.
void Context::PreparePatchDirectEdge(DirectEdge *edge) {
  SpinLockedRegion locker(&edge_list_lock);
  edge->next_patchable = unpatched_edge_list;
  unpatched_edge_list = edge;
}

// Allocates an indirect edge data structure.
IndirectEdge *Context::AllocateIndirectEdge(const BlockMetaData *meta) {
  auto edge = new IndirectEdge(meta, indirect_edge_entry_code);
  SpinLockedRegion locker(&indirect_edge_list_lock);
  edge->next = indirect_edge_list;
  indirect_edge_list = edge;
  return edge;
}

// Returns a pointer to the code cache that is used for allocating code for
// basic blocks.
CodeCache *Context::BlockCodeCache(void) {
  return &block_code_cache;
}

// Get a pointer to this context's code cache index.
Index *Context::CodeCacheIndex(void) {
  return code_cache_index;
}

// Invalidate blocks that have been committed to the code cache index. This
// invalidates all blocks in the range `[begin_addr, end_addr)`.
void Context::InvalidateIndexedBlocks(AppPC begin_addr, AppPC end_addr) {
  BlockMetaData *meta(nullptr);
  meta = code_cache_index->RemoveRange(begin_addr, end_addr);

  // TODO(pag): Do something with `meta`!! This is a major memory leak at the
  //            moment.
  //
  // TODO(pag): Need to remove the block meta-data from IndirectEdge targets.
  //
  // TODO(pag): Should we intersect block that have code that logically
  //            intersects code in the range `[begin_addr, end_addr)`?
}

// Returns a pointer to the `CachePC` associated with the context-callable
// function at `func_addr`.
const arch::Callback *Context::ContextCallback(AppPC func_pc) {
  os::LockedRegion locker(&context_callbacks_lock);
  auto *&cb(context_callbacks[func_pc]);
  if (!cb) cb = arch::GenerateContextCallback(&edge_code_cache, func_pc);
  return cb;
}

// Returns a pointer to the code cache code associated with some outline-
// callable function at `func_addr`.
const arch::Callback *Context::InlineCallback(InlineFunctionCall *call) {
  os::LockedRegion locker(&inline_callbacks_lock);
  auto &cb(inline_callbacks[call->target_app_pc]);
  if (!cb) cb = arch::GenerateInlineCallback(&edge_code_cache, call);
  return cb;
}

#ifdef GRANARY_WHERE_kernel
// Returns a pointer to the code that can disable interrupts.
CachePC Context::DisableInterruptCode(void) const {
  return disable_interrupts_code;
}

// Returns a pointer to the code that can enable interrupts.
CachePC Context::EnableInterruptCode(void) const {
  return enable_interrupts_code;
}
#endif  // GRANARY_WHERE_kernel

namespace {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
GRANARY_EARLY_GLOBAL static Container<Context> gContext;
#pragma clang diagnostic pop
}  // namespace

// Initializes a new active context.
void InitContext(void) {
  gContext.Construct();
}

// Destroys the active context.
void ExitContext(void) {
  gContext.Destroy();
}

// Loads the active context.
Context *GlobalContext(void) {
  return gContext.AddressOf();
}

}  // namespace granary
