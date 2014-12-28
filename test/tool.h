/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef TEST_TOOL_H_
#define TEST_TOOL_H_

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "granary/tool.h"

// Implements a mock Granary `Tool`.
class MockTool : public granary::InstrumentationTool {
 public:
  MockTool(void) = default;
  virtual ~MockTool(void) = default;

  // Used to instrument control-flow instructions and decide how basic blocks
  // should be materialized.
  //
  // This method is repeatedly executed until no more materialization
  // requests are made.
  MOCK_METHOD2(InstrumentControlFlow,
               void(granary::BlockFactory *materializer,
                    granary::Trace *cfg));

  // Used to implement more complex forms of instrumentation where tools need
  // to see the entire local control-flow graph.
  //
  // This method is executed once per tool per instrumentation session.
  MOCK_METHOD1(InstrumentBlocks,
               void(const granary::Trace *cfg));

  // Used to implement the typical JIT-based model of single basic-block at a
  // time instrumentation.
  //
  // This method is executed for each decoded BB in the local CFG,
  // but is never re-executed for the same (tool, BB) pair in the current
  // instrumentation session.
  MOCK_METHOD1(InstrumentBlock,
               void(granary::DecodedBlock *block));
};

#endif  // TEST_TOOL_H_
