/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"

#include "granary/driver/xed2-intel64/decoder.h"
#include "granary/driver/xed2-intel64/instruction.h"

namespace granary {
namespace driver {

// Decoder state that sets the mode to 64-bit.
extern xed_state_t XED_STATE;

// Decode an instruction, and update the program counter by reference to point
// to the next logical instruction. Returns `true` iff the instruction was
// successfully decoded.
bool InstructionDecoder::DecodeNext(Instruction *instr, AppPC *pc) {
  *pc = DecodeInternal(instr, *pc);
  return nullptr != *pc;
}

// Encode t an instruction IR at `*pc` and update `pc`.
bool InstructionDecoder::EncodeNext(Instruction *instr, CachePC *pc) {
  *pc = EncodeInternal(instr, *pc);
  return nullptr != *pc;
}

// Decode an x86 instruction into an instruction IR.
bool InstructionDecoder::Decode(Instruction *instr, AppPC pc) {
  return nullptr != DecodeInternal(instr, pc);
}

// Encode an instruction IR into an x86 instruction.
bool InstructionDecoder::Encode(Instruction *instr, CachePC pc) {
  return nullptr != EncodeInternal(instr, pc);
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

/*
 *
 *  XED_ENCODER_OPERAND_TYPE_INVALID,
    XED_ENCODER_OPERAND_TYPE_BRDISP,
    XED_ENCODER_OPERAND_TYPE_REG,
    XED_ENCODER_OPERAND_TYPE_IMM0,
    XED_ENCODER_OPERAND_TYPE_SIMM0,
    XED_ENCODER_OPERAND_TYPE_IMM1,
    XED_ENCODER_OPERAND_TYPE_MEM,
    XED_ENCODER_OPERAND_TYPE_PTR,

    XED_ENCODER_OPERAND_TYPE_SEG0,

    XED_ENCODER_OPERAND_TYPE_SEG1,

    XED_ENCODER_OPERAND_TYPE_OTHER
 */

/*
XED_OPERAND_TYPE_INVALID,
XED_OPERAND_TYPE_ERROR,
XED_OPERAND_TYPE_IMM,
XED_OPERAND_TYPE_IMM_CONST,
XED_OPERAND_TYPE_NT_LOOKUP_FN,
XED_OPERAND_TYPE_REG,
*/

// Pull out a register operand from the XED instruction.
static void ConvertRegisterOperand(Operand *instr_op, xed_decoded_inst_t *xedd,
                                   xed_operand_enum_t op_name) {
  instr_op->type = XED_ENCODER_OPERAND_TYPE_REG;
  instr_op->u.reg = xed_decoded_inst_get_reg(xedd, op_name);
  instr_op->width = xed_get_register_width_bits64(instr_op->u.reg);
}

// Pull out a PC-relative branch target from the XED instruction.
static void ConvertRelativeBranch(Instruction *instr, Operand *instr_op,
                                  xed_decoded_inst_t *xedd) {
  instr->has_pc_rel_op = true;
  instr_op->type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  instr_op->width = 64;
  instr_op->rel_app_pc = instr->decoded_pc +
                         xed_decoded_inst_get_branch_displacement(xedd);

  // Need to update the instruction width so that we treat all relative
  // branches as being rel32.
  switch (xed_decoded_inst_get_branch_displacement_width(xedd)) {
    case 1: instr->length += 3; break;
    case 2: instr->length += 2; break;
    case 4: break;
    default: granary_break_on_fault();
  }
}

// Pull out an immediate operand from the XED instruction.
static void ConvertImmediateOperand(Operand *instr_op, xed_decoded_inst_t *xedd,
                                    xed_operand_enum_t op_name) {
  if (XED_OPERAND_IMM0SIGNED == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_SIMM0;
    instr_op->u.imm0 = xed3_operand_get_imm0(xedd);

  } else if (XED_OPERAND_IMM0 == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_IMM0;
    instr_op->u.imm0 = xed3_operand_get_imm0(xedd);
  } else if (XED_OPERAND_IMM1 == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_IMM1;
    instr_op->u.imm1 = static_cast<decltype(instr_op->u.imm1)>(
        xed3_operand_get_imm1(xedd));
  }
  instr_op->width = xed_decoded_inst_get_immediate_width_bits(xedd);
}

// Convert a `xed_operand_t` into an `Operand`.
static void ConvertDecodedOperand(Instruction *instr, xed_decoded_inst_t *xedd,
                                  const xed_operand_t *op) {
  if (XED_OPVIS_EXPLICIT != xed_operand_operand_visibility(op)) {
    return;
  }
  auto op_name = xed_operand_name(op);
  auto op_type = xed_operand_type(op);
  auto instr_op = &(instr->ops[instr->num_operands++]);

  instr_op->rw = xed_operand_rw(op);

  if (xed_operand_is_register(op_name)) {
    ConvertRegisterOperand(instr_op, xedd, op_name);
  } else if (XED_OPERAND_RELBR == op_name) {
    ConvertRelativeBranch(instr, instr_op, xedd);
  } else if (XED_OPERAND_TYPE_IMM == op_type ||
             XED_OPERAND_TYPE_IMM_CONST == op_type) {
    ConvertImmediateOperand(instr_op, xedd, op_name);
  // More complicated.
  } else if (XED_OPERAND_TYPE_NT_LOOKUP_FN == op_type) {
    granary_break_on_fault();

  } else {
    // TODO(pag): Implement this!
    granary_break_on_fault();
  }
}

// Convert the operands of a `xed_decoded_inst_t` to `Operand` types.
static void ConvertDecodedOperands(Instruction *instr,
                                   xed_decoded_inst_t *xedd) {
  auto xedi = xed_decoded_inst_inst(xedd);
  for (auto o = 0U; o < xed_inst_noperands(xedi); ++o) {
    ConvertDecodedOperand(instr, xedd, xed_inst_operand(xedi, o));
  }
}

// Convert a `xed_decoded_inst_t` into an `Instruction`.
static void ConvertDecodedInstruction(Instruction *instr,
                                      xed_decoded_inst_t *xedd,
                                      AppPC pc) {
  memset(instr, 0, sizeof *instr);
  instr->iclass = xed_decoded_inst_get_iclass(xedd);
  instr->category = xed_decoded_inst_get_category(xedd);
  instr->length = static_cast<int8_t>(xed_decoded_inst_get_length(xedd));
  instr->num_operands = 0;
  instr->needs_encoding = false;
  instr->has_pc_rel_op = false;
  instr->decoded_pc = pc;
  memcpy(&(instr->encode_buffer[0]), pc, static_cast<size_t>(instr->length));
  ConvertDecodedOperands(instr, xedd);
}
}  // namespace

// Decode an x86-64 instruction into a Granary `Instruction`, by first going
// through XED's `xed_decoded_inst_t` IR.
AppPC InstructionDecoder::DecodeInternal(Instruction *instr, AppPC pc) {
  if (pc) {
    xed_decoded_inst_t xedd;
    if (XED_ERROR_NONE == DecodeBytes(&xedd, pc)) {
      ConvertDecodedInstruction(instr, &xedd, pc);
      return pc + instr->length;
    }
  }
  return nullptr;
}

// Encode a DynamoRIO instruction intermediate representation into an x86
// instruction.
CachePC InstructionDecoder::EncodeInternal(Instruction *instr,
                                           CachePC pc) {
  GRANARY_UNUSED(instr);
  GRANARY_UNUSED(pc);
  return pc;
}

}  // namespace driver
}  // namespace granary
