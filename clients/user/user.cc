/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#include "clients/user/signal.h"
#include "clients/user/syscall.h"

using namespace granary;

extern "C" {

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_32BIT     0x40

extern void exit_group(int) __attribute__((noreturn));
extern void munmap(void *mem, unsigned long size);
extern void *mmap(void *__addr, size_t __len, int __prot, int __flags,
                  int __fd, long __offset);
}  // extern C

// Defined in `clients/user/signal.cc`, but not exported to other clients.
extern void InitGDBDebug(void);

// Defined in `clients/user/syscall.cc`, but not exported to other clients.
extern void HookSystemCallEntry(arch::MachineContext *);
extern void HookSystemCallExit(arch::MachineContext *);
extern void RemoveAllHooks(void);

namespace {

// Prevents user-space code from replacing the `SIGSEGV` and `SIGILL`
// signal handlers. This is to help in the debugging of user space
// programs, where attaching GDB early on in the program's execution
// causes the bug to disappear.
static void SuppressSigAction(SystemCallContext ctx, void *) {
  if (__NR_rt_sigaction != ctx.Number()) return;

  // If `act == NULL` then code is querying the current state of the signal
  // handler.
  if (!ctx.Arg1()) return;

  // Turn this `sigaction` into a no-op (that will likely return `-EINVAL`)
  auto signum = ctx.Arg0();
  if (SIGILL == signum || SIGTRAP == signum ||
      SIGBUS == signum || SIGSEGV == signum) {
    ctx.Arg0() = SIGUNUSED;
    ctx.Arg1() = 0;
    ctx.Arg2() = 0;
  }
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
    munmap(reinterpret_cast<void *>(addr), len);
    ctx.Number() = __NR_mmap;
    ctx.Arg2() = 0;  // PROT_NONE.
    ctx.Arg3() = MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED;
    ctx.Arg4() = std::numeric_limits<uintptr_t>::max();  // -1.
    ctx.Arg5() = 0;  // offset.
  }
}

// Handle proper Granary exit procedures. Granary's `exit_group` function
// deals with proper `Exit`ing of all tools.
static void ExitGranary(SystemCallContext ctx, void *) {
  if (__NR_exit_group == ctx.Number()) {
    exit_group(static_cast<int>(ctx.Arg0()));
  }
}

}  // namespace

// Tool that helps user-space instrumentation work.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;

  virtual void Init(InitReason) {
    AddSystemCallEntryFunction(SuppressSigAction);
    AddSystemCallEntryFunction(InvalidateUnmappedMemory);
    AddSystemCallEntryFunction(ExitGranary);
    InitGDBDebug();
  }

  virtual void Exit(ExitReason) {
    RemoveAllHooks();
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
