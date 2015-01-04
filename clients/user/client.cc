/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#ifdef GRANARY_WHERE_user

#include "clients/user/client.h"
#include "clients/util/closure.h"

GRANARY_USING_NAMESPACE granary;

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

// Hooks that other clients can use for interposing on system calls.
static ClosureList<SystemCallContext> gEntryHooks GRANARY_GLOBAL;
static ClosureList<SystemCallContext> gExitHooks GRANARY_GLOBAL;

// Deletes all hooks and restores the syscall hooking system to its original
// state. This is done during `User::Exit`.
static void RemoveAllHooks(void) {
  gEntryHooks.Reset();
  gExitHooks.Reset();
}

// Trigger an exit when the program is killed.
static void ExitOnSignal(int) {
  exit(1);
}

// Try to handle a signal, assuming no handler is already present.
static void TryHandleSignal(int signum, void (*handler)(int)) {
  struct kernel_sigaction new_sigaction;
  rt_sigaction(signum, nullptr, &new_sigaction, _NSIG / 8);
  if (new_sigaction.k_sa_handler) return;
  memset(&new_sigaction, 0, sizeof new_sigaction);
  memset(&(new_sigaction.sa_mask), 0xFF, sizeof new_sigaction.sa_mask);
  new_sigaction.k_sa_handler = handler;
  GRANARY_IF_DEBUG( auto ret = ) rt_sigaction(signum, &new_sigaction, nullptr,
                                              _NSIG / 8);
  GRANARY_ASSERT(!ret);
}

}  // namespace

// Handle a system call entrypoint.
void HookSystemCallEntry(arch::MachineContext *mcontext) {
  SystemCallContext ctx(mcontext);
  gEntryHooks.ApplyAll(ctx);

  // Note: We apply these hooks *after* the `entry_hooks` so that client-added
  //       hooks can have visibility on all system calls before Granary mangles
  //       them.

  // Handle proper Granary exit procedures. Granary's `exit_group` function
  // deals with proper `Exit`ing of all tools.
  if (__NR_exit_group == ctx.Number()) {
    exit(static_cast<int>(ctx.Arg0()));

  // Exit a thread. This might trigger a full Granary exit if the program is
  // single-threaded.
  } else if (__NR_exit == ctx.Number()) {
    os::ExitThread();

  // Manipulate certain kinds of memory operations.
  } else if (__NR_munmap == ctx.Number()) {
    UnmapMemory(ctx);
  }
}

// Handle a system call exit.
void HookSystemCallExit(arch::MachineContext *context) {
  gExitHooks.ApplyAll(SystemCallContext(context));
}

// Register a function to be called before a system call is made.
void AddSystemCallEntryFunction(SystemCallHook *callback) {
  if (!FLAG_hook_syscalls) return;
  gEntryHooks.Add(callback);
}

// Register a function to be called after a system call is made.
void AddSystemCallExitFunction(SystemCallHook *callback) {
  if (!FLAG_hook_syscalls) return;
  gExitHooks.Add(callback);
}

// Tool that helps user-space instrumentation work.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;

  static void Init(InitReason reason) {
    if (kInitProgram == reason || kInitAttach == reason) {
      TryHandleSignal(SIGTERM, ExitOnSignal);
      TryHandleSignal(SIGINT, ExitOnSignal);
    }
  }

  static void Exit(ExitReason reason) {
    if (kExitThread == reason) return;
    if (FLAG_hook_syscalls) RemoveAllHooks();
  }

  virtual void InstrumentEntrypoint(BlockFactory *factory,
                                    CompensationBlock *entry_block,
                                    EntryPointKind kind, int) {
    if (kEntryPointUserAttach == kind && !FLAG_early_attach) {
      for (auto succ : entry_block->Successors()) {
        factory->RequestBlock(succ.block, kRequestBlockExecuteNatively);
      }
    }
  }

  virtual void InstrumentBlock(DecodedBlock *block) {
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
    syscall->InsertBefore(lir::ContextFunctionCall(HookSystemCallEntry));

    if (!gExitHooks.IsEmpty()) {
      syscall->InsertAfter(lir::ContextFunctionCall(HookSystemCallExit));
    }
  }
};

// Initialize the `user` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<UserSpaceInstrumenter>("user");
}

#endif  // GRANARY_WHERE_user
