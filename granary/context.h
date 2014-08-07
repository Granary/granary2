/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CONTEXT_H_
#define GRANARY_CONTEXT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/pc.h"

#include "granary/cache.h"
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

// Interface for environments in Granary.
class ContextInterface {
 public:
  ContextInterface(void) = default;

  // Needed for linking against the base vtable.
  virtual ~ContextInterface(void);

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
};

// Manages environmental information that changes how Granary behaves. For
// example, in the Linux kernel, the environmental data gives the instruction
// decoder access to the kernel's exception tables, so that it can annotate
// instructions as potentially faulting.
class Context : public ContextInterface {
 public:
  // Initialize the this instrumentation.
  Context(void);

  // Initialize all tools from a comma-separated list of tools.
  void InitTools(const char *tool_names);

  virtual ~Context(void);

  // Allocate and initialize some `BlockMetaData`.
  virtual BlockMetaData *AllocateBlockMetaData(AppPC start_pc) override;

  // Allocate and initialize some `BlockMetaData`, based on some existing
  // meta-data template `meta_template`.
  virtual BlockMetaData *AllocateBlockMetaData(
      const BlockMetaData *meta_template, AppPC start_pc) override;

  // Allocate and initialize some empty `BlockMetaData`.
  virtual BlockMetaData *AllocateEmptyBlockMetaData(void) override;

  // Register some meta-data with Granary.
  virtual void RegisterMetaData(const MetaDataDescription *desc) override;

  // Allocate instances of the tools that will be used to instrument blocks.
  virtual InstrumentationTool *AllocateTools(void) override;

  // Free the allocated tools.
  virtual void FreeTools(InstrumentationTool *tools) override;

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  virtual DirectEdge *AllocateDirectEdge(
      BlockMetaData *dest_block_meta) override;

  // Allocates an indirect edge data structure.
  virtual IndirectEdge *AllocateIndirectEdge(
      const BlockMetaData *dest_block_meta) override;

  // Returns a pointer to the code cache that is used for allocating code for
  // basic blocks.
  virtual CodeCache *BlockCodeCache(void) override;

  // Get a pointer to this context's code cache index.
  virtual LockedIndex *CodeCacheIndex(void) override;

 private:
  // Manages all metadata allocated/understood by this environment.
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
  FineGrainedLock edge_list_lock;
  DirectEdge *patched_edge_list;
  DirectEdge *unpatched_edge_list;

  // List of indirect edges.
  FineGrainedLock indirect_edge_list_lock;
  IndirectEdge *indirect_edge_list;

  // Code cache index for normal blocks.
  LockedIndex code_cache_index;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Context);
};

// Changes the active context.
void SetGlobalContext(ContextInterface *context);

// Loads the active context.
ContextInterface *GlobalContext(void);

}  // namespace granary

#endif  // GRANARY_CONTEXT_H_
