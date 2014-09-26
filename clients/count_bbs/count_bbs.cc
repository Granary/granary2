/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

GRANARY_DEFINE_bool(count_execs, false,
    "Count the number of times each block is executed. This option is only "
    "meaningful for dynamic instrumentation. By default, `count_bbs` does not "
    "count the number of executions of each basic block.",

    "count_bbs");

// Records the static number of basic blocks. This could be an underestimation
// of the total number of basic blocks in the instrumented binary, but an
// overestimate of the total number of *distinct* basic blocks instrumented
// (because of race conditions when two threads simultaneously instrument the
// same basic block).
std::atomic<uint64_t> NUM_BBS(ATOMIC_VAR_INIT(0));

// Runtime block execution counter.
class BlockCounter : public MutableMetaData<BlockCounter> {
 public:
  BlockCounter(void)
      : count(0) {}

  uint64_t count;
};

// Simple tool for static and dynamic basic block counting.
class BBCount : public InstrumentationTool {
 public:
  virtual void Init(InitReason) {
    if (FLAG_count_execs) {
      RegisterMetaData<BlockCounter>();
    }
  }

  virtual void Exit(ExitReason) {
    os::Log(os::LogDebug, "%lu blocks were translated.\n",
            NUM_BBS.load());
  }

  virtual ~BBCount(void) = default;
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    if (IsA<CompensationBasicBlock *>(bb)) return;

    NUM_BBS.fetch_add(1);

    if (FLAG_count_execs) {
      MemoryOperand counter_addr(&(GetMetaData<BlockCounter>(bb)->count));
      lir::InlineAssembly asm_({&counter_addr});
      asm_.InlineAfter(bb->FirstInstruction(), "INC m64 %0;"_x86_64);
    }
  }
};

// Initialize the `count_bbs` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<BBCount>("count_bbs");
})
