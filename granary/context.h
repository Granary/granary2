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

namespace granary {

// Forward declarations.
class BlockMetaData;
class CodeCache;
class CompensationBasicBlock;
class DecodedBasicBlock;
class DirectEdge;
class IndirectEdge;
class Instruction;
class MetaDataDescription;
class InstrumentationTool;
class InstrumentationManager;
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

  // Initialize all tools from a comma-separated list of tools.
  void InitTools(InitReason reason);

  // Exit all tools.
  void ExitTools(ExitReason reason);

  // Allocate and initialize some `BlockMetaData`.
  BlockMetaData *AllocateBlockMetaData(AppPC start_pc);

  // Allocate and initialize some `BlockMetaData`, based on some existing
  // meta-data template `meta_template`.
  BlockMetaData *AllocateBlockMetaData(const BlockMetaData *meta_template,
                                       AppPC start_pc);

  // Allocate instances of the tools that will be used to instrument blocks.
  InstrumentationTool *AllocateTools(void);

  // Free the allocated tools.
  void FreeTools(InstrumentationTool *tools);

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  DirectEdge *AllocateDirectEdge(BlockMetaData *dest_block_meta);

  // Allocates an indirect edge data structure.
  IndirectEdge *AllocateIndirectEdge(const BlockMetaData *dest_block_meta);

  // Returns a pointer to the code cache that is used for allocating code for
  // basic blocks.
  CodeCache *BlockCodeCache(void);

  // Get a pointer to this context's code cache index.
  LockedIndex *CodeCacheIndex(void);

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
  // Manages all tools that instrument code that is taken over by this
  // environment.
  InstrumentationManager tool_manager;

  // Manages all basic block code allocated/understood by this environment.
  CodeCache block_code_cache;

  // Manages all edge code allocated/understood by this environment.
  CodeCache edge_code_cache;

  // Pointer to the code that performs the flag saving and stack switching for
  // in/direct edge code. This code is is the first step in entering Granary
  // via a direct edge code stub / in-edge jump.
  const CachePC direct_edge_entry_code;
  const CachePC indirect_edge_entry_code;

  // Pointer to the code that performs interrupt enabling and disabling.
  const CachePC disable_interrupts_code;
  const CachePC enable_interrupts_code;

  // List of patched and not-yet-patched direct edges, as well as a lock that
  // protects both lists.
  SpinLock edge_list_lock;
  DirectEdge *patched_edge_list;
  DirectEdge *unpatched_edge_list;

  // List of indirect edges.
  SpinLock indirect_edge_list_lock;
  IndirectEdge *indirect_edge_list;

  // Code cache index for normal blocks.
  LockedIndex code_cache_index;

  // Mapping of context callback functions to their code cache equivalents. In
  // the code cache, these functions are wrapped with code that saves/restores
  // registers, etc.
  SpinLock context_callbacks_lock;
  TinyMap<AppPC, arch::Callback *, 8> context_callbacks;

  // Mapping of arguments callback functions to their code cache equivalents.
  // In the code cache, these functions are wrapped with code that saves/
  // restores registers, etc.
  SpinLock arg_callbacks_lock;
  TinyMap<AppPC, arch::Callback *, 8> outline_callbacks;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Context);
};

// Initializes a new active context.
void InitContext(InitReason reason);

// Destroys the active context.
void ExitContext(ExitReason reason);

// Loads the active context.
Context *GlobalContext(void);

}  // namespace granary

#endif  // GRANARY_CONTEXT_H_
