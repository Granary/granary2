/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

// Tool that helps user-space instrumentation work.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;

  void InstrumentSyscall(ControlFlowInstruction *syscall) {
    BeginInlineAssembly();

    // Prevents user-space code from replacing the `SIGSEGV` an `SIGILL`
    // signal handlers. This is to help in the debugging of user space
    // programs, where attaching GDB early on in the program's execution
    // causes the bug to disappear.
    //
    // In this code, `EAX` is the Linux kernel ABI-defined register for
    // passing the syscall number, and `EDI` is the register for passing the
    // first argument, in this case, the signal number to the `rt_sigaction`
    // system call.
    InlineBefore(syscall, // Filter only on `rt_sigaction = 13`
                          "CMP r32 EAX, i32 13;"
                          "JNZ l %1;"

                          // Ignore if `act == NULL`, i.e. user space code
                          // is querying the current state of the signal
                          // handler.
                          "TEST r64 RSI, r64 RSI;"
                          "JZ l %1;"

                          // Precent overriding of `SIGSEGV = 11`.
                          "CMP r32 EDI, i32 11;"
                          "JNZ l %1;"

                          // Prevent overriding of `SIGILL = 4`.
                          "CMP r32 EDI, i32 4;"
                          "JNZ l %1;"

                          // Pretend that the syscall failed by returning
                          // `-EINVAL == -22`.
                          "MOV r64 RAX, i64 -22;"
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

// Initialize the `user` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<UserSpaceInstrumenter>("user");
})
