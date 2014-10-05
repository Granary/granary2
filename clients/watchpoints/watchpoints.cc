/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#include "clients/util/closure.h"
#include "clients/watchpoints/watchpoints.h"

using namespace granary;

namespace {

// Hooks that other tools can use for interposing on memory operands that will
// be instrumented for watchpoints.
static ClosureList<WatchedOperand *> watchpoint_hooks GRANARY_GLOBAL;

}  // namespace

// TODO(pag): Generic allocators (similar to with meta-data) but for allowing
//            multiple tools to register descriptor info.
// TODO(pag): Eventually handle user space syscalls to avoid EFAULTs.
// TODO(pag): Eventually handle user space signals.
// TODO(pag): Eventually handle kernel space bit waitqueues.
// TODO(pag): Eventually handle kernel space interrupts.
// TODO(pag): Eventually handle user space addresses being de-referenced in
//            kernel space.

// Implements the instrumentation needed to do address watchpoints.
//
// Address watchpoints is a mechanism that enables selective memory shadowing
// by tainting memory addresses. The 48th bit of an address distinguishes
// "watched" (i.e. tainted) addresses from "unwatched" addresses. The
// watchpoints instrumentation injects instructions to detect dereferences of
// tainted addresses and ensures that memory instructions don't raise faults
// when they are accessed.
class Watchpoints : public InstrumentationTool {
 public:
  virtual ~Watchpoints(void) = default;

  void InstrumentMemOp(DecodedBasicBlock *bb, NativeInstruction *instr,
                       const MemoryOperand &mloc) {
    // Doesn't read from or write to memory.
    if (mloc.IsEffectiveAddress()) return;

    // Reads or writes from an absolute address, not through a register.
    VirtualRegister watched_addr;
    if (!mloc.MatchRegister(watched_addr)) return;

    // Ignore addresses stored in non-GPRs (e.g. accesses to the stack).
    if (!watched_addr.IsGeneralPurpose()) return;
    if (watched_addr.IsVirtualStackPointer()) return;
    if (watched_addr.IsSegmentOffset()) return;

    VirtualRegister unwatched_addr(bb->AllocateVirtualRegister());
    RegisterOperand unwatched_addr_reg(unwatched_addr);
    RegisterOperand watched_addr_reg(watched_addr);
    WatchedOperand client_op(bb, instr, mloc, unwatched_addr_reg,
                             watched_addr_reg);

    lir::InlineAssembly asm_({&unwatched_addr_reg, &watched_addr_reg});

    asm_.InlineBefore(instr,
        "MOV r64 %0, r64 %1;"  // Copy the original (%1).
        "BT r64 %0, i8 48;"  // Test the discriminating bit (bit 48).
        GRANARY_IF_USER_ELSE("JNB", "JB") " l %2;"
        "  INT3;"
        "  SHL r64 %0, i8 16;"
        "  SAR r64 %0, i8 16;"_x86_64);

    // Allow all hooked tools to see the watched (%1) and unwatched (%0)
    // address.
    watchpoint_hooks.ApplyAll(&client_op);

    asm_.InlineBefore(instr,
        "LABEL %2:"_x86_64);

    // If it's an implicit memory location then we need to change the register
    // being used by the instruction in place, while keeping a copy around
    // for later.
    asm_.InlineBeforeIf(instr, !mloc.IsModifiable(),
        "XCHG r64 %0, r64 %1;"_x86_64);

    // Replace the original memory operand with the unwatched address.
    if (mloc.IsModifiable()) {
      MemoryOperand unwatched_addr_mloc(unwatched_addr, mloc.ByteWidth());
      mloc.Ref().ReplaceWith(unwatched_addr_mloc);

    // Restore the tainted bits if the memory operand was implicit, and if the
    // watched address was not overwritten by the instruction.
    } else if (!instr->MatchOperands(ExactWriteOnlyTo(watched_addr_reg))) {
      GRANARY_ASSERT(watched_addr.IsNative());
      asm_.InlineAfter(instr,
          "BSWAP r64 %1;"  // Swap bytes in unwatched address.
          "BSWAP r64 %0;"  // Swap bytes in watched address.
          "MOV r16 %1, r16 %0;"
          "BSWAP r64 %1;"_x86_64);
    }
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

// Initialize the `watchpoints` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<Watchpoints>("watchpoints");
})
