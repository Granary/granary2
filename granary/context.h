/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CONTEXT_H_
#define GRANARY_CONTEXT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/pc.h"

#include "granary/base/tiny_map.h"

#include "granary/cache.h"
#include "granary/exit.h"
#include "granary/index.h"
#include "granary/tool.h"

#include "os/lock.h"

namespace granary {

// Forward declarations.
class BlockMetaData;
class CodeCache;
class CompensationBlock;
class DecodedBlock;
class DirectEdge;
class IndirectEdge;
class Instruction;
class MetaDataDescription;
class InlineFunctionCall;

namespace arch {
class Callback;
}  // namespace arch

// Groups together all of the major data structures related to an
// instrumentation "session". All non-trivial state is packaged within the
// context
class Context {
 public:
  Context(void);
  ~Context(void);

  // Allocate and initialize some `BlockMetaData`.
  BlockMetaData *AllocateBlockMetaData(AppPC start_pc);

  // Allocate and initialize some `BlockMetaData`, based on some existing
  // meta-data template `meta_template`.
  BlockMetaData *InstantiateBlockMetaData(const BlockMetaData *meta_template,
                                          AppPC start_pc);

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  DirectEdge *AllocateDirectEdge(BlockMetaData *dest_block_meta);

  // Prepare a direct edge for patching.
  void PreparePatchDirectEdge(DirectEdge *edge);

  // Allocates an indirect edge data structure.
  IndirectEdge *AllocateIndirectEdge(const BlockMetaData *source_block_meta,
                                     const BlockMetaData *dest_block_meta);

  // Get a pointer to this context's code cache index.
  Index *CodeCacheIndex(void);

  // Invalidate blocks that have been committed to the code cache index. This
  // invalidates all blocks in the range `[begin_addr, end_addr)`.
  //
  // Note: We assume that `begin_addr <= end_addr` and that both `begin_addr`
  //       and `end_addr` are page-aligned.
  void InvalidateIndexedBlocks(AppPC begin_addr, AppPC end_addr);

  // Returns a pointer to the `arch::MachineContextCallback` associated with
  // the context-callable function at `func_addr`.
  const arch::Callback *ContextCallback(AppPC func_pc);

  // Returns a pointer to the code cache code associated with some outline-
  // callable function at `func_addr`.
  const arch::Callback *InlineCallback(InlineFunctionCall *call);

#ifdef GRANARY_WHERE_kernel
  // Returns a pointer to the code that can disable interrupts.
  CachePC DisableInterruptCode(void) const;

  // Returns a pointer to the code that can enable interrupts.
  CachePC EnableInterruptCode(void) const;
#endif  // GRANARY_WHERE_kernel

 private:

  // List of patched and not-yet-patched direct edges, as well as a lock that
  // protects both lists.
  SpinLock edge_list_lock;
  DirectEdge *edge_list;
  DirectEdge *unpatched_edge_list;
  DirectEdge *patched_edge_list;

  // List of indirect edges.
  SpinLock indirect_edge_list_lock;
  IndirectEdge *indirect_edge_list;

  // Code cache index for normal blocks.
  //
  // TODO(pag): Make this into a global that is available via `index.h` rather
  //            than going through the `Context`.
  Index *code_cache_index;

  // Mapping of context callback functions to their code cache equivalents. In
  // the code cache, these functions are wrapped with code that saves/restores
  // registers, etc.
  os::Lock context_callbacks_lock;
  TinyMap<AppPC, arch::Callback *, 8> context_callbacks;

  // Mapping of arguments callback functions to their code cache equivalents.
  // In the code cache, these functions are wrapped with code that saves/
  // restores registers, etc.
  os::Lock inline_callbacks_lock;
  TinyMap<AppPC, arch::Callback *, 8> inline_callbacks;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Context);
};

// Initializes a new active context.
void InitContext(void);

// Destroys the active context.
void ExitContext(void);

// Loads the active context.
Context *GlobalContext(void);

}  // namespace granary

#endif  // GRANARY_CONTEXT_H_
