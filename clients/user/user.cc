/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#include "clients/user/syscall.h"

using namespace granary;

GRANARY_DEFINE_bool(debug_fix_hidden_breakpoints, false,
    "Should Granary try to \"fix\" hidden GDB breakpoints? Sometimes GDB "
    "inserts hidden breakpoints at key program locations in order to interpose "
    "on events like thread creation, etc. However, when Granary sees these "
    "breakpoints, it only sees a trap instruction that otherwise clobbers one "
    "or more bytes of a pre-existing instruction. This option tries to emulate "
    "the clobbered instructions by special-casing them. Unfortunately, this is "
    "highly non-portable, therefore the default value is `no`.\n"
    "\n"
    "Note: In practice, this option is only relevant for people debugging\n"
    "      Granary using GDB. Therefore, they should manually fix the\n"
    "      individual special cases for their system before using this option.",

    "user");

// Defined in `clients/user/syscall.cc`, but not exported to other clients.
extern void HookSystemCallEntry(arch::MachineContext *);
extern void HookSystemCallExit(arch::MachineContext *);

namespace {

#define __NR_rt_sigaction  13
#define SIGSEGV         11
#define SIGILL          4
#define SIGUNUSED       31

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

}  // namespace

// Tool that helps user-space instrumentation work.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;

  virtual void Init(void) {
    AddSystemCallEntryFunction(SuppressSigAction);
  }

  // Adds in the hooks that allow other tools (including this tool) to hook
  // the system call handlers in high-level way.
  void InstrumentSyscall(ControlFlowInstruction *syscall) {
    syscall->InsertBefore(lir::CallWithContext(HookSystemCallEntry));
    syscall->InsertAfter(lir::CallWithContext(HookSystemCallExit));
  }

  // GDB inserts hidden breakpoints into programs, especially in programs
  // using `pthreads`. When Granary comes across these breakpoints, it most
  // likely will detach, which, when combined with the `transparent_returns`
  // tool, results in full thread detaches. Here we try to handle these special
  // cases in a completely non-portable way. The comments, however, give
  // some guidance as to how to port this.
  bool FixHiddenBreakpoints(BlockFactory *factory, ControlFlowInstruction *cfi,
                            BasicBlock *block) {
    auto fixed = false;
    auto decoded_pc = block->StartAppPC();
    auto module = ModuleContainingPC(decoded_pc);
    auto module_name = module->Name();
    auto offset = module->OffsetOfPC(decoded_pc);

    const char *append_asm(nullptr);
    std::unique_ptr<Instruction> append_instr(nullptr);

    if (StringsMatch("dl", module_name)) {
      if (0x10970 == offset.offset) {
        // Emulate `__GI__dl_debug_state` (or just `_dl_debug_state`), which is
        // a function that only does `RET` or `REPZ RET`, and exists solely to
        // be hooked by GDB.
        append_instr = lir::Return(factory);
      }
    } else if (StringsMatch("ld", module_name)) {
      if (0x100fd == offset.offset) {
        // Emulate `call_init+93`, then jump to `call_init+100`.
        append_asm = "MOV r64 RDX, m64 [RBX+0x108];";
        append_instr = lir::Jump(factory, decoded_pc + 7);

      } else if (0x10970 == offset.offset) {
        // Another case of `__GI__dl_debug_state`.
        append_instr = lir::Return(factory);
      }
    } else if (StringsMatch("libpthread", module_name)) {
      if (0x6f50 == offset.offset) {
        // `__GI___nptl_create_event`, similar to `_dl_debug_state`.
        append_instr = lir::Return(factory);
      }
    }

    if (append_asm) {
      BeginInlineAssembly();
      InlineBefore(cfi, append_asm);
      EndInlineAssembly();
      fixed = true;
    }

    if (append_instr.get()) {
      cfi->InsertBefore(std::move(append_instr));
      fixed = true;
    }

    if (fixed) {
      Instruction::Unlink(cfi);
      return true;
    }

    os::Log(os::LogOutput, "code = %p\n", decoded_pc);
    os::Log(os::LogOutput, "module = %s\n", module_name);
    os::Log(os::LogOutput, "offset = %lx\n\n", offset.offset);

    granary_curiosity();
    return false;
  }

  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    if (!FLAG_debug_fix_hidden_breakpoints) return;
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (succ.cfi->HasIndirectTarget()) continue;
        if (!IsA<NativeBasicBlock *>(succ.block)) continue;
        FixHiddenBreakpoints(factory, succ.cfi, succ.block);
        break;
      }
    }
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
