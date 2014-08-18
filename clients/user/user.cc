/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

// Runtime block execution counter.
class ReturnAddressTracker : public IndexableMetaData<ReturnAddressTracker> {
 public:
  ReturnAddressTracker(void)
      : is_instrumented(true) {}

  bool Equals(const ReturnAddressTracker *that) const {
    return is_instrumented == that->is_instrumented;
  }

  // Tells us whether or not we think that the return address points to
  // instrumented code (`true`) or native code (`false`).
  bool is_instrumented;
};

// Tool that implements several user-space special cases for instrumenting
// common binaries.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual void Init(void) {
    RegisterMetaData<ReturnAddressTracker>();
  }

  virtual ~UserSpaceInstrumenter(void) = default;

  // Mark the meta-data of some instrumented block as returning to some native
  // code.
  void MarkRetAddressNative(BasicBlock *block) {
    auto inst_block = DynamicCast<InstrumentedBasicBlock *>(block);
    if (!inst_block) return;

    auto block_meta = GetMetaDataStrict<ReturnAddressTracker>(inst_block);
    block_meta->is_instrumented = false;
  }

  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    auto entry_meta = GetMetaData<ReturnAddressTracker>(cfg->EntryBlock());
    auto ret_addr_is_native = !entry_meta->is_instrumented;

    for (auto block : cfg->NewBlocks()) {
      if (GRANARY_UNLIKELY(ret_addr_is_native)) {
        for (auto succ : block->Successors()) {
          if (succ.cfi->IsJump() || succ.cfi->IsFunctionReturn()) {
            MarkRetAddressNative(succ.block);
          }
        }
        // Don't do the `ld` checks, as we're likely attaching during
        // `call_init` of `ld-linux-*.so`.
        continue;
      }
      GRANARY_UNUSED(factory);
/*
      auto direct_block = DynamicCast<DirectBasicBlock *>(block);
      if (!direct_block) continue;

      // If this block targets `libdl` or `libld` then detach.
      auto module = ModuleContainingPC(direct_block->StartAppPC());
      if (StringsMatch("dl", module->Name()) ||
          StringsMatch("ld", module->Name())) {
        factory->RequestBlock(direct_block, BlockRequestKind::REQUEST_NATIVE);
      } */
    }
  }

  // Used to instrument code entrypoints.
  virtual void InstrumentEntryPoint(BlockFactory *,
                                    CompensationBasicBlock *entry_block,
                                    EntryPointKind kind, int) {
    if (ENTRYPOINT_USER_LOAD != kind) return;
    auto meta = GetMetaData<ReturnAddressTracker>(entry_block);
    meta->is_instrumented = false;

    for (auto succ : entry_block->Successors()) {
      MarkRetAddressNative(succ.block);
    }
  }
};

// Initialize the `user` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<UserSpaceInstrumenter>("user");
})
