/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_bool(count_execs, false,
    "Count the number of times each block is executed. This option is only "
    "meaningful for dynamic instrumentation. By default, `count_bbs` does not "
    "count the number of executions of each basic block.",

    "count_bbs");

GRANARY_DEFINE_bool(count_per_condition, false,
    "Count the number of times each block is executed with respect to the "
    "last conditional branch within the current function.\n"
    "\n"
    "Note: This is only relevant if `count_execs` is used.\n"
    "\n"
    "Note: If there are three blocks, A -> B -> C, such that the branch\n"
    "      from A to B is conditional, but B to C is unconditional, then\n"
    "      both B and C will be specialized with respect to A.",

    "count_bbs");

// Records the static number of basic blocks. This could be an underestimation
// of the total number of basic blocks in the instrumented binary, but an
// overestimate of the total number of *distinct* basic blocks instrumented
// (because of race conditions when two threads simultaneously instrument the
// same basic block).
std::atomic<uint64_t> gNumBlocks = ATOMIC_VAR_INIT(0);

// Runtime block execution counter.
class CounterMetaData : public MutableMetaData<CounterMetaData> {
 public:
  CounterMetaData(void)
      : count(0) {}

  uint64_t count;
};

// Function and conditional arc context meta-data.
class CondArcMetaData : public IndexableMetaData<CondArcMetaData> {
 public:
  CondArcMetaData(void)
      : branch_pc_low16(0) {}

  bool Equals(const CondArcMetaData &that) const {
    return branch_pc_low16 == that.branch_pc_low16;
  }

  uint16_t branch_pc_low16;
};

// Simple tool for static and dynamic basic block counting.
class BBCount : public InstrumentationTool {
 public:
  static void Init(InitReason reason) {
    if (kInitThread == reason) return;
    if (FLAG_count_execs) {
      AddMetaData<CounterMetaData>();
      if (FLAG_count_per_condition) AddMetaData<CondArcMetaData>();
    }
  }

  static void Exit(ExitReason reason) {
    if (kExitProgram == reason || kExitDetach == reason) {
      if (FLAG_count_execs) ForEachMetaData(LogMetaInfo);
      os::Log("#count_bbs %lu blocks were translated.\n", gNumBlocks.load());
    }
  }

  virtual ~BBCount(void) = default;

  // Instrument an individual decoded block.
  virtual void InstrumentBlock(DecodedBlock *block) {
    if (IsA<CompensationBlock *>(block)) return;

    gNumBlocks.fetch_add(1);

    // Add an execution counter to each block.
    if (FLAG_count_execs) {
      auto count_meta = GetMetaData<CounterMetaData>(block);
      MemoryOperand counter_addr(&(count_meta->count));
      lir::InlineAssembly asm_(counter_addr);
      asm_.InlineAfter(block->FirstInstruction(), "INC m64 %0;"_x86_64);
    }
  }

  // If we're doing arc-specific counters, then propagate arc context to
  // successor blocks.
  virtual void InstrumentControlFlow(BlockFactory *, Trace *trace) {
    if (!FLAG_count_execs) return;
    if (!FLAG_count_per_condition) return;
    for (auto block : trace->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (!IsA<InstrumentedBlock *>(succ.block)) continue;
        if (succ.cfi->IsConditionalJump()) {
          MarkConditionalTarget(succ.cfi->DecodedPC(), succ.block);
        } else if (succ.cfi->IsFunctionCall() || succ.cfi->IsFunctionReturn()) {
          ResetConditionalTarget(succ.block);
        } else {
          CopyConditionalSource(block, succ.block);
        }
      }
    }
  }

 private:

  // Log the execution counter for each block.
  static void LogMetaInfo(const BlockMetaData *meta, IndexedStatus) {
    auto app_meta = MetaDataCast<const AppMetaData *>(meta);
    auto count_meta = MetaDataCast<const CounterMetaData *>(meta);
    auto offset = os::ModuleOffsetOfPC(app_meta->start_pc);
    if (FLAG_count_per_condition) {
      auto arc_meta = MetaDataCast<CondArcMetaData *>(meta);
      os::Log("B %s %lx A %x C %lu\n", offset.module->Name(), offset.offset,
              arc_meta->branch_pc_low16, count_meta->count);
    } else {
      os::Log("B %s %lx C %lu\n", offset.module->Name(), offset.offset,
              count_meta->count);
    }
  }

  static void ResetConditionalTarget(Block *target_block) {
    if (auto meta = GetMetaData<CondArcMetaData>(target_block)) {
      meta->branch_pc_low16 = 0;
    }
  }

  static void CopyConditionalSource(Block *source_block, Block *target_block) {
    if (auto source_meta = GetMetaData<CondArcMetaData>(source_block)) {
      if (auto dest_meta = GetMetaData<CondArcMetaData>(target_block)) {
        dest_meta->branch_pc_low16 = source_meta->branch_pc_low16;
      }
    }
  }

  static void MarkConditionalTarget(AppPC source_pc, Block *target_block) {
    if (!IsA<DirectBlock *>(target_block)) return;
    auto meta = GetMetaData<CondArcMetaData>(target_block);
    meta->branch_pc_low16 = static_cast<uint16_t>(
        reinterpret_cast<uintptr_t>(source_pc));
  }
};

// Initialize the `count_bbs` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<BBCount>("count_bbs");
}
