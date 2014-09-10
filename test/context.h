/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef TEST_CONTEXT_H_
#define TEST_CONTEXT_H_

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "granary/context.h"

// Implements a mock Granary `Context`.
class MockContext : public granary::ContextInterface {
 public:
  MockContext(void) = default;
  virtual ~MockContext(void) = default;

  MOCK_METHOD1(InitTools, void(const char *));

  // Returns a pointer to the module containing some program counter.
  MOCK_METHOD1(FindModuleContainingPC,
               const granary::os::Module *(granary::AppPC));

  // Returns a pointer to the module containing some program counter.
  MOCK_METHOD1(FindModuleByName, const granary::os::Module *(const char *));

  // Returns an iterator to all currently loaded modules.
  MOCK_CONST_METHOD0(LoadedModules, granary::os::ConstModuleIterator(void));

  // Allocate and initialize some `BlockMetaData`.
  MOCK_METHOD1(AllocateBlockMetaData,
               granary::BlockMetaData *(granary::AppPC));

  // Allocate and initialize some `BlockMetaData`, based on some existing
  // meta-data template.
  MOCK_METHOD2(AllocateBlockMetaData,
               granary::BlockMetaData *(const granary::BlockMetaData *,
                                        granary::AppPC pc));

  // Allocate and initialize some empty `BlockMetaData`.
  MOCK_METHOD0(AllocateEmptyBlockMetaData,
               granary::BlockMetaData *());

  // Register some meta-data with Granary.
  MOCK_METHOD1(RegisterMetaData,
               void (const granary::MetaDataDescription *));

  // Compile some code into one of the code caches.
  MOCK_METHOD1(Compile,
               void (granary::LocalControlFlowGraph *));

  // Allocate instances of the tools that will be used to instrument blocks.
  MOCK_METHOD0(AllocateTools,
               granary::InstrumentationTool *());

  // Free the allocated tools.
  MOCK_METHOD1(FreeTools,
               void (granary::InstrumentationTool *tools));

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  MOCK_METHOD1(AllocateDirectEdge,
               granary::DirectEdge *(granary::BlockMetaData *));

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  MOCK_METHOD1(AllocateIndirectEdge,
               granary::IndirectEdge *(const granary::BlockMetaData *));

  // Instantiates an indirect edge. This creates an out-edge that targets
  // `cache_pc` if the indirect CFI being taken is trying to jump to `app_pc`.
  // `edge->in_edge_pc` is updated in place to reflect the new target.
  MOCK_METHOD3(InstantiateIndirectEdge,
               void(granary::IndirectEdge *, granary::AppPC,
                    granary::CompensationBasicBlock *));

  // Returns a pointer to the code cache that is used for allocating code for
  // basic blocks.
  MOCK_METHOD0(BlockCodeCache, granary::CodeCache *(void));

  // Get a pointer to this context's code cache index.
  MOCK_METHOD0(CodeCacheIndex, granary::LockedIndex *(void));

  // Returns a pointer to the `arch::MachineContextCallback` associated with
  // the context-callable function at `func_addr`.
  MOCK_METHOD1(ContextCallback,
               granary::arch::MachineContextCallback *(uintptr_t func_addr));

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(MockContext);
};

#endif  // TEST_CONTEXT_H_
