/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ENVIRONMENT_H_
#define GRANARY_ENVIRONMENT_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class Instruction;
class DecodedBasicBlock;
class BlockMetaData;
GRANARY_INTERNAL_DEFINITION class ModuleManager;
GRANARY_INTERNAL_DEFINITION class CodeAllocator;

// Interface for environments in Granary.
class EnvironmentInterface {
 public:
  EnvironmentInterface(void) = default;
  virtual ~EnvironmentInterface(void) = default;

  // Annotates the instruction, or adds an annotated instruction into the
  // instruction list. This returns the first
  GRANARY_INTERNAL_DEFINITION
  virtual void AnnotateInstruction(Instruction *instr) = 0;

  // Allocate and initialize some `BlockMetaData`.
  GRANARY_INTERNAL_DEFINITION
  virtual BlockMetaData *AllocateBlockMetaData(AppPC start_pc) = 0;

  // Allocate and initialize some empty `BlockMetaData`.
  GRANARY_INTERNAL_DEFINITION
  virtual BlockMetaData *AllocateEmptyBlockMetaData(void) = 0;

  // Allocate some edge code from the edge code cache.
  GRANARY_INTERNAL_DEFINITION
  virtual CachePC AllocateEdgeCode(int num_bytes) = 0;
};

#ifdef GRANARY_INTERNAL

// Manages environmental information that changes how Granary behaves. For
// example, in the Linux kernel, the environmental data gives the instruction
// decoder access to the kernel's exception tables, so that it can annotate
// instructions as potentially faulting.
class Environment : public EnvironmentInterface {
 public:
  Environment(void) = delete;
  virtual ~Environment(void) = default;

  // Initialize the Environment with a module manager and edge code allocator.
  inline Environment(ModuleManager *module_manager_,
                     CodeAllocator *edge_code_allocator_)
      : module_manager(module_manager_),
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

  ModuleManager * const module_manager;
  CodeAllocator * const edge_code_allocator;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Environment);
};
#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_ENVIRONMENT_H_
