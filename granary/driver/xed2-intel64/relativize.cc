/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/new.h"

#include "granary/cfg/instruction.h"

#include "granary/driver/xed2-intel64/builder.h"
#include "granary/driver/xed2-intel64/relativize.h"

namespace granary {
namespace driver {

// Make a native instruction safe to execute from within the code cache.
// This sometimes results in additional instructions being
void InstructionRelativizer::Relativize(NativeInstruction *native_instr_) {
  native_instr = native_instr_;
  instr = native_instr->instruction.get();
  if (instr->has_pc_rel_op) {
    switch (instr->iclass) {
      case XED_ICLASS_LEA: return RelativizeLEA();
      case XED_ICLASS_PUSH: return RelativizePUSH();
      case XED_ICLASS_POP: return RelativizePOP();
      case XED_ICLASS_JMP:
      case XED_ICLASS_JMP_FAR:
      case XED_ICLASS_CALL_FAR:
      case XED_ICLASS_CALL_NEAR: return RelativizeCFI();
      default:
        if (instr->has_memory_op) {
          RelativizeMemOP();
        }
    }
  }
}

namespace {
enum {
  _3_75_GB = 4026531840L
};

// Returns true if an address needs relativizing.
static bool AddressNeedsRelativizing(PC relative_pc, PC cache_pc) {
  auto signed_diff = relative_pc - cache_pc;
  auto diff = 0 > signed_diff ? -signed_diff : signed_diff;
  return _3_75_GB < diff;
}
}  // namespace

#define INSERT_BEFORE(...) \
  do { \
    auto ir = new Instruction; \
    __VA_ARGS__ ; \
    auto ir_instr = new NativeInstruction(ir); \
    native_instr->InsertBefore( \
        std::move(std::unique_ptr<granary::Instruction>(ir_instr))); \
  } while (0)

// Convert a RIP-relative LEA into a MOV <imm64>, reg.
void InstructionRelativizer::RelativizeLEA(void) {
  auto rel_pc = instr->ops[1].rel.pc;  // LEA_GPRv_AGEN
  if (AddressNeedsRelativizing(rel_pc, cache_pc)) {
    auto rel_ptr = reinterpret_cast<uintptr_t>(rel_pc);
    auto dest_reg = instr->ops[0].u.reg;
    MOV_GPRv_IMMv(instr, Register(dest_reg), Immediate(rel_ptr));
  }
}

// Relativize a `PUSH X(%RIP)` instruction if the native instruction pointer
// is too far away from the code cache.
void InstructionRelativizer::RelativizePUSH(void) {
  auto rel_pc = instr->ops[0].rel.pc;  // PUSH_MEMv
  if (AddressNeedsRelativizing(rel_pc, cache_pc)) {
    INSERT_BEFORE(LEA_GPRv_AGEN(ir, REG_RSP, REG_RSP[-8]));
    INSERT_BEFORE(PUSH_GPRv_50(ir, REG_RAX));
    INSERT_BEFORE(MOV_GPRv_IMMv(ir, REG_RAX, Immediate(rel_pc)));
    INSERT_BEFORE(MOV_GPRv_MEMv(ir, REG_RAX, *REG_RAX));
    INSERT_BEFORE(MOV_MEMv_GPRv(ir, REG_RSP[8], REG_RAX));
    POP_GPRv_51(instr, REG_RAX);
  }
}

// Relativize a `POP X(%RIP)` instruction if the native instruction pointer
// is too far away from the code cache.
void InstructionRelativizer::RelativizePOP(void) {
  auto rel_pc = instr->ops[0].rel.pc;  // POP_MEMv
  if (AddressNeedsRelativizing(rel_pc, cache_pc)) {
    INSERT_BEFORE(PUSH_GPRv_50(ir, REG_RAX));
    INSERT_BEFORE(PUSH_GPRv_50(ir, REG_RBX));
    INSERT_BEFORE(MOV_GPRv_IMMv(ir, REG_RAX, Immediate(rel_pc)));
    INSERT_BEFORE(MOV_GPRv_MEMv(ir, REG_RBX, REG_RSP[16]));
    INSERT_BEFORE(MOV_MEMv_GPRv(ir, *REG_RAX, REG_RBX));
    INSERT_BEFORE(POP_GPRv_51(ir, REG_RBX));
    INSERT_BEFORE(POP_GPRv_51(ir, REG_RAX));
    LEA_GPRv_AGEN(instr, REG_RSP, REG_RSP[8]);
  }
}

namespace {
// Represents a far-away program counter.
struct FarPC {
  PC pc;
  GRANARY_DEFINE_NEW_ALLOCATOR(FarPC, {
    SHARED = true,
    ALIGNMENT = 8
  })
};
}  // namespace

// Convert a RIP-relative CALL/JMP to some far-off location.
void InstructionRelativizer::RelativizeCFI(void) {
  Operand &op(instr->ops[0]);
  auto rel_pc = op.rel.pc;
  if (AddressNeedsRelativizing(rel_pc, cache_pc)) {
    if (XED_ENCODER_OPERAND_TYPE_PTR == op.type) {
      // TODO(pag): Use virtual reg.

    } else if (XED_ENCODER_OPERAND_TYPE_BRDISP) {
      granary_break_on_fault_if(instr->IsConditionalJump());
      op.type = XED_ENCODER_OPERAND_TYPE_PTR;
      op.rel.imm = reinterpret_cast<intptr_t>(new FarPC{rel_pc});
    } else {
      granary_break_on_fault();
    }
  }
}

// Relativize a memory operation with a RIP-relative memory operand.
void InstructionRelativizer::RelativizeMemOP(void) {
  auto op_num = 0;
  for (Operand &op : instr->ops) {
    if (XED_ENCODER_OPERAND_TYPE_PTR == op.type) {
      break;
    }
    ++op_num;
  }

  Operand &op(instr->ops[op_num]);
  if (xed_operand_action_written(op.rw)) {
    // TODO(pag): Use virtual reg.
  } else if (xed_operand_action_read(op.rw)) {
    // TODO(pag): Use virtual reg.
  } else {
    granary_break_on_fault();  // TODO(pag): Implement this. CMPXCHG?
  }
}

}  // namespace driver
}  // namespace granary
