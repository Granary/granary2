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

  // Annotates the instruction, or adds an annotated instruction into the
  // instruction list. This returns the first
  MOCK_METHOD1(AnnotateInstruction,
               void(granary::Instruction *instr));

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
               granary::Tool *());

  // Free the allocated tools.
  MOCK_METHOD1(FreeTools,
               void (granary::Tool *tools));

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  MOCK_METHOD2(AllocateDirectEdge,
               granary::DirectEdge *(const granary::BlockMetaData *,
                                     granary::BlockMetaData *));

  // Allocates a direct edge data structure, as well as the code needed to
  // back the direct edge.
  MOCK_METHOD2(AllocateIndirectEdge,
               granary::IndirectEdge *(const granary::BlockMetaData *,
                                       const granary::BlockMetaData *));

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
  MOCK_METHOD0(CodeCacheIndex, granary::LockedIndex *());

  // Get a pointer to this context's shadow code cache index.
  MOCK_METHOD0(ShadowCodeCacheIndex, granary::LockedIndex *());

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(MockContext);
};

#endif  // TEST_CONTEXT_H_
