/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#ifdef GRANARY_WHERE_user
#ifndef GRANARY_TARGET_test
#ifndef GRANARY_WITH_VALGRIND

#include "clients/user/client.h"
#include "generated/clients/gdb/offsets.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_bool(debug_gdb_prompt, true,
    "Should a GDB process attacher helper be printed out on startup? Default "
    "is `yes`.",

    "gdb");

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
extern "C" void AwaitAttach(int signum, siginfo_t *siginfo, void *context) {
  char buff[1024];
  auto num_bytes = Format(
      buff, sizeof buff,
      "Process ID for attaching GDB: %d\nPress enter to continue.\n",
      getpid());
  write(1, buff, num_bytes);
  read(0, buff, 1);

  // Useful for debugging purposes.
  auto ucontext = reinterpret_cast<ucontext_t *>(context);
  GRANARY_USED(signum);
  GRANARY_USED(siginfo);
  GRANARY_USED(ucontext);
}

// Used to attach a signal handler to an arbitrary signal, such that when the
// signal is triggered, a message is printed to the screen that allows
// Granary to be attached to the process.
static void AwaitAttachOnSignal(int signum) {
  struct kernel_sigaction new_sigaction;
  memset(&new_sigaction, 0, sizeof new_sigaction);
  memset(&(new_sigaction.sa_mask), 0xFF, sizeof new_sigaction.sa_mask);
  new_sigaction.k_sa_handler = UnsafeCast<__sighandler_t>(&AwaitAttach);
  new_sigaction.sa_flags = SA_SIGINFO;
  GRANARY_IF_DEBUG( auto ret = ) rt_sigaction(signum, &new_sigaction, nullptr,
                                              _NSIG / 8);
  GRANARY_ASSERT(!ret);
}

// Prevents user-space code from replacing the `SIGSEGV` and `SIGILL`
// signal handlers. This is to help in the debugging of user space
// programs, where attaching GDB early on in the program's execution
// causes the bug to disappear.
static void SuppressSigAction(SystemCallContext ctx) {
  if (__NR_rt_sigaction != ctx.Number()) return;

  // If `act == NULL` then code is querying the current state of the signal
  // handler.
  if (!ctx.Arg1()) return;

  // Turn this `sigaction` into a no-op (that will likely return `-EINVAL`)
  auto signum = ctx.Arg0();
  if (SIGTRAP == signum || SIGBUS == signum || SIGSEGV == signum) {
    ctx.Arg0() = SIGUNUSED;
    ctx.Arg1() = 0;
    ctx.Arg2() = 0;
  }
}

}  // namespace

GRANARY_DECLARE_bool(debug_log_fragments);

// Tool that helps user-space instrumentation work.
class GDBDebuggerHelper : public InstrumentationTool {
 public:
  virtual ~GDBDebuggerHelper(void) = default;

  // Initialize Granary for debugging. This is geared toward GDB-based debugging,
  // where we can either attach GDB on program startup. Alternatively, if
  // attaching GDB somehow makes the bug being debugged disappear, then we
  // register a signal handler for `SEGFAULT`s that will prompt for GDB to be
  // attached.
  static void Init(InitReason reason) {
    if (kInitThread == reason) return;
    if (!FLAG_debug_gdb_prompt) AddSystemCallEntryFunction(SuppressSigAction);
  }

  // Returns true if the target of a native basic block is known to be an
  // internal GDB breakpoint location. Internal GDB breakpoints can be found
  // by doing `maint info breakpoints` in GDB and looking at negative-numbered
  // breakpoints.
  //
  // The specific `SYMBOL_OFFSET_*` macros are computed at compile time by the
  // `gdb` client's Makefile, and are placed in
  // `generated/clients/gdb/offsets.h`.
  //
  // TODO(pag): Need to handle the following as well:
  //    r_debug_state
  //    _r_debug_state
  //    rtld_db_dlactivity
  //    __dl_rtld_db_dlactivity
  //    _rtld_debug_state
  static bool IsInternalBreakpointLocation(DirectBlock *block) {
    auto decoded_pc = block->StartAppPC();
    auto module = os::ModuleContainingPC(decoded_pc);
    auto module_name = module->Name();
    auto offset = module->OffsetOfPC(decoded_pc);

    if (StringsMatch("ld", module_name)) {
      return SYMBOL_OFFSET_ld__dl_debug_state == offset.offset;
    } else if (StringsMatch("libpthread", module_name)) {
      return SYMBOL_OFFSET_libpthread___nptl_create_event == offset.offset ||
             SYMBOL_OFFSET_libpthread___nptl_death_event == offset.offset;
    } else {
      return false;
    }
  }

  // Fix an internal breakpoint by converting it into a function call then
  // return. This is a fun hack ;-)
  static void FixInternalBreakpoint(BlockFactory *factory,
                                    ControlFlowInstruction *cfi) {
    GRANARY_ASSERT(cfi->DecodedPC() && cfi->DecodedLength());
    cfi->InsertBefore(lir::Jump(factory, cfi->DecodedPC() +
                                         cfi->DecodedLength()));
    DecodedBlock::Unlink(cfi);
  }

  virtual void InstrumentControlFlow(BlockFactory *factory, Trace *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (succ.cfi->HasIndirectTarget()) continue;
        auto direct_block = DynamicCast<DirectBlock *>(succ.block);
        if (!direct_block) continue;
        if (!IsInternalBreakpointLocation(direct_block)) continue;

        FixInternalBreakpoint(factory, succ.cfi);
      }
    }
  }
};

// Initialize the `gdb` tool.
GRANARY_ON_CLIENT_INIT() {
  if (FLAG_debug_gdb_prompt) {
    AwaitAttach(-1, nullptr, nullptr);
  } else {
    AwaitAttachOnSignal(SIGSEGV);
    AwaitAttachOnSignal(SIGBUS);
    AwaitAttachOnSignal(SIGTRAP);
  }
  AddInstrumentationTool<GDBDebuggerHelper>("gdb");
}

#endif  // GRANARY_WITH_VALGRIND
#endif  // GRANARY_TARGET_test
#endif  // GRANARY_WHERE_user
