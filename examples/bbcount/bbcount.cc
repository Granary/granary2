/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

GRANARY_DEFINE_bool(count_execs, false,
    "Count the number of times each block is executed. This option is only "
    "meaningful for static instrumentation. By default, `bbcount` does not "
    "count the number of executions of each basic block.")

// Runtime block execution counter.
class BlockCounter : public MutableMetaData {
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
  virtual ~BBCount(void) = default;

  // Instrument a basic block.
  virtual void InstrumentBB(InFlightBasicBlock *bb) {
    NUM_BBS.fetch_add(1);
    if (!FLAG_count_execs) {
      return;
    }

    for (auto instr : bb->Instructions()) {
      GRANARY_UNUSED(instr);
    }
  }

} static BBCOUNT;

GRANARY_INIT(bbcount, {
  RegisterTool(&BBCOUNT);
  if (FLAG_count_execs) {
    RegisterMetaData<BlockCounter>();
  }
})
