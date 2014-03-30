/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

// Simple tool for tracing memory loads and stores.
class Watchpoints : public Tool {
 public:
  virtual ~Watchpoints(void) = default;

  void InstrumentMemOp(DecodedBasicBlock *bb, NativeInstruction *instr,
                       const MemoryOperand &mloc, int scope_id) {
    VirtualRegister addr;
    if (mloc.MatchRegister(addr) && !addr.IsStackPointer() &&
        !mloc.IsEffectiveAddress()) {

      // If the address register is read AND overwritten by the memory
      // instruction, which is the case for x86 string instructions (e.g.
      // `MOVS`, `STOS`, etc.), then we need to restore the watched bits after
      // the emulated instruction.
      RegisterOperand addr_reg(addr);
      auto updates_address_reg = instr->MatchOperands(
          ExactReadAndWriteTo(addr_reg));

      BeginInlineAssembly(bb, {&addr_reg}, scope_id);
      InlineBeforeIf(instr, updates_address_reg,
                     "MOV r64 %2, r64 %0;"_x86_64);  // Backup the value.
      InlineBefore(instr,
                   // Store bit 48 into the carry flag, and then jump to label
                   // `%1` if the CF indicates that the address in `%0` (i.e.
                   // addr_reg) isn't watched.
                   "BT r64 %0, i8 48;"
                   GRANARY_IF_USER_ELSE("JC", "JNC") " l %1;"
                   "SHL r64 %0, i8 16;"
                   "SAR r64 %0, i8 16;"_x86_64);
      // TODO(pag): Insert annotation for watchpoints here so that other tools
      //            can depend on `watchpoints` and then inject their code
      //            before/after the watchpoints-specific annotation.
      // TODO(pag): Need to be able to communicate properties of the memory
      //            location to other tools (e.g. size, read/write, etc.).
      InlineBefore(instr,
                   "LABEL %1:"_x86_64);
      InlineAfterIf(instr, updates_address_reg,
                    "BSWAP r64 %0;"
                    "BSWAP r64 %2;"
                    "MOV r16 %0, r16 %2;"
                    "BSWAP r64 %0;"_x86_64);
      EndInlineAssembly();
    }
  }

  // Instrument a basic block.
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    MemoryOperand mloc1;
    MemoryOperand mloc2;
    for (auto instr : bb->AppInstructions()) {
      auto num_matched = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                     ReadOrWriteTo(mloc2));
      if (2 == num_matched) {
        InstrumentMemOp(bb, instr, mloc1, 0);
        InstrumentMemOp(bb, instr, mloc2, 1);
      } else if (1 == num_matched) {
        InstrumentMemOp(bb, instr, mloc1, 0);
      }
    }
  }
};

// Initialize the `watchpoints` tool.
GRANARY_CLIENT_INIT({
  // TODO(pag): Add dependency on `x86_64` pseudo tool here as a way of
  //            constraining this tool to being dependent on x86.
  RegisterTool<Watchpoints>("watchpoints");
})
