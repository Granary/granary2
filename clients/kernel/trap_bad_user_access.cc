/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifdef GRANARY_WHERE_kernel

#include <granary.h>

GRANARY_USING_NAMESPACE granary;

// Callback that is invoked when a user space address is accessed when it
// shouldn't be.
static void TrapOnBadUserAccess(void *mem, AppPC pc) {
  if (!mem) return;  // E.g. happens with the various prefetches.
  granary_curiosity();  // Traps into GDB.
  GRANARY_USED(mem);
  GRANARY_USED(pc);
}

// Traps when a user space address is accessed in kernel space when it
// shouldn't be.
class TrapBadUserAccess : public InstrumentationTool {
 public:
  virtual ~TrapBadUserAccess(void) = default;

  void InstrumentMemOp(DecodedBasicBlock *bb, NativeInstruction *instr,
                       const MemoryOperand &mloc) {

    // Exceptional control-flow instructions are allowed to access user data.
    // In fact, they are the *only* instructions allowed to do so.
    if (IsA<ExceptionalControlFlowInstruction *>(instr)) return;

    // Won't fault, even if given a bad address or a user space address,
    // therefore it's not considered as a potential source of faults due to
    // bad user memory accesses.
    if (StringsMatch("INVLPG", instr->OpCodeName())) return;
    if (StringsMatch("PREFETCHT0", instr->OpCodeName())) return;
    if (StringsMatch("PREFETCHT1", instr->OpCodeName())) return;
    if (StringsMatch("PREFETCHT2", instr->OpCodeName())) return;
    if (StringsMatch("PREFETCHNTA", instr->OpCodeName())) return;

    // Doesn't read from or write to memory.
    if (mloc.IsEffectiveAddress()) return;

    // Reads or writes from an absolute address, not through a register.
    VirtualRegister addr;
    if (!mloc.MatchRegister(addr)) return;

    // Ignore addresses stored in non-GPRs (e.g. accesses to the stack).
    if (!addr.IsGeneralPurpose()) return;
    if (addr.IsVirtualStackPointer()) return;
    if (addr.IsSegmentOffset()) return;

    RegisterOperand addr_reg(addr);
    lir::InlineAssembly asm_(addr_reg);

    asm_.InlineBefore(instr,
        // Test bit 47, which should be sign-extended to the value of all other
        // bits.
        "BT r64 %0, i8 47;"
        "JB l %1;"_x86_64);
    instr->InsertBefore(lir::InlineFunctionCall(bb, TrapOnBadUserAccess,
                                                addr_reg, instr->DecodedPC()));
    asm_.InlineBefore(instr,
        "LABEL %1:"_x86_64);
  }

  // Instrument a basic block.
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    MemoryOperand mloc1, mloc2;
    for (auto instr : bb->AppInstructions()) {
      auto num_matched = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                     ReadOrWriteTo(mloc2));
      if (2 == num_matched) {
        InstrumentMemOp(bb, instr, mloc1);
        InstrumentMemOp(bb, instr, mloc2);
      } else if (1 == num_matched) {
        InstrumentMemOp(bb, instr, mloc1);
      }
    }
  }
};

// Initialize the `trap_bad_user_access` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<TrapBadUserAccess>("trap_bad_user_access");
}

#endif   // GRANARY_WHERE_kernel
