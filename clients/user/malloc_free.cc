/* Copyright 2014 Peter Goodman, all rights reserved. */
#if 0
#include "clients/util/types.h"

#include <granary.h>

#ifdef GRANARY_WHERE_user

#include "clients/user/syscall.h"
#include "clients/util/closure.h"

using namespace granary;

namespace {

// Invalidates any code cache blocks related to an `mmap` request.
static void UnmapMemory(SystemCallContext ctx) {
  auto addr = ctx.Arg0();
  auto len = ctx.Arg1();

  // Turn an `munmap` into an `mmap` and `mprotect` pair that first makes
  // the memory unusable, then hints to the OS that it no longer needs to be
  // backed.

  mmap(reinterpret_cast<void *>(addr), len, PROT_NONE,
       MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  ctx.Number() = __NR_mprotect;
  ctx.Arg2() = PROT_NONE;  // Should succeed.
}

// Use Granary's `exit_group` function to handle process exit. This will lead
// to all tools exiting.
static void ExitGranary(SystemCallContext ctx) {
  exit(static_cast<int>(ctx.Arg0()));
}

// Hooks that other clients can use for interposing on system calls.
static ClosureList<MallocContext> malloc_entry_hooks GRANARY_GLOBAL;
static ClosureList<SystemCallContext> malloc_exit_hooks GRANARY_GLOBAL;

// Deletes all hooks and restores the syscall hooking system to its original
// state. This is done during `User::Exit`.
static void RemoveAllHooks(void) {
  malloc_entry_hooks.Reset();
  malloc_exit_hooks.Reset();
}

}  // namespace

// Handle a system call entrypoint.
void HookSystemCallEntry(void *, arch::MachineContext *context) {
  SystemCallContext ctx(context);
  entry_hooks.ApplyAll(ctx);

  // Note: We apply these hooks *after* the `entry_hooks` so that client-added
  //       hooks can have visibility on all system calls before Granary mangles
  //       them.

  // Handle proper Granary exit procedures. Granary's `exit_group` function
  // deals with proper `Exit`ing of all tools.
  if (GRANARY_UNLIKELY(__NR_exit_group == ctx.Number())) {
    ExitGranary(ctx);

  // Manipulate certain kinds of memory operations.
  } else if (__NR_munmap == ctx.Number()) {
    UnmapMemory(ctx);
  }
}

// Handle a system call exit.
void HookSystemCallExit(void *, arch::MachineContext *context) {
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

// Tool that helps instrument `malloc` and `free` functions.
class MallocFreeInstrumenter : public InstrumentationTool {
 public:
  virtual ~MallocFreeInstrumenter(void) = default;

  virtual void Exit(ExitReason) {
    RemoveAllHooks();
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
    // Unconditionally pre-instrument syscalls so we can see `munmap`s and
    // `exit_group`s.
    syscall->InsertBefore(lir::CallWithContext(HookSystemCallEntry));

    if (!exit_hooks.IsEmpty()) {
      syscall->InsertAfter(lir::CallWithContext(HookSystemCallExit));
    }
  }
};

// Initialize the `malloc_free` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<MallocFreeInstrumenter>("malloc_free");
})

#endif  // GRANARY_WHERE_user
#endif
