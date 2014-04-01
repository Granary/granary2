/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/arch/decode.h"
#include "granary/arch/x86-64/instruction.h"
#include "granary/arch/x86-64/mangle/early.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Decoder state that sets the mode to 64-bit.
extern xed_state_t XED_STATE;

// Initialize the instruction decoder.
InstructionDecoder::InstructionDecoder(void) {}

// Decode an instruction, and update the program counter by reference to point
// to the next logical instruction. Returns `true` iff the instruction was
// successfully decoded.
bool InstructionDecoder::DecodeNext(DecodedBasicBlock *block,
                                    Instruction *instr, AppPC *pc) {
  *pc = DecodeInternal(block, instr, *pc);
  return nullptr != *pc;
}

// Decode an x86 instruction into an instruction IR.
bool InstructionDecoder::Decode(DecodedBasicBlock *block,
                                Instruction *instr, AppPC pc) {
  return nullptr != DecodeInternal(block, instr, pc);
}

namespace {

// Returns true if an instruction might cross a page boundary.
static bool InstructionMightCrossPageBoundary(PC pc) {
  auto pc_ptr = reinterpret_cast<uintptr_t>(pc);
  auto max_pc_ptr = pc_ptr + XED_MAX_INSTRUCTION_BYTES;
  return (pc_ptr / GRANARY_ARCH_PAGE_FRAME_SIZE) !=
         (max_pc_ptr / GRANARY_ARCH_PAGE_FRAME_SIZE);
}

// Try decoding and instruction without reading in `XED_MAX_INSTRUCTION_BYTES`
// byte (i.e. try decoding as a 1-byte instruction, then as a 2-byte, etc.).
static xed_error_enum_t TryDecodeBytes(xed_decoded_inst_t *xedd, PC pc) {
  auto decode_status = XED_ERROR_LAST;
  for (auto i = 1U; i <= XED_MAX_INSTRUCTION_BYTES; ++i) {
    decode_status = xed_decode(xedd, pc, XED_MAX_INSTRUCTION_BYTES);
    if (XED_ERROR_NONE == decode_status) {
      break;
    }
  }
  return decode_status;
}

// Decode some bytes into a `xed_decoded_inst_t` instruction.
static xed_error_enum_t DecodeBytes(xed_decoded_inst_t *xedd, PC pc) {
  xed_decoded_inst_zero_set_mode(xedd, &XED_STATE);
  xed_decoded_inst_set_input_chip(xedd, XED_CHIP_INVALID);
  if (GRANARY_UNLIKELY(InstructionMightCrossPageBoundary(pc))) {
    return TryDecodeBytes(xedd, pc);
  } else {
    return xed_decode(xedd, pc, XED_MAX_INSTRUCTION_BYTES);
  }
}

// Pull out a register operand from the XED instruction.
static void ConvertRegisterOperand(Instruction *instr, Operand *instr_op,
                                   const xed_decoded_inst_t *xedd,
                                   xed_operand_enum_t op_name) {
  auto reg = xed_decoded_inst_get_reg(xedd, op_name);
  instr_op->type = XED_ENCODER_OPERAND_TYPE_REG;
  instr_op->reg.DecodeFromNative(reg);
  instr_op->width = static_cast<int8_t>(xed_get_register_width_bits64(reg));

  // Update the stack pointer tracking.
  if (GRANARY_UNLIKELY(instr_op->reg.IsStackPointer())) {
    if (instr_op->IsRead()) {
      instr->reads_from_stack_pointer = true;
    }
    if (instr_op->IsWrite()) {
      instr->writes_to_stack_pointer = true;
    }
  }
}

static PC NextDecodedAddress(const Instruction *instr) {
  return instr->decoded_pc + instr->decoded_length;
}

// Get a PC-relative address.
static PC GetPCRelativeBranchTarget(const Instruction *instr,
                                    const xed_decoded_inst_t *xedd) {
  auto disp = xed_decoded_inst_get_branch_displacement(xedd);
  return NextDecodedAddress(instr) + disp;
}

// Get a PC-relative memory address.
static const void *GetPCRelativeMemoryAddress(const Instruction *instr,
                                              const xed_decoded_inst_t *xedd,
                                              unsigned index) {
  auto disp = xed_decoded_inst_get_memory_displacement(xedd, index);
  return reinterpret_cast<const void *>(NextDecodedAddress(instr) + disp);
}

// Pull out a PC-relative branch target from the XED instruction.
static void ConvertRelativeBranch(Instruction *instr, Operand *instr_op,
                                  const xed_decoded_inst_t *xedd) {
  instr_op->type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  instr_op->width = arch::ADDRESS_WIDTH_BITS;
  instr_op->branch_target.as_pc = GetPCRelativeBranchTarget(instr, xedd);
}

// Returns true if a register is the instruction pointer.
static bool RegIsInstructionPointer(xed_reg_enum_t reg) {
  return XED_REG_RIP == reg || XED_REG_EIP == reg || XED_REG_IP == reg;
}

// Convert a memory operand into an `Operand`.
static void ConvertMemoryOperand(Instruction *instr, Operand *instr_op,
                                 const xed_decoded_inst_t *xedd,
                                 unsigned index) {
  auto is_sticky = instr->has_prefix_rep || instr->has_prefix_repne ||
                   XED_ICLASS_XLAT == instr->iclass;
  auto disp = xed_decoded_inst_get_memory_displacement(xedd, index);
  auto scale = xed_decoded_inst_get_scale(xedd, index);
  auto segment_reg = xed_decoded_inst_get_seg_reg(xedd, index);
  auto base_reg = xed_decoded_inst_get_base_reg(xedd, index);
  auto index_reg = xed_decoded_inst_get_index_reg(xedd, index);

  if (XED_REG_INVALID == segment_reg &&
      ((XED_REG_INVALID == index_reg && !disp) ||
       (XED_REG_INVALID == base_reg && 1 == disp))) {
    instr_op->reg.DecodeFromNative(static_cast<int>(base_reg));
  } else {
    instr_op->mem.disp = static_cast<int32_t>(disp);
    instr_op->mem.reg_base = base_reg;
    instr_op->mem.reg_index = index_reg;
    instr_op->mem.reg_seg = segment_reg;
    instr_op->mem.scale = static_cast<uint8_t>(scale);
    instr_op->is_compound = true;
  }

  instr_op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  instr_op->width = static_cast<int8_t>(xed3_operand_get_mem_width(xedd) * 8);
  instr_op->is_sticky = instr_op->is_sticky || is_sticky;
  instr_op->is_effective_address = XED_ICLASS_LEA == instr->iclass;
}

// Pull out an effective address from a LEA_GPRv_AGEN instruction. We actually
// treat the effective address as either an immediate or as a base/disp, unlike
// the expected XED_OPERAND_AGEN, and at encoding time convert back to an AGEN.
//
// Note: XED_OPERAND_AGEN's memory operand index is 0. See docs for function
//       `xed_agen`.
static void ConvertBaseDisp(Instruction *instr, Operand *instr_op,
                            const xed_decoded_inst_t *xedd, unsigned index) {
  if (RegIsInstructionPointer(xed_decoded_inst_get_base_reg(xedd, index))) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_PTR;  // Overloaded meaning.
    instr_op->addr.as_ptr = GetPCRelativeMemoryAddress(instr, xedd, index);
    instr_op->width = static_cast<int8_t>(
        xed3_operand_get_mem_width(xedd) * 8); // Width of addressed memory.
  } else {
    ConvertMemoryOperand(instr, instr_op, xedd, index);
  }
}

