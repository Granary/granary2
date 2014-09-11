/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#include "clients/user/syscall.h"

using namespace granary;

// Defined in `clients/user/syscall.cc`, but not exported to other clients.
extern void HookSystemCallEntry(arch::MachineContext *);
extern void HookSystemCallExit(arch::MachineContext *);
extern void RemoveOldHooks(void);

namespace {

#define __NR_munmap         11
#define __NR_rt_sigaction   13
#define SIGSEGV             11
#define SIGILL              4
#define SIGUNUSED           31

// Prevents user-space code from replacing the `SIGSEGV` and `SIGILL`
// signal handlers. This is to help in the debugging of user space
// programs, where attaching GDB early on in the program's execution
// causes the bug to disappear.
static void SuppressSigAction(SystemCallContext ctx, void *) {
  if (__NR_rt_sigaction != ctx.Number()) return;

  // If `act == NULL` then code is querying the current state of the signal
  // handler.
  if (!ctx.Arg1()) return;

  auto signum = ctx.Arg0();
  if (SIGSEGV != signum && SIGILL != signum) return;

  // Turn this `sigaction` into a no-op (that will likely return `-EINVAL`)
  ctx.Arg0() = SIGUNUSED;
  ctx.Arg1() = 0;
  ctx.Arg2() = 0;
}

// Invalidates any code cache blocks related to an mmap request.
static void InvalidateUnmappedMemory(SystemCallContext ctx, void *) {
  if (__NR_munmap != ctx.Number()) return;
  auto addr = ctx.Arg0();
  auto len = ctx.Arg1();
  if (os::InvalidateModuleCode(reinterpret_cast<AppPC>(addr),
                               static_cast<int>(len))) {
    // TODO(pag): Evil hack that allows us to avoid proper code cache
    //            invalidation for the time being. We will elide a `munmap` if
    //            it's unmapping code.
    ctx.Arg1() = 0;
  }
}

}  // namespace

// Tool that helps user-space instrumentation work.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;

  virtual void Init(void) {
    AddSystemCallEntryFunction(SuppressSigAction);
    AddSystemCallEntryFunction(InvalidateUnmappedMemory);
  }

  virtual void Exit(void) {
    RemoveOldHooks();
  }

  // Adds in the hooks that allow other tools (including this tool) to hook
  // the system call handlers in high-level way.
  void InstrumentSyscall(ControlFlowInstruction *syscall) {
    syscall->InsertBefore(lir::CallWithContext(HookSystemCallEntry));
    syscall->InsertAfter(lir::CallWithContext(HookSystemCallExit));
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
