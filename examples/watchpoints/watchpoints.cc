/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

// TODO(pag): Generic allocators (similar to with meta-data) but for allowing
//            multiple tools to register descriptor info.
// TODO(pag): Eventually handle user space syscalls to avoid EFAULTs.
// TODO(pag): Eventually handle user space signals.
// TODO(pag): Eventually handle kernel space bit waitqueues.
// TODO(pag): Eventually handle kernel space interrupts.
// TODO(pag): Eventually handle user space addresses being de-referenced in
//            kernel space.

// Implements the instrumentation needed to do address watchpoints. Address
// watchpoints work by tainting memory addresses, such that
//
// Address watchpoints is a mechanism that enables selective memory shadowing
// by tainting memory addresses. The 48th bit of an address distinguishes
// "watched" (i.e. tainted) addresses from "unwatched" addresses. The
// watchpoints instrumentation injects instructions to detect dereferences of
// tainted addresses and ensures that memory instructions don't raise faults
// when
class Watchpoints : public Tool {
 public:
  virtual ~Watchpoints(void) = default;

  void InstrumentMemOp(DecodedBasicBlock *bb, NativeInstruction *instr,
                       const LiveRegisterTracker &live_regs,
                       const MemoryOperand &mloc, int scope_id) {
    // Doesn't read from or write to memory.
    if (mloc.IsEffectiveAddress()) return;

    // Reads or writes from an absolute address, not through a register.
    VirtualRegister watched_addr;
    if (!mloc.MatchRegister(watched_addr)) return;

    // Ignore addresses stored in non-GPRs (e.g. accesses to the stack).
    if (!watched_addr.IsGeneralPurpose()) return;

    VirtualRegister unwatched_addr(bb->AllocateVirtualRegister());
    RegisterOperand unwatched_addr_reg(unwatched_addr);
    RegisterOperand watched_addr_reg(watched_addr);

    // It was already replaced by something else; modify the virtual register
    // in-place under the assumption that the original(s) are already saved.
    if (watched_addr.IsVirtual()) {
      BeginInlineAssembly({nullptr, &watched_addr_reg}, scope_id);

    // It's an explicit memory location, so we will change the memory operand
    // in place to use `%1`.
    } else if (mloc.IsModifiable()) {
      BeginInlineAssembly({&watched_addr_reg, &unwatched_addr_reg}, scope_id);
      InlineBefore(instr,
                   "MOV r64 %1, r64 %0;"_x86_64);  // Copy the watched addr.

    // It's an implicit memory location, so we need to change the register
    // being used by the instruction in place, while keeping a copy around
    // for later.
    } else {
      GRANARY_ASSERT(watched_addr.IsNative());
      BeginInlineAssembly({&unwatched_addr_reg, &watched_addr_reg}, scope_id);
      InlineBefore(instr,
                   "MOV r64 %0, r64 %1;"_x86_64);  // Copy the watched addr.
    }
    InlineBefore(instr,
                 "BT r64 %1, i8 48;"  // Test the discriminating bit (bit 48).
                 GRANARY_IF_USER_ELSE("JB", "JNB") " l %2;"
                 "  SHL r64 %1, i8 16;"
                 "  SAR r64 %1, i8 16;"
                 "  "  // %1 now contains unwatched address.
                 "LABEL %2:"_x86_64);

    // Nothing to do in this case, just mirror the structure above.
    if (watched_addr.IsVirtual()) {

    // Replace the original memory operand.
    } else if (mloc.IsModifiable()) {
      MemoryOperand unwatched_addr_mloc(unwatched_addr, mloc.ByteWidth());
      mloc.Ref().ReplaceWith(unwatched_addr_mloc);

    // Restore the original only if it's an implicit register (and so we
    // modified the register in place instead of modifying a copy), and if
    // the register itself is not killed by the instruction, and not dead
    // after the instruction.
    } else if (!instr->MatchOperands(ExactWriteOnlyTo(watched_addr_reg)) &&
               !live_regs.IsDead(watched_addr)) {
      InlineAfter(instr,
                  "BSWAP r64 %0;"
                  "BSWAP r64 %1;"
                  "MOV r16 %1, r16 %0;"
                  "BSWAP r64 %1;"_x86_64);
    }

    EndInlineAssembly();
  }

  // Instrument a basic block.
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    MemoryOperand mloc1, mloc2;
    LiveRegisterTracker live_regs;
    live_regs.ReviveAll();

    for (auto instr : bb->ReversedAppInstructions()) {
      auto num_matched = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                     ReadOrWriteTo(mloc2));
      if (2 == num_matched) {
        InstrumentMemOp(bb, instr, live_regs, mloc1, 0);
        InstrumentMemOp(bb, instr, live_regs, mloc2, 1);
      } else if (1 == num_matched) {
        InstrumentMemOp(bb, instr, live_regs, mloc1, 0);
      }
      live_regs.Visit(instr);
    }
  }
};

// Initialize the `watchpoints` tool.
GRANARY_CLIENT_INIT({
  // TODO(pag): Add dependency on `x86-64` pseudo tool here as a way of
  //            constraining this tool to being dependent on x86.
  RegisterTool<Watchpoints>("watchpoints");
})
