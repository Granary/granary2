/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CONTEXT_H_
#define GRANARY_CONTEXT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/types.h"

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
class CodeAllocator;

// Interface for environments in Granary.
class ContextInterface {
 public:
  // Needed for linking against the base vtable.
  ContextInterface(void);

  virtual ~ContextInterface(void) = default;

  // Annotates the instruction, or adds an annotated instruction into the
  // instruction list. This returns the first
  virtual void AnnotateInstruction(Instruction *instr) = 0;

  // Allocate and initialize some `BlockMetaData`.
  virtual BlockMetaData *AllocateBlockMetaData(AppPC start_pc) = 0;

  // Allocate and initialize some empty `BlockMetaData`.
  virtual BlockMetaData *AllocateEmptyBlockMetaData(void) = 0;

  // Allocate some edge code from the edge code cache.
  virtual CachePC AllocateEdgeCode(int num_bytes) = 0;

  // Register some meta-data with Granary.
  virtual void RegisterMetaData(const MetaDataDescription *desc) = 0;

  // Compile some code into one of the code caches.
  virtual void Compile(BlockMetaData *meta) = 0;

  // Allocate instances of the tools that will be used to instrument blocks.
  virtual Tool *AllocateTools(void) = 0;

  // Free the allocated tools.
  virtual void FreeTools(Tool *tools) = 0;
};

// Manages environmental information that changes how Granary behaves. For
// example, in the Linux kernel, the environmental data gives the instruction
// decoder access to the kernel's exception tables, so that it can annotate
// instructions as potentially faulting.
class Context : public ContextInterface {
 public:
  Context(void) = delete;
  virtual ~Context(void) = default;

  // Initialize the Context.
  inline Context(ModuleManager *module_manager_,
                 MetaDataManager *metadata_manager_,
                 ToolManager *tool_manager_,
                 CodeAllocator *edge_code_allocator_)
      : module_manager(module_manager_),
        metadata_manager(metadata_manager_),
        tool_manager(tool_manager_),
        edge_code_allocator(edge_code_allocator_) {}

  // Annotates the instruction, or adds an annotated instruction into the
  // instruction list. This returns the first
  virtual void AnnotateInstruction(Instruction *instr) override;

  // Allocate and initialize some `BlockMetaData`.
  virtual BlockMetaData *AllocateBlockMetaData(AppPC start_pc) override;

  // Allocate and initialize some empty `BlockMetaData`.
  virtual BlockMetaData *AllocateEmptyBlockMetaData(void) override;

  // Allocate some edge code from the edge code cache.
  virtual CachePC AllocateEdgeCode(int num_bytes) override;

  // Register some meta-data with Granary.
  virtual void RegisterMetaData(const MetaDataDescription *desc) override;

  // Compile some code into one of the code caches.
  virtual void Compile(BlockMetaData *meta) override;

  // Allocate instances of the tools that will be used to instrument blocks.
  virtual Tool *AllocateTools(void) override;

  // Free the allocated tools.
  virtual void FreeTools(Tool *tools) override;

 private:

  // Manages all modules allocated/understood by this environment.
  ModuleManager * const module_manager;

  // Manages all metadata allocated/understood by this environment.
  MetaDataManager * const metadata_manager;

  // Manages all tools that instrument code that is taken over by this
  // environment.
  ToolManager * const tool_manager;

  // Manages all edge code allocated/understoof by this environment. Code cache
  // code is managed on a per-module address range basis.
  CodeAllocator * const edge_code_allocator;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Context);
};

}  // namespace granary

#endif  // GRANARY_CONTEXT_H_
