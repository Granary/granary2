/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/instrument.h"
#include "granary/metadata.h"

#include "os/module.h"

#include "test/context.h"
#include "test/index.h"
#include "test/tool.h"

using namespace testing;
using namespace granary;

class ToolA : public MockTool {
 public:
  ToolA(void) {
    RegisterMetaData<AppMetaData>();
  }
  virtual ~ToolA(void) = default;
};

class ToolB : public MockTool {
 public:
  virtual ~ToolB(void) = default;
};

class ToolC : public MockTool {
 public:
  virtual ~ToolC(void) = default;
};

class ToolEnvironment : public ::testing::Environment {
 public:
  virtual ~ToolEnvironment(void) = default;

  void SetUp(void) {
    RegisterInstrumentationTool<ToolA>("a");
    RegisterInstrumentationTool<ToolB>("b", {"a"});
    RegisterInstrumentationTool<ToolC>("c", {"b"});
  }
};

static auto global_env = AddGlobalTestEnvironment(new ToolEnvironment);

class InstrumentTest : public Test {
 protected:
  InstrumentTest(void)
      : m1(&context),
        m2(&context),
        m3(&context),
        index(new MockIndex),
        locked_index(index) {
    m1.Register("a");
    m2.Register("b");  // Registers `ToolA` and `ToolB`.
    m3.Register("c");  // Registers `ToolA`, `ToolB`, and `ToolC`.
  }
  virtual ~InstrumentTest(void) = default;

  MockContext context;

  InstrumentationManager m1;
  InstrumentationManager m2;
  InstrumentationManager m3;

  MetaDataManager metadata_manager;

  MockIndex *index;
  granary::LockedIndex locked_index;
};

// Test running ToolA on invalid (nullptr) code.
TEST_F(InstrumentTest, InstrumentNothing) {
  auto module_meta_desc = MetaDataDescription::Get<AppMetaData>();

  EXPECT_CALL(context, RegisterMetaData(module_meta_desc))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&] {
        metadata_manager.Register(module_meta_desc);
      }));

  InstrumentationTool *tool_a_generic = m1.AllocateTools();
  ToolA *tool_a = UnsafeCast<ToolA *>(tool_a_generic);
  auto meta = metadata_manager.Allocate();

  EXPECT_CALL(context, CodeCacheIndex())
      .WillRepeatedly(Return(&locked_index));

  EXPECT_CALL(*index, Request(meta))
     .WillOnce(Return(IndexFindResponse{UnificationStatus::REJECT, nullptr}));

  EXPECT_CALL(context, AllocateTools())
      .Times(1)
      .WillOnce(Return(tool_a_generic));

  EXPECT_CALL(*tool_a, InstrumentControlFlow(_, _))
      .Times(1);

  EXPECT_CALL(*tool_a, InstrumentBlocks(_))
      .Times(1);

  EXPECT_CALL(*tool_a, InstrumentBlock(_))
      .Times(1);

  EXPECT_CALL(context, FreeTools(tool_a_generic))
      .Times(1)
      .WillOnce(Invoke([&] (InstrumentationTool *tools) {
        m1.FreeTools(tools);
      }));

  do {
    LocalControlFlowGraph cfg(&context);  // Meta-data will be cleaned up.
    BinaryInstrumenter inst(&context, &cfg, meta);
    inst.InstrumentDirect();
  } while (0);
}
