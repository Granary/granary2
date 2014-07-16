/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "granary/arch/init.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"

#include "granary/context.h"
#include "granary/tool.h"
#include "granary/translate.h"

using namespace granary;
using namespace testing;

// Decodes one block at a time.
class JitTool : public Tool {
 public:
  virtual ~JitTool(void) = default;
};

// Decodes one function at a time.
class FunctionTool : public Tool {
 public:
  virtual ~FunctionTool(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (!succ.cfi->IsFunctionCall()) {
          factory->RequestBlock(succ.block);
        }
      }
    }
  }
};

// Decodes into direct calls.
class CallTool : public Tool {
 public:
  virtual ~CallTool(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (succ.cfi->IsFunctionCall()) {
          factory->RequestBlock(succ.block);
        }
      }
    }
  }
};

// Force decodes the first block of up to `num_to_unroll` function calls.
class CallUnrollerTool : public Tool {
 public:
  int num_to_unroll;

  CallUnrollerTool(void)
      : num_to_unroll(10) {}
  virtual ~CallUnrollerTool(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                       LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (succ.cfi->IsFunctionCall()) {
          if (num_to_unroll--) {
            factory->RequestBlock(succ.block, BlockRequestKind::REQUEST_NOW);
          }
        }
      }
    }
  }
};

// Force decodes the first block of up to `num_to_unroll` direct or conditional
// jumps.
class JumpUnrollerTool : public Tool {
 public:
  int num_to_unroll;

  JumpUnrollerTool(void)
      : num_to_unroll(10) {}
  virtual ~JumpUnrollerTool(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                       LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (succ.cfi->IsJump()) {
          if (num_to_unroll--) {
            factory->RequestBlock(succ.block, BlockRequestKind::REQUEST_NOW);
          }
        }
      }
    }
  }
};

// Forces execution to go native on function calls.
class NativeCallTool : public Tool {
 public:
  virtual ~NativeCallTool(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (succ.cfi->IsFunctionCall()) {
          factory->RequestBlock(succ.block, BlockRequestKind::REQUEST_NATIVE);
        }
      }
    }
  }
};

#define TOOL_HARNESS(tool_name) \
    class tool_name ## _DirectControlFlowTest : public Test { \
     public: \
      tool_name ## _DirectControlFlowTest(void) \
          : context() { \
        context.InitTools(#tool_name); \
      } \
      \
      virtual ~tool_name ## _DirectControlFlowTest(void) = default; \
      \
      static void SetUpTestCase(void) { \
        arch::Init(); \
        RegisterTool<tool_name>(#tool_name); \
      } \
      \
     protected: \
      Context context; \
    }

TOOL_HARNESS(JitTool);
TOOL_HARNESS(FunctionTool);
TOOL_HARNESS(CallTool);
TOOL_HARNESS(CallUnrollerTool);
TOOL_HARNESS(JumpUnrollerTool);
TOOL_HARNESS(NativeCallTool);

namespace {

static int fibonacci_rec(int n) {
  if (!n) return n;
  if (1 == n) return 1;
  return fibonacci_rec(n - 1) + fibonacci_rec(n - 2);
}

static int fibonacci_iter(int n) {
  if (!n) return n;
  if (1 == n) return 1;
  int result = 0;
  int prev = 0;
  int prev_prev = 1;
  for (auto i = 2; i <= n; ++i) {
    result = prev + prev_prev;
    prev_prev = prev;
    prev = result;
  }
  return result;
}

static int factorial_rec(int n) {
  if (1 >= n) return 1;
  return n * factorial_rec(n - 1);
}

static int factorial_iter(int n) {
  auto res = 1;
  for (auto i = 2; i <= n; ++i) {
    res *= i;
  }
  return res;
}

}  // namespace

#define TEST_WITH_TOOLS(test_name, ...) \
    TEST_F(JitTool_DirectControlFlowTest, test_name) __VA_ARGS__ \
    TEST_F(FunctionTool_DirectControlFlowTest, test_name) __VA_ARGS__ \
    TEST_F(CallTool_DirectControlFlowTest, test_name) __VA_ARGS__ \
    TEST_F(CallUnrollerTool_DirectControlFlowTest, test_name) __VA_ARGS__ \
    TEST_F(JumpUnrollerTool_DirectControlFlowTest, test_name) __VA_ARGS__ \
    TEST_F(NativeCallTool_DirectControlFlowTest, test_name) __VA_ARGS__

TEST_WITH_TOOLS(RecursiveFibonacci, {
  auto inst = Translate(&context, fibonacci_rec);
  auto fibonacci_rec_inst = UnsafeCast<int(*)(int)>(inst);
  for (auto i = 0; i < 10; ++i) {
    EXPECT_EQ(fibonacci_rec(i), fibonacci_rec_inst(i));
  }
})

TEST_WITH_TOOLS(IterativeFibonacci, {
  auto inst = Translate(&context, fibonacci_iter);
  auto fibonacci_iter_inst = UnsafeCast<int(*)(int)>(inst);
  for (auto i = 0; i < 10; ++i) {
    EXPECT_EQ(fibonacci_iter(i), fibonacci_iter_inst(i));
  }
})

TEST_WITH_TOOLS(RecursiveFactorial, {
  auto inst = Translate(&context, factorial_rec);
  auto factorial_rec_inst = UnsafeCast<int(*)(int)>(inst);
  for (auto i = 0; i < 10; ++i) {
    EXPECT_EQ(factorial_rec(i), factorial_rec_inst(i));
  }
})

TEST_WITH_TOOLS(IterativeFactorial, {
  auto inst = Translate(&context, factorial_iter);
  auto factorial_iter_inst = UnsafeCast<int(*)(int)>(inst);
  for (auto i = 0; i < 10; ++i) {
    EXPECT_EQ(factorial_iter(i), factorial_iter_inst(i));
  }
})
