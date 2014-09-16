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
#include "granary/metadata.h"
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
class MetaDataManager;
class InstrumentationTool;
class InstrumentationManager;

namespace arch {
class MachineContextCallback;
}  // namespace arch

#ifdef GRANARY_TARGET_test

// Interface for contexts in Granary. Used only for the `test` target for
// mocking out the `Context` class. Beyond that, the extra indirection
// introduced by the virtual dispatch is unwanted.
class ContextInterface {
 public:
  ContextInterface(void) = default;

  // Needed for linking against the base vtable.
  virtual ~ContextInterface(void);

  virtual void InitTools(InitReason, const char *tool_names) = 0;

  virtual void ExitTools(ExitReason) = 0;

  // Allocate and initialize some `BlockMetaData`.
  virtual BlockMetaData *AllocateBlockMetaData(AppPC start_pc) = 0;

  // Allocate and initialize some `BlockMetaData`, based on some existing
  // meta-data `meta`.
  virtual BlockMetaData *AllocateBlockMetaData(const BlockMetaData *meta,
                                               AppPC start_pc) = 0;

  // Allocate and initialize some empty `BlockMetaData`.
  virtual BlockMetaData *AllocateEmptyBlockMetaData(void) = 0;

  // Register some meta-data with Granary.
  virtual void RegisterMetaData(const MetaDataDescription *desc) = 0;

  // Allocate instances of the tools that will be used to instrument blocks.
  virtual InstrumentationTool *AllocateTools(void) = 0;

  // Free the allocated tools.
  virtual void FreeTools(InstrumentationTool *tools) = 0;

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  virtual DirectEdge *AllocateDirectEdge(BlockMetaData *dest_block_meta) = 0;

  // Allocates an indirect edge data structure.
  virtual IndirectEdge *AllocateIndirectEdge(
      const BlockMetaData *dest_block_meta) = 0;

  // Returns a pointer to the code cache that is used for allocating code for
  // basic blocks.
  virtual CodeCache *BlockCodeCache(void) = 0;

  // Get a pointer to this context's code cache index.
  virtual LockedIndex *CodeCacheIndex(void) = 0;

  // Invalidate blocks that have been committed to the code cache index.
  virtual void InvalidateIndexedBlocks(AppPC begin_addr, AppPC end_addr) = 0;

  // Returns a pointer to the `arch::MachineContextCallback` associated with
  // the context-callable function at `func_addr`.
  virtual arch::MachineContextCallback *ContextCallback(
      uintptr_t func_addr) = 0;
};

#else
# ifndef ContextInterface
#   define ContextInterface Context
# endif
#endif  // GRANARY_TARGET_test

// Groups together all of the major data structures related to an
// instrumentation "session". All non-trivial state is packaged within the
// context
class Context GRANARY_IF_TEST( : public ContextInterface ) {
 public:
  Context(void);

  GRANARY_TEST_VIRTUAL
  ~Context(void);

  // Initialize all tools from a comma-separated list of tools.
  GRANARY_TEST_VIRTUAL
  void InitTools(InitReason reason, const char *tool_names);

  // Exit all tools.
  GRANARY_TEST_VIRTUAL
  void ExitTools(ExitReason reason);

  // Allocate and initialize some `BlockMetaData`.
  GRANARY_TEST_VIRTUAL
  BlockMetaData *AllocateBlockMetaData(AppPC start_pc);

  // Allocate and initialize some `BlockMetaData`, based on some existing
  // meta-data template `meta_template`.
  GRANARY_TEST_VIRTUAL
  BlockMetaData *AllocateBlockMetaData(const BlockMetaData *meta_template,
                                       AppPC start_pc);

  // Allocate and initialize some empty `BlockMetaData`.
  GRANARY_TEST_VIRTUAL
  BlockMetaData *AllocateEmptyBlockMetaData(void);

  // Register some meta-data with Granary.
  GRANARY_TEST_VIRTUAL
  void RegisterMetaData(const MetaDataDescription *desc);

  // Allocate instances of the tools that will be used to instrument blocks.
  GRANARY_TEST_VIRTUAL
  InstrumentationTool *AllocateTools(void);

  // Free the allocated tools.
  GRANARY_TEST_VIRTUAL
  void FreeTools(InstrumentationTool *tools);

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  GRANARY_TEST_VIRTUAL
  DirectEdge *AllocateDirectEdge(BlockMetaData *dest_block_meta);

  // Allocates an indirect edge data structure.
  GRANARY_TEST_VIRTUAL
  IndirectEdge *AllocateIndirectEdge(const BlockMetaData *dest_block_meta);

  // Returns a pointer to the code cache that is used for allocating code for
  // basic blocks.
  GRANARY_TEST_VIRTUAL
  CodeCache *BlockCodeCache(void);

  // Get a pointer to this context's code cache index.
  GRANARY_TEST_VIRTUAL
  LockedIndex *CodeCacheIndex(void);

  // Invalidate blocks that have been committed to the code cache index. This
  // invalidates all blocks in the range `[begin_addr, end_addr)`.
  //
  // Note: We assume that `begin_addr <= end_addr` and that both `begin_addr`
  //       and `end_addr` are page-aligned.
  GRANARY_TEST_VIRTUAL
  void InvalidateIndexedBlocks(AppPC begin_addr, AppPC end_addr);

  // Returns a pointer to the `arch::MachineContextCallback` associated with
  // the context-callable function at `func_addr`.
  GRANARY_TEST_VIRTUAL
  arch::MachineContextCallback *ContextCallback(uintptr_t func_addr);

 private:
  // Manages all meta-data allocated/understood by this environment.
  MetaDataManager metadata_manager;

  // Manages all tools that instrument code that is taken over by this
  // environment.
  InstrumentationManager tool_manager;

  // Manages all basic block code allocated/understood by this environment.
  os::Module *block_code_cache_mod;
  CodeCache block_code_cache;

  // Manages all edge code allocated/understood by this environment.
  os::Module *edge_code_cache_mod;
  CodeCache edge_code_cache;

  // Pointer to the code that performs the flag saving and stack switching for
  // in/direct edge code. This code is is the first step in entering Granary
  // via a direct edge code stub / in-edge jump.
  CachePC direct_edge_entry_code;
  CachePC indirect_edge_entry_code;

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
  // the code cache, these functions are wrapped with code that save/restore
  // registers, etc.
  SpinLock context_callbacks_lock;
  TinyMap<uintptr_t, arch::MachineContextCallback *, 32> context_callbacks;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Context);
};

// Initializes a new active context.
void InitContext(void);

// Loads the active context.
ContextInterface *GlobalContext(void);

}  // namespace granary

#endif  // GRANARY_CONTEXT_H_
