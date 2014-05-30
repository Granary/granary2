/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CONTEXT_H_
#define GRANARY_CONTEXT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/pc.h"

#include "granary/cache.h"
#include "granary/metadata.h"
#include "granary/module.h"
#include "granary/tool.h"

namespace granary {

// Forward declarations.
class Instruction;
class DecodedBasicBlock;
class BlockMetaData;
class MetaDataDescription;
class ModuleManager;
class MetaDataManager;
class Tool;
class ToolManager;
class CodeCacheInterface;

// Interface for environments in Granary.
class ContextInterface {
 public:
  // Needed for linking against the base vtable.
  ContextInterface(void);

  virtual ~ContextInterface(void) = default;

  // Allocate and initialize some `BlockMetaData`.
  virtual BlockMetaData *AllocateBlockMetaData(AppPC start_pc) = 0;

  // Allocate and initialize some empty `BlockMetaData`.
  virtual BlockMetaData *AllocateEmptyBlockMetaData(void) = 0;

  // Allocate some edge code from the edge code cache.
  virtual CachePC AllocateEdgeCode(int num_bytes) = 0;

  // Register some meta-data with Granary.
  virtual void RegisterMetaData(const MetaDataDescription *desc) = 0;

  // Compile some code into one of the code caches.
  virtual void Compile(LocalControlFlowGraph *cfg) = 0;

  // Allocate instances of the tools that will be used to instrument blocks.
  virtual Tool *AllocateTools(void) = 0;

  // Free the allocated tools.
  virtual void FreeTools(Tool *tools) = 0;

  // Allocate a new code cache.
  //
  // Note: This should be a lightweight operation as it is usually invoked
  //       whilst fine-grained locks are held.
  virtual CodeCacheInterface *AllocateCodeCache(void) = 0;

  // Flush an entire code cache.
  //
  // Note: This should be a lightweight operation as it is usually invoked
  //       whilst fine-grained locks are held (e.g. schedule for the allocator
  //       to be freed).
  virtual void FlushCodeCache(CodeCacheInterface *cache) = 0;
};

// Manages environmental information that changes how Granary behaves. For
// example, in the Linux kernel, the environmental data gives the instruction
// decoder access to the kernel's exception tables, so that it can annotate
// instructions as potentially faulting.
class Context : public ContextInterface {
 public:
  virtual ~Context(void) = default;

  // Initialize the Context.
  Context(void);

  // Allocate and initialize some `BlockMetaData`.
  virtual BlockMetaData *AllocateBlockMetaData(AppPC start_pc) override;

  // Allocate and initialize some empty `BlockMetaData`.
  virtual BlockMetaData *AllocateEmptyBlockMetaData(void) override;

  // Allocate some edge code from the edge code cache.
  virtual CachePC AllocateEdgeCode(int num_bytes) override;

  // Register some meta-data with Granary.
  virtual void RegisterMetaData(const MetaDataDescription *desc) override;

  // Compile some code into one of the code caches.
  virtual void Compile(LocalControlFlowGraph *cfg) override;

  // Allocate instances of the tools that will be used to instrument blocks.
  virtual Tool *AllocateTools(void) override;

  // Free the allocated tools.
  virtual void FreeTools(Tool *tools) override;

  // Allocate a new code cache.
  //
  // Note: This should be a lightweight operation as it is usually invoked
  //       whilst fine-grained locks are held.
  virtual CodeCacheInterface *AllocateCodeCache(void) override;

  // Flush an entire code cache.
  //
  // Note: This should be a lightweight operation as it is usually invoked
  //       whilst fine-grained locks are held (e.g. schedule for the allocator
  //       to be freed).
  virtual void FlushCodeCache(CodeCacheInterface *cache);

 private:

  // Manages all modules allocated/understood by this environment.
  ModuleManager module_manager;

  // Manages all metadata allocated/understood by this environment.
  MetaDataManager metadata_manager;

  // Manages all tools that instrument code that is taken over by this
  // environment.
  ToolManager tool_manager;

  // Manages all edge code allocated/understood by this environment. Code cache
  // code is managed on a per-module address range basis.
  CodeCache edge_code_cache;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Context);
};

}  // namespace granary

#endif  // GRANARY_CONTEXT_H_
