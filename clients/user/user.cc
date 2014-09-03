/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

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

// Tool that helps user-space instrumentation work.
class UserSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~UserSpaceInstrumenter(void) = default;

  void InstrumentSyscall(ControlFlowInstruction *syscall) {
    BeginInlineAssembly();

    // Prevents user-space code from replacing the `SIGSEGV` and `SIGILL`
    // signal handlers. This is to help in the debugging of user space
    // programs, where attaching GDB early on in the program's execution
    // causes the bug to disappear.
    //
    // Note: This type of behavior is very common, particularly because of the
    //       interaction between GDB's "hidden" breakpoints and Granary. GDB
    //       automatically inserts many breakpoints into programs (e.g. into
    //       various `pthread` functions). Granary is not aware of this, and so
    //       it only sees the `INT3` instructions, which it takes a signal to
    //       (locally) detach. However, in user space, the `transparent_returns`
    //       tool is enabled by default, and so the local detach behaves like a
    //       full thread detach. If the bug in question only happens after (in
    //       the thread's execution) one of the hidden breakpoints is hit, then
    //       the bug (caused by Granary) will likely never show up.
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

                          // Prevent overriding of `SIGSEGV = 11`.
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
      /*if (IsA<DecodedBasicBlock *>(block)) {
        auto decoded_pc = block->StartAppPC();
        auto module = ModuleContainingPC(decoded_pc);
        auto module_name = module->Name();
        auto offset = module->OffsetOfPC(decoded_pc);
        if (StringsMatch("libswscale_plugin", module_name) &&
            (6520 == offset.offset || 6522 == offset.offset)) {
          granary_curiosity();
        }
      }*/
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
