/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#ifdef GRANARY_WHERE_user

#include "clients/user/signal.h"
#include "clients/user/syscall.h"
#include "clients/util/closure.h"

using namespace granary;

GRANARY_DEFINE_bool(early_attach, true,
    "Should Granary attach to the user program when Granary is first "
    "loaded? The default is `yes`.",

    "user");

GRANARY_DEFINE_bool(hook_syscalls, true,
    "Should Granary hook into a program's system calls? The default is `yes`.\n"
    "\n"
    "Note: Granary must hook system calls for comprehensive instrumentation\n"
    "      of user space programs to work. However, if a program isn't being\n"
    "      comprehensively instrumented, then system calls need not be\n"
    "      hooked.",

    "user");

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
namespace {

// Invalidates any code cache blocks related to an mmap request.
static void InvalidateUnmappedMemory(void *, SystemCallContext ctx) {
  if (__NR_munmap != ctx.Number()) return;
  auto addr = ctx.Arg0();
  auto len = ctx.Arg1();
  if (os::InvalidateModuleCode(reinterpret_cast<AppPC>(addr),
                               static_cast<int>(len))) {
    // Turn an `munmap` into a `munmap+mmap(MAP_FIXED)` pair, such that the
    // memory is relinquished, but the address space remains allocated.
    //
    // Note: There is a potential race between the `munmap` and `mmap`. If this
    //       becomes a problem then we can synchronize `mmap`, `munmap` and
    //       `mremap`.
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
static void ExitGranary(void *, SystemCallContext ctx) {
  if (__NR_exit_group == ctx.Number()) {
    exit_group(static_cast<int>(ctx.Arg0()));
  }
}

// Hooks that other clients can use for interposing on system calls.
static ClosureList<SystemCallContext> entry_hooks GRANARY_GLOBAL;
static ClosureList<SystemCallContext> exit_hooks GRANARY_GLOBAL;

// Deletes all hooks and restores the syscall hooking system to its original
// state. This is done during `User::Exit`.
static void RemoveAllHooks(void) {
  entry_hooks.Reset();
  exit_hooks.Reset();
}

}  // namespace

// Handle a system call entrypoint.
void HookSystemCallEntry(arch::MachineContext *context) {
  entry_hooks.ApplyAll(SystemCallContext(context));
}

// Handle a system call exit.
void HookSystemCallExit(arch::MachineContext *context) {
  exit_hooks.ApplyAll(SystemCallContext(context));
}

// Register a function to be called before a system call is made.
void AddSystemCallEntryFunction(SystemCallHook *callback,
                                void *data,
                                CleanUpData *delete_data) {
  if (!FLAG_hook_syscalls) return;
  entry_hooks.Add(callback, data, delete_data);
}

// Register a function to be called after a system call is made.
void AddSystemCallExitFunction(SystemCallHook *callback,
                               void *data,
                               CleanUpData *delete_data) {
  if (!FLAG_hook_syscalls) return;
  exit_hooks.Add(callback, data, delete_data);
}

// Tool that helps user-space instrumentation work.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;

  virtual void Init(InitReason) {
    AddSystemCallEntryFunction(InvalidateUnmappedMemory);
    AddSystemCallEntryFunction(ExitGranary);
  }

  virtual void Exit(ExitReason) {
    if (FLAG_hook_syscalls) {
      RemoveAllHooks();
    }
  }

  virtual void InstrumentEntrypoint(BlockFactory *factory,
                                    CompensationBasicBlock *entry_block,
                                    EntryPointKind kind, int) {
    if (ENTRYPOINT_USER_LOAD == kind && !FLAG_early_attach) {
      for (auto succ : entry_block->Successors()) {
        factory->RequestBlock(succ.block, REQUEST_NATIVE);
      }
    }
  }

  virtual void InstrumentBlock(DecodedBasicBlock *block) {
    if (!FLAG_hook_syscalls) return;
    for (auto succ : block->Successors()) {
      if (succ.cfi->IsSystemCall()) {
        InstrumentSyscall(succ.cfi);
      }
    }
  }

 protected:
  // Adds in the hooks that allow other tools (including this tool) to hook
  // the system call handlers in high-level way.
  void InstrumentSyscall(ControlFlowInstruction *syscall) {
    if (!entry_hooks.IsEmpty()) {
      syscall->InsertBefore(lir::CallWithContext(HookSystemCallEntry));
    }
    if (!exit_hooks.IsEmpty()) {
      syscall->InsertAfter(lir::CallWithContext(HookSystemCallExit));
    }
  }
};

// Initialize the `user` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<UserSpaceInstrumenter>("user");
})

#endif  // GRANARY_WHERE_user