// Pull out an immediate operand from the XED instruction.
static void ConvertImmediateOperand(Operand *instr_op,
                                    const xed_decoded_inst_t *xedd,
                                    xed_operand_enum_t op_name) {
  if (XED_OPERAND_IMM0SIGNED == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_SIMM0;
    instr_op->imm.as_int = static_cast<intptr_t>(
        xed_decoded_inst_get_signed_immediate(xedd));

  } else if (XED_OPERAND_IMM0 == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_IMM0;
    instr_op->imm.as_uint = xed_decoded_inst_get_unsigned_immediate(xedd);

  } else if (XED_OPERAND_IMM1 == op_name ||
             XED_OPERAND_IMM1_BYTES == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_IMM1;
    instr_op->imm.as_uint = static_cast<uintptr_t>(
        xed_decoded_inst_get_second_immediate(xedd));
  }
  instr_op->width = static_cast<int8_t>(
      xed_decoded_inst_get_immediate_width_bits(xedd));
}

// Convert a `xed_operand_t` into an `Operand`. This operates on explicit
// operands only, and when an increments `instr->num_ops` when a new explicit
// operand is found.
static bool ConvertDecodedOperand(Instruction *instr,
                                  const xed_decoded_inst_t *xedd,
                                  unsigned op_num) {
  auto xedi = xed_decoded_inst_inst(xedd);
  auto op = xed_inst_operand(xedi, op_num);
  auto iform = xed_decoded_inst_get_iform_enum(xedd);
  bool is_explicit = XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op) ||
                     IsAmbiguousOperand(instr->iclass, iform, op_num);
  if (!is_explicit) {
    return false;
  }

  auto op_name = xed_operand_name(op);
  auto op_type = xed_operand_type(op);
  auto instr_op = &(instr->ops[op_num]);

  instr_op->rw = xed_operand_rw(op);
  instr_op->is_sticky = !is_explicit;
  instr_op->is_explicit = true;

  if (xed_operand_is_register(op_name)) {
    ConvertRegisterOperand(instr, instr_op, xedd, op_name);
  } else if (XED_OPERAND_RELBR == op_name) {
    ConvertRelativeBranch(instr, instr_op, xedd);
  } else if (XED_OPERAND_MEM0 == op_name) {
    ConvertBaseDisp(instr, instr_op, xedd, 0);
  } else if (XED_OPERAND_MEM1 == op_name) {
    ConvertBaseDisp(instr, instr_op, xedd, 1);
  } else if (XED_OPERAND_TYPE_IMM == op_type ||
             XED_OPERAND_TYPE_IMM_CONST == op_type) {
    ConvertImmediateOperand(instr_op, xedd, op_name);
  } else {
    // Ignore `XED_OPERAND_AGEN`, which is only for LEA.
    instr_op->type = XED_ENCODER_OPERAND_TYPE_INVALID;
    GRANARY_ASSERT(false);  // TODO(pag): Implement this!
  }

  ++instr->num_explicit_ops;
  return true;
}

