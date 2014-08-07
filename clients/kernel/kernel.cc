/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

// Tool that implements several kernel-space special cases for instrumenting
// common binaries.
class KernelSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~KernelSpaceInstrumenter(void) = default;
  virtual void InstrumentEntryPoint(BlockFactory *factory,
                                    CompensationBasicBlock *block,
                                    EntryPointKind kind, int) {
    if (ENTRYPOINT_KERNEL_SYSCALL == kind) {
      for (auto succ : block->Successors()) {
        factory->RequestBlock(succ.block, REQUEST_NATIVE);
      }
    }
  }
};

// Initialize the `kernel` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<KernelSpaceInstrumenter>("kernel");
})
