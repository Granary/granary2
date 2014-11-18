/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

GRANARY_USING_NAMESPACE granary;

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
class CounterMetaData : public MutableMetaData<CounterMetaData> {
 public:
  CounterMetaData(void)
      : count(0),
        next(nullptr) {}

  uint64_t count;

  // Next block meta-data.
  BlockMetaData *next;
};

namespace {

// Linked list of block meta-datas.
static BlockMetaData *gAllBlocks = nullptr;
static SpinLock gAllBlocksLock;

// Meta-data iterator, where the meta-data is chained together via the
// `BlockTypeInfo` type.
typedef MetaDataLinkedListIterator<CounterMetaData> BlockCounterMetaIterator;

}  // namespace

// Simple tool for static and dynamic basic block counting.
class BBCount : public InstrumentationTool {
 public:
  virtual void Init(InitReason) {
    if (FLAG_count_execs) {
      AddMetaData<CounterMetaData>();
    }
  }

  static void LogMetaInfo(BlockMetaData *meta) {
    auto app_meta = MetaDataCast<AppMetaData *>(meta);
    auto count_meta = MetaDataCast<CounterMetaData *>(meta);
    auto offset = os::ModuleOffsetOfPC(app_meta->start_pc);
    os::Log("B %s %lx C %lu\n", offset.module->Name(), offset.offset,
            count_meta->count);
  }

  virtual void Exit(ExitReason) {
    if (FLAG_count_execs) {
      SpinLockedRegion locker(&gAllBlocksLock);
      for (auto meta : BlockCounterMetaIterator(gAllBlocks)) {
        LogMetaInfo(meta);
      }
      gAllBlocks = nullptr;
    }
    os::Log("# %lu blocks were translated.\n", NUM_BBS.load());
  }

  virtual ~BBCount(void) = default;

  // Chain the meta-data into the global list.
  static void ChainBlockMeta(BlockMetaData *meta, CounterMetaData *count_meta) {
    SpinLockedRegion locker(&gAllBlocksLock);
    count_meta->next = gAllBlocks;
    gAllBlocks = meta;
  }

  // Add an execution counter to each block.
  static void AddExecCounter(DecodedBasicBlock *block,
                             CounterMetaData *count_meta) {
    MemoryOperand counter_addr(&(count_meta->count));
    lir::InlineAssembly asm_({&counter_addr});
    asm_.InlineAfter(block->FirstInstruction(), "INC m64 %0;"_x86_64);
  }

  virtual void InstrumentBlock(DecodedBasicBlock *block) {
    if (IsA<CompensationBasicBlock *>(block)) return;

    NUM_BBS.fetch_add(1);

    if (FLAG_count_execs) {
      auto meta = block->MetaData();
      auto count_meta = MetaDataCast<CounterMetaData *>(meta);
      ChainBlockMeta(meta, count_meta);
      AddExecCounter(block, count_meta);
    }
  }
};

// Initialize the `count_bbs` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<BBCount>("count_bbs");
}