// Convert the operands of a `xed_decoded_inst_t` to `Operand` types.
static void ConvertDecodedOperands(Instruction *instr,
                                   const xed_decoded_inst_t *xedd,
                                   unsigned num_ops) {
  for (auto o = 0U; o < num_ops; ++o) {
    if (!ConvertDecodedOperand(instr, xedd, o)) {
      break;
    }
  }
}

// Get the prefixes out of the instruction; however, ignore branch hint
// prefixes.
static void ConvertDecodedPrefixes(Instruction *instr,
                                   const xed_decoded_inst_t *xedd) {
  instr->has_prefix_rep = xed_operand_values_has_rep_prefix(xedd);
  instr->has_prefix_repne = xed_operand_values_has_repne_prefix(xedd);
  instr->has_prefix_br_hint_taken = xed_operand_values_branch_taken_hint(xedd);
  instr->has_prefix_br_hint_not_taken = \
      xed_operand_values_branch_not_taken_hint(xedd);
}

// Convert a `xed_decoded_inst_t` into an `Instruction`.
static void ConvertDecodedInstruction(Instruction *instr,
                                      const xed_decoded_inst_t *xedd,
                                      AppPC pc) {
  auto xedi = xed_decoded_inst_inst(xedd);
  memset(instr, 0, sizeof *instr);
  instr->decoded_pc = pc;
  instr->iclass = xed_decoded_inst_get_iclass(xedd);
  instr->category = xed_decoded_inst_get_category(xedd);
  instr->decoded_length = static_cast<uint8_t>(
      xed_decoded_inst_get_length(xedd));
  ConvertDecodedPrefixes(instr, xedd);
  instr->is_atomic = xed_operand_values_get_atomic(xedd);
  instr->effective_operand_width = static_cast<int8_t>(
      xed_decoded_inst_get_operand_width(xedd));
  ConvertDecodedOperands(instr, xedd, xed_inst_noperands(xedi));
  instr->AnalyzeStackUsage();
}
}  // namespace

// Decode an x86-64 instruction into a Granary `Instruction`, by first going
// through XED's `xed_decoded_inst_t` IR.
AppPC InstructionDecoder::DecodeInternal(DecodedBasicBlock *block,
                                         Instruction *instr, AppPC pc) {
  if (pc) {
    xed_decoded_inst_t xedd;
    if (XED_ERROR_NONE == DecodeBytes(&xedd, pc)) {
      ConvertDecodedInstruction(instr, &xedd, pc);
      auto next_pc = pc + instr->decoded_length;
      MangleDecodedInstruction(block, instr);
      return next_pc;
    }
  }
  return nullptr;
}

}  // namespace arch
}  // namespace granary
