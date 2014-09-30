/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifdef GRANARY_WHERE_user

#include <granary.h>

#include "clients/user/signal.h"
#include "clients/user/syscall.h"

using namespace granary;

GRANARY_DEFINE_bool(debug_gdb_prompt, true,
    "Should a GDB process attacher helper be printed out on startup? Default "
    "is `yes`.",

    "gdb");

extern "C" {
extern int getpid(void);
extern long long write(int __fd, const void *__buf, size_t __n);
extern long long read(int __fd, void *__buf, size_t __nbytes);

}  // extern C
namespace {

// Initialize Granary for debugging by GDB. For example, if one is doing:
//
//    grr --tools=foo -- ls
//
// Then in another terminal, one can do:
//
//    sudo gdb /bin/ls
//    (gdb) a <pid that is printed out>
//    (gdb) c
//
// Then press the ENTER key in the origin terminal (where `grr ... ls` is) to
// continue execution under GDB's supervision.
extern "C" void AwaitAttach(int signum, void *siginfo, void *context) {
  char buff[1024];
  auto num_bytes = Format(
      buff, sizeof buff,
      "Process ID for attaching GDB: %d\nPress enter to continue.\n",
      getpid());
  write(1, buff, num_bytes);
  read(0, buff, 1);

  // Useful for debugging purposes.
  GRANARY_USED(signum);
  GRANARY_USED(siginfo);  // `siginfo_t *`.
  GRANARY_USED(context);  // `ucontext *` on Linux.
}

// Used to attach a signal handler to an arbitrary signal, such that when the
// signal is triggered, a message is printed to the screen that allows
// Granary to be attached to the process.
static void AwaitAttachOnSignal(int signum) {
  struct sigaction new_sigaction;
  memset(&new_sigaction, 0, sizeof new_sigaction);
  memset(&(new_sigaction.sa_mask), 0xFF, sizeof new_sigaction.sa_mask);
  new_sigaction.sa_sigaction = &AwaitAttach;
  new_sigaction.sa_flags = SA_SIGINFO;
  rt_sigaction(signum, &new_sigaction, nullptr, _NSIG / 8);
}

// Prevents user-space code from replacing the `SIGSEGV` and `SIGILL`
// signal handlers. This is to help in the debugging of user space
// programs, where attaching GDB early on in the program's execution
// causes the bug to disappear.
static void SuppressSigAction(void *, SystemCallContext ctx) {
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

}  // namespace

// Tool that helps user-space instrumentation work.
class GDBDebuggerHelper : public InstrumentationTool {
 public:
  virtual ~GDBDebuggerHelper(void) = default;

  // Initialize Granary for debugging. This is geared toward GDB-based debugging,
  // where we can either attach GDB on program startup. Alternatively, if
  // attaching GDB somehow makes the bug being debugged disappear, then we
  // register a signal handler for `SEGFAULT`s that will prompt for GDB to be
  // attached.
  virtual void Init(InitReason) {
    if (!FLAG_debug_gdb_prompt) AddSystemCallEntryFunction(SuppressSigAction);
  }

  // GDB inserts hidden breakpoints into programs, especially in programs
  // using `pthreads`. When Granary comes across these breakpoints, it most
  // likely will detach, which, when combined with the `transparent_returns`
  // tool, results in full thread detaches. Here we try to handle these special
  // cases in a completely non-portable way. The comments, however, give
  // some guidance as to how to port this.
  bool FixHiddenBreakpoints(BlockFactory *factory, ControlFlowInstruction *cfi,
                            BasicBlock *block) {
    auto decoded_pc = block->StartAppPC();
    auto module = os::ModuleContainingPC(decoded_pc);
    auto module_name = module->Name();
    auto offset = module->OffsetOfPC(decoded_pc);
    auto call_native = false;

    if (StringsMatch("ld", module_name)) {
      // `__GI__dl_debug_state` (or just `_dl_debug_state`), which is just a
      // simple return.
      call_native = 0x10970 == offset.offset;
    } else if (StringsMatch("libpthread", module_name)) {
      // `__GI___nptl_create_event` and `__GI___nptl_death_event`.
      call_native = 0x6f50 == offset.offset || 0x6f60 == offset.offset;
    }

    // GDB somtimes puts `int3`s on specific functions so that it knows when
    // key events (e.g. thread creation) happen. Most of these functions are
    // basically no-ops, so would can just manually call them recursively).
    if (call_native) {
      cfi->InsertBefore(lir::Call(factory, decoded_pc, REQUEST_NATIVE));
      cfi->InsertBefore(lir::Return(factory));

      Instruction::Unlink(cfi);
      return true;
    }

    os::Log(os::LogOutput, "code = %p\nmodule = %s\noffset = %lx\n\n",
            decoded_pc, module_name, offset.offset);
    return false;
  }

  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (succ.cfi->HasIndirectTarget()) continue;
        if (!IsA<NativeBasicBlock *>(succ.block)) continue;
        FixHiddenBreakpoints(factory, succ.cfi, succ.block);
        break;
      }
    }
  }
};

// Initialize the `gdb` tool.
GRANARY_CLIENT_INIT({
  if (FLAG_debug_gdb_prompt) {
    AwaitAttach(-1, nullptr, nullptr);
  } else {
    AwaitAttachOnSignal(SIGSEGV);
    AwaitAttachOnSignal(SIGILL);
    AwaitAttachOnSignal(SIGBUS);
    AwaitAttachOnSignal(SIGTRAP);
  }
  RegisterInstrumentationTool<GDBDebuggerHelper>("gdb");
})

#endif  // GRANARY_WHERE_user
