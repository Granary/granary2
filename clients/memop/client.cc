/* Copyright 2015 Peter Goodman, all rights reserved. */

#include "clients/util/closure.h"
#include "clients/memop/client.h"  // Needs to go first.

GRANARY_USING_NAMESPACE granary;

namespace {

// Hooks that other tools can use for interposing on memory operands that will
// be instrumented for watchpoints.
static ClosureList<const InstrumentedMemoryOperand &>
    gMemOpHooks GRANARY_GLOBAL;

}  // namespace

// Abstract tool for instrumenting memory operands.
class MemOpTool : public InstrumentationTool {
 public:
  MemOpTool(void)
      : bb(nullptr),
        instr(nullptr),
        op_num(0) {}

  virtual ~MemOpTool(void) = default;

  static void Init(InitReason reason) {
    if (kInitProgram == reason || kInitAttach == reason) {
      virt_addr_reg[0] = AllocateVirtualRegister();
      virt_addr_reg[1] = AllocateVirtualRegister();
    }
  }

  static void Exit(ExitReason reason) {
    if (kExitDetach == reason) {
      gMemOpHooks.Reset();
    }
  }

  virtual void InstrumentBlock(DecodedBlock *bb_) override {
    MemoryOperand mloc1, mloc2;
    bb = bb_;
    for (auto instr_ : bb->AppInstructions()) {
      auto num_matched = instr_->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                      ReadOrWriteTo(mloc2));
      instr = instr_;
      op_num = 0;
      if (2 == num_matched) {
        InstrumentMemOp(mloc1);
        op_num = 1;
        InstrumentMemOp(mloc2);
      } else if (1 == num_matched) {
        InstrumentMemOp(mloc1);
      }
    }
  }

 private:

  // Dispatches to all hooks.
  static void InstrumentMemOp(const InstrumentedMemoryOperand &op) {
    gMemOpHooks.ApplyAll(op);
  }

  // Instrument a memory operation.
  void InstrumentMemOp(MemoryOperand &mloc) {
    if (mloc.IsEffectiveAddress()) return;  // Doesn't access memory.

    // Reads or writes from an absolute address, not through a register.
    VirtualRegister addr_reg, seg_reg;
    const void *addr_ptr(nullptr);

    if (mloc.MatchRegister(addr_reg)) {
      if (mloc.MatchSegmentRegister(seg_reg)) {
        InstrumentSegMemOp(mloc, addr_reg, seg_reg);
      } else {
        InstrumentRegMemOp(mloc, addr_reg);
      }
    } else if (mloc.MatchPointer(addr_ptr)) {
      InstrumentAddrMemOp(mloc, addr_ptr);

    } else if (mloc.IsCompound()) {
      InstrumentCompoundMemOp(mloc);
    }
  }

  // Instrument a memory operand that accesses some memory through a register.
  void InstrumentRegMemOp(MemoryOperand &mloc, VirtualRegister reg) {
    RegisterOperand addr_reg_op(reg);
    InstrumentedMemoryOperand op = {bb, instr, mloc, addr_reg_op, op_num};
    InstrumentMemOp(op);
  }

  // Instrument a memory operand that accesses some memory through an offset of
  // a segment register. We assume that the first quadword stored in the segment
  // points to the segment base address.
  void InstrumentSegMemOp(MemoryOperand &mloc, VirtualRegister seg_offs,
                          VirtualRegister seg_reg) {
    RegisterOperand offset_op(seg_offs);
    RegisterOperand addr_reg_op(virt_addr_reg[op_num]);
    RegisterOperand seg_reg_op(seg_reg);
    lir::InlineAssembly asm_(offset_op, addr_reg_op, seg_reg_op);
    asm_.InlineBefore(instr, "MOV r64 %1, m64 %2:[0];"
                             "LEA r64 %1, m64 [%1 + %0];"_x86_64);
    InstrumentedMemoryOperand op = {bb, instr, mloc, addr_reg_op, op_num};
    InstrumentMemOp(op);
  }

  // Instrument a memory operand that accesses some absolute memory address.
  void InstrumentAddrMemOp(MemoryOperand &mloc, const void *addr) {
    ImmediateOperand native_addr(addr);
    RegisterOperand addr_reg_op(virt_addr_reg[op_num]);
    lir::InlineAssembly asm_(native_addr, addr_reg_op);
    asm_.InlineBefore(instr, "MOV r64 %1, i64 %0;"_x86_64);
    InstrumentedMemoryOperand op = {bb, instr, mloc, addr_reg_op, op_num};
    InstrumentMemOp(op);
  }

  // Instrument a compound memory operation.
  void InstrumentCompoundMemOp(MemoryOperand &mloc) {
    auto addr_reg = virt_addr_reg[op_num];

    // Track stack pointer propagation.
    VirtualRegister base;
    if (mloc.CountMatchedRegisters(base) && base.IsStackPointerAlias()) {
      addr_reg.MarkAsStackPointerAlias();
    }

    RegisterOperand addr_reg_op(addr_reg);
    lir::InlineAssembly asm_(mloc, addr_reg_op);
    asm_.InlineBefore(instr, "LEA r64 %1, m64 %0;"_x86_64);
    InstrumentedMemoryOperand op = {bb, instr, mloc, addr_reg_op, op_num};
    InstrumentMemOp(op);
  }

  // Current block being instrumented.
  DecodedBlock *bb;

  // Current instruction being instrumented.
  NativeInstruction *instr;

  // Current memory operand being instrumented.
  size_t op_num;

  // Virtual registers used throughout.
  static VirtualRegister virt_addr_reg[2];
};

VirtualRegister MemOpTool::virt_addr_reg[2];

// Registers a function that can hook into the memory operands instrumenter.
void AddMemOpInstrumenter(void (*func)(const InstrumentedMemoryOperand &)) {
  gMemOpHooks.Add(func);
}

GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<MemOpTool>("memop");
}
