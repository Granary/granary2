/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

// Tool that implements several user-space special cases for instrumenting
// common binaries.
class UserSpaceInstrumenter : public Tool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      auto direct_block = DynamicCast<DirectBasicBlock *>(block);
      if (!direct_block) continue;

      // If this block targets `libdl` then detach.
      auto module = ModuleContainingPC(direct_block->StartAppPC());
      if (StringsMatch("dl", module->Name())) {
        factory->RequestBlock(direct_block, BlockRequestKind::REQUEST_NATIVE);
      }
    }
  }
};

// Initialize the `whole_func` tool.
GRANARY_CLIENT_INIT({
  RegisterTool<UserSpaceInstrumenter>("user");
})
