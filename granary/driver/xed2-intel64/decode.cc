/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"

#include "granary/driver/xed2-intel64/decode.h"
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
  instr_op->rel_app_pc = instr->decoded_pc + instr->length +
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

// Convert a `xed_operand_t` into an `Operand`. This operates on explicit
// operands only, and when an increments `instr->num_ops` when a new explicit
// operand is found.
static void ConvertDecodedOperand(Instruction *instr, xed_decoded_inst_t *xedd,
                                  const xed_operand_t *op) {
  if (XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op)) {
    auto op_name = xed_operand_name(op);
    auto op_type = xed_operand_type(op);
    auto instr_op = &(instr->ops[instr->num_ops++]);
    instr_op->rw = xed_operand_rw(op);

    if (xed_operand_is_register(op_name)) {
      ConvertRegisterOperand(instr_op, xedd, op_name);
    } else if (XED_OPERAND_RELBR == op_name) {
      ConvertRelativeBranch(instr, instr_op, xedd);
    } else if (XED_OPERAND_TYPE_IMM == op_type ||
               XED_OPERAND_TYPE_IMM_CONST == op_type) {
      ConvertImmediateOperand(instr_op, xedd, op_name);
    } else if (XED_OPERAND_TYPE_NT_LOOKUP_FN == op_type) {  // More complicated.
      granary_break_on_fault();  // TODO(pag): Implement this!
    } else {
      granary_break_on_fault();  // TODO(pag): Implement this!
    }
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

// Get the prefixes out of the instruction; however, ignore branch hint
// prefixes.
static void ConvertDecodedPrefixes(Instruction *instr,
                                   xed_decoded_inst_t *xedd) {
  instr->prefixes.s.rep = xed_operand_values_has_rep_prefix(xedd);
  instr->prefixes.s.repne = xed_operand_values_has_repne_prefix(xedd);
  instr->prefixes.s.lock = xed_operand_values_has_lock_prefix(xedd);
}

// Copy the raw bytes into the `encode_buffer` of the instruction. This will
// strip off branch hint prefixes and adjust the instruction length if there
// was a branch hint.
static void ConvertBytes(Instruction *instr, xed_decoded_inst_t *xedd,
                         AppPC pc) {
  auto jcc_has_prefix = XED_CATEGORY_COND_BR == instr->category &&
                        (xed_operand_values_branch_not_taken_hint(xedd) ||
                         xed_operand_values_branch_taken_hint(xedd));
  if (jcc_has_prefix) {
    instr->length -= 1;
  }
  memcpy(&(instr->encode_buffer[0]),
         &(pc[jcc_has_prefix ? 1 : 0]),
         static_cast<size_t>(instr->length));
}

// Convert a `xed_decoded_inst_t` into an `Instruction`.
static void ConvertDecodedInstruction(Instruction *instr,
                                      xed_decoded_inst_t *xedd,
                                      AppPC pc) {
  memset(instr, 0, sizeof *instr);
  instr->iclass = xed_decoded_inst_get_iclass(xedd);
  instr->category = xed_decoded_inst_get_category(xedd);
  instr->length = static_cast<decltype(instr->length)>(
      xed_decoded_inst_get_length(xedd));
  instr->num_ops = 0;
  instr->needs_encoding = false;
  instr->has_pc_rel_op = false;
  instr->is_atomic = xed_operand_values_get_atomic(xedd);
  instr->decoded_pc = pc;
  ConvertDecodedPrefixes(instr, xedd);
  ConvertDecodedOperands(instr, xedd);
  ConvertBytes(instr, xedd, pc);
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

namespace {

// Convert a Granary relative branch operand into a XED encoder one. Granary's
// `Operand` structure maintains the actual target, and then when we know where
// the instruction will be encoded, we calculate the relative displacement. Here
// we dependend on `instr->length` because we know ahead of time how long
// the various jumps are. In other cases, `instr->length` is not safe to use.
static void ConvertRelBranchOperand(const Instruction *instr,
                                    const Operand *op,
                                    xed_encoder_operand_t *ir_op) {
  ir_op->width = 32;
  if (instr->encoded_pc) {
    ir_op->u.brdisp = static_cast<decltype(ir_op->u.brdisp)>(
        op->rel_app_pc - (instr->encoded_pc + instr->length));
  } else {
    ir_op->u.brdisp = 0;
  }
}

// Convert an `Operand` instro a `xed_encoder_operand_t`.
static void ConvertEncodedOperand(const Instruction *instr,
                                  const Operand *op,
                                  xed_encoder_operand_t *ir_op,
                                  uint32_t *max_width) {
  ir_op->type = op->type;
  if (XED_ENCODER_OPERAND_TYPE_BRDISP == op->type) {
    ConvertRelBranchOperand(instr, op, ir_op);
  } else {
    ir_op->width = op->width;
    ir_op->u = op->u;
  }

  // Set the effective operand width to max width of all operands.
  if (op->width > *max_width) {
    *max_width = op->width;
  }
}

// Conver an `Instruction` instances into an `xed_encoder_instruction_t`.
static void ConvertInstruction(const Instruction *instr,
                               xed_encoder_instruction_t *ir) {
  uint32_t width(0);
  ir->mode = XED_STATE;
  ir->iclass = instr->iclass;
  ir->prefixes = instr->prefixes;
  ir->noperands = static_cast<decltype(ir->noperands)>(instr->num_ops);
  for (int8_t i(0); i < instr->num_ops; ++i) {
    ConvertEncodedOperand(instr, &(instr->ops[i]), &(ir->operands[i]), &width);
  }
  ir->effective_operand_width = width;
  ir->effective_address_width = 0;
}

// Encode an instruction into the instruction's encode buffer.
static void EncodeInstruction(Instruction *instr, CachePC pc) {
  if (pc) {
    instr->encoded_pc = pc;
  }
  xed_encoder_request_t xedd;
  xed_encoder_instruction_t ir;
  ConvertInstruction(instr, &ir);
  xed_encoder_request_zero_set_mode(&xedd, &XED_STATE);
  granary_break_on_fault_if(!xed_convert_to_encoder_request(&xedd, &ir));
  unsigned encoded_len(0);
  auto encode_status = xed_encode(&xedd, &(instr->encode_buffer[0]),
                                  XED_MAX_INSTRUCTION_BYTES, &encoded_len);
  granary_break_on_fault_if(XED_ERROR_NONE != encode_status);
  instr->length = static_cast<decltype(instr->length)>(encoded_len);
}

// Copy the encoded instruction buffer to the encode location.
static void CopyEncodedBytes(Instruction *instr, CachePC pc) {
  if (pc) {
    memcpy(pc, &(instr->encode_buffer[0]), static_cast<size_t>(instr->length));
  }
}
}  // namespace

// Encode a DynamoRIO instruction intermediate representation into an x86
// instruction.
CachePC InstructionDecoder::EncodeInternal(Instruction *instr, CachePC pc) {
  if (instr->needs_encoding || instr->has_pc_rel_op) {
    EncodeInstruction(instr, pc);
    instr->needs_encoding = false;
  }
  CopyEncodedBytes(instr, pc);
  return pc + instr->length;
}

}  // namespace driver
}  // namespace granary
