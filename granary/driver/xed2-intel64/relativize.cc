/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

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
      default: return;
    }
  }
  GRANARY_UNUSED(cache_pc);
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

// Convert a RIP-relative CALL/JMP to some far-off location into
void InstructionRelativizer::RelativizeCFI(void) {
  auto rel_pc = instr->ops[0].rel.pc;
  if (AddressNeedsRelativizing(rel_pc, cache_pc)) {

  }
}

}  // namespace driver
}  // namespace granary
