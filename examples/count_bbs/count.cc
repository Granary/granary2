/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

GRANARY_DEFINE_bool(count_execs, false,
    "Count the number of times each block is executed. This option is only "
    "meaningful for dynamic instrumentation. By default, `count_bbs` does not "
    "count the number of executions of each basic block.");

namespace {

// Runtime block execution counter.
class BlockCounter : public MutableMetaData<BlockCounter> {
 public:
  BlockCounter(void)
      : count(0) {}

  uint64_t count;
};

// Records the static number of basic blocks. This could be an underestimation
// of the total number of basic blocks in the instrumented binary, but an
// overestimate of the total number of *distinct* basic blocks instrumented
// (because of race conditions when two threads simultaneously instrument the
// same basic block).
static std::atomic<uint64_t> NUM_BBS(ATOMIC_VAR_INIT(0));

// Simple tool for static and dynamic basic block counting.
class BBCount : public Tool {
 public:
  BBCount(void) {
    if (FLAG_count_execs) {
      RegisterMetaData<BlockCounter>();
    }
  }

  virtual ~BBCount(void) = default;
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    BeginInlineAssembly();
    InlineBefore(bb->FirstInstruction(),
                 "XOR r64 %0, r64 %0;"
                 "ADD r64 %0, i32 1;"
                 "TEST r64 R13, r64 R14;"
                 "TEST r64 R15, r64 R14;"
                 "ADD r64 %0, i32 1;"_x86_64);
    EndInlineAssembly();

    NUM_BBS.fetch_add(1);
    if (!FLAG_count_execs) {
      return;
    }
    Instruction *insert_instr = bb->FirstInstruction();

    // Try to find a good place to insert this instruction such that the
    // placement is before an instruction that kills the flags (but doesn't
    // read them).
    for (auto instr : bb->ReversedAppInstructions()) {
      if (!IsA<ControlFlowInstruction *>(instr) &&
          instr->WritesConditionCodes() &&
          !instr->ReadsConditionCodes()) {
        insert_instr = instr;
      }
    }

    // Now that we have an insertion spot (either first instruction, or before
    // and instruction that kills the flags), go and insert the increment to
    // the block-specific execution counter.
    auto meta = GetMetaData<BlockCounter>(bb);
    MemoryOperand counter_addr(&(meta->count));
    BeginInlineAssembly({&counter_addr});
    InlineBefore(insert_instr, "INC m64 %0;"_x86_64);
    EndInlineAssembly();
  }
};

}  // namespace

// Initialize the `count_bbs` tool.
GRANARY_CLIENT_INIT({
  RegisterTool<BBCount>("count_bbs");
})
