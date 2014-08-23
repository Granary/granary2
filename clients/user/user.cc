/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

// Tool that implements several kernel-space special cases for instrumenting
// common binaries.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;

  void InstrumentSyscall(ControlFlowInstruction *syscall) {
    BeginInlineAssembly();
    InlineBefore(syscall, "CMP r32 EAX, i32 13;"
                          "JNZ l %1;"
                          "CMP r32 EDI, i32 11;"
                          "JNZ l %1;"
                          "MOV r64 RAX, i64 -22;"  // `-EINVAL`.
                          "JMP l %0;"
                          "LABEL %1:"_x86_64);
    InlineAfter(syscall,  "LABEL %0:"_x86_64);
    EndInlineAssembly();
  }

  virtual void InstrumentBlock(DecodedBasicBlock *block) {
    for (auto succ : block->Successors()) {
      if (succ.cfi->IsSystemCall()) {
        InstrumentSyscall(succ.cfi);
      }
    }
  }
};

// Initialize the `kernel` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<UserSpaceInstrumenter>("user");
})
