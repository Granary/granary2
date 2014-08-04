/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "arch/init.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"

#include "granary/context.h"
#include "granary/tool.h"
#include "granary/translate.h"

// For each jump type, expand some macro with enough info to generate test
// code.
//
// Note: Not all possible condition codes need to be set here (especially for
//       things like jle where one of two conditions can be met); only one
//       satisfying condition needs to be met.
#define FOR_EACH_CBR(macro, ...) \
    macro(jo, OF, ~0, ##__VA_ARGS__) \
    macro(jno, 0, ~OF, ##__VA_ARGS__) \
    macro(jb, CF, ~0, ##__VA_ARGS__) \
    macro(jnb, 0, ~CF, ##__VA_ARGS__) \
    macro(jz, ZF, ~0, ##__VA_ARGS__) \
    macro(jnz, 0, ~ZF, ##__VA_ARGS__) \
    macro(jbe, (CF | ZF), ~0, ##__VA_ARGS__) \
    macro(jnbe, 0, ~(CF | ZF), ##__VA_ARGS__) \
    macro(js, SF, ~0, ##__VA_ARGS__) \
    macro(jns, 0, ~SF, ##__VA_ARGS__) \
    macro(jp, PF, ~0, ##__VA_ARGS__) \
    macro(jnp, 0, ~PF, ##__VA_ARGS__) \
    macro(jl, SF, ~0, ##__VA_ARGS__) \
    macro(jnl, 0, ~SF, ##__VA_ARGS__) \
    macro(jle, (ZF | SF), ~0, ##__VA_ARGS__) \
    macro(jnle, 0, ~(ZF | SF), ##__VA_ARGS__)

enum {
  CF = (1 << 0),  // carry
  PF = (1 << 2),  // parity
  AF = (1 << 4),  // adjust
  ZF = (1 << 6),  // zero
  SF = (1 << 7),  // sign
  DF = (1 << 10),  // direction
  OF = (1 << 11)  // overflow
};

#define DECLARE_COND_JUMP_TESTER(opcode, ...) \
  extern bool jcc_ ## opcode ## _true(void); \
  extern bool jcc_ ## opcode ## _false(void);

extern "C" {
FOR_EACH_CBR(DECLARE_COND_JUMP_TESTER)

extern bool jcc_jcxz_true(void);
extern bool jcc_jcxz_false(void);

extern bool jcc_jecxz_true(void);
extern bool jcc_jecxz_false(void);

extern bool jcc_jrcxz_true(void);
extern bool jcc_jrcxz_false(void);

extern int loop_return_5(void);
extern int loope_return_5(void);
extern int loopne_return_5(void);
}

using namespace granary;
using namespace testing;

// Decodes all blocks in the function, but doesn't look in the cache for them.
class AllFuncBlocks : public InstrumentationTool {
 public:
  virtual ~AllFuncBlocks(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        factory->RequestBlock(succ.block, BlockRequestKind::REQUEST_CHECK_LCFG);
      }
    }
  }
};

class ConditionalBranchTest : public Test {
 public:
  virtual ~ConditionalBranchTest(void) = default;
  ConditionalBranchTest(void)
      : context() {
    context.InitTools("all_func_blocks");
  }

  static void SetUpTestCase(void) {
    arch::Init();
    RegisterInstrumentationTool<AllFuncBlocks>("all_func_blocks");
  }

 protected:
  Context context;
};

#define JCC_TEST(opcode, ...) \
    TEST_F(ConditionalBranchTest, jCC_ ## opcode) { \
      auto inst_true = Translate(&context, jcc_ ## opcode ## _true); \
      auto inst_false = Translate(&context, jcc_ ## opcode ## _false); \
      EXPECT_TRUE(jcc_ ## opcode ## _true()); \
      EXPECT_TRUE(jcc_ ## opcode ## _false()); \
      auto inst_true_func = UnsafeCast<bool(*)(void)>(inst_true); \
      auto inst_false_func = UnsafeCast<bool(*)(void)>(inst_false); \
      EXPECT_TRUE(inst_true_func()); \
      EXPECT_TRUE(inst_false_func()); \
    }

FOR_EACH_CBR(JCC_TEST)

JCC_TEST(jecxz)
JCC_TEST(jrcxz)

#define LOOP_TEST(opcode, ...) \
    TEST_F(ConditionalBranchTest, opcode) { \
      auto inst = Translate(&context, opcode ## _return_5); \
      EXPECT_EQ(5, opcode ## _return_5()); \
      auto inst_func = UnsafeCast<int(*)(void)>(inst); \
      EXPECT_EQ(5, inst_func()); \
    }

LOOP_TEST(loop)
LOOP_TEST(loope)
LOOP_TEST(loopne)
