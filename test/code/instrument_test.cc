/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/code/instrument.h"
#include "granary/metadata.h"
#include "granary/module.h"

#include "test/tool.h"
#include "test/context.h"

using namespace ::testing;
using namespace granary;

class ToolA : public MockTool {
 public:
  ToolA(void) {
    RegisterMetaData<ModuleMetaData>();
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
    RegisterTool<ToolA>("a");
    RegisterTool<ToolB>("b", {"a"});
    RegisterTool<ToolC>("c", {"b"});
  }
};

static auto global_env = AddGlobalTestEnvironment(new ToolEnvironment);

class InstrumentTest : public Test {
 protected:
  InstrumentTest(void) {
    m1.Register("a");
    m2.Register("b");  // Registers `ToolA` and `ToolB`.
    m3.Register("c");  // Registers `ToolA`, `ToolB`, and `ToolC`.
  }
  virtual ~InstrumentTest(void) = default;

  ToolManager m1;
  ToolManager m2;
  ToolManager m3;

  MockContext context;
  MetaDataManager metadata_manager;
};

// Test running ToolA on invalid (nullptr) code.
TEST_F(InstrumentTest, InstrumentNothing) {
  auto module_meta_desc = MetaDataDescription::Get<ModuleMetaData>();

  EXPECT_CALL(context, RegisterMetaData(module_meta_desc))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&] {
        metadata_manager.Register(module_meta_desc);
      }));

  Tool *tool_a_generic = m1.AllocateTools(&context);
  ToolA *tool_a = UnsafeCast<ToolA *>(tool_a_generic);

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
      .WillOnce(Invoke([&] (Tool *tools) {
        m1.FreeTools(tools);
      }));

  auto meta = metadata_manager.Allocate();
  do {
    LocalControlFlowGraph cfg;  // meta will be cleaned up.
    Instrument(&context, &cfg, meta);
  } while (0);
}
