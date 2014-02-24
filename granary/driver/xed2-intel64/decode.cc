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

// Import XED instruction information into Granary's low-level IR. This
// initializes a number of the internal `Instruction` fields to sane defaults.
void InitInstruction(Instruction *instr, xed_iclass_enum_t iclass,
                     xed_category_enum_t category, int8_t num_ops);

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

// Pull out a register operand from the XED instruction.
static void ConvertRegisterOperand(Operand *instr_op, xed_decoded_inst_t *xedd,
                                   xed_operand_enum_t op_name) {
  instr_op->type = XED_ENCODER_OPERAND_TYPE_REG;
  instr_op->u.reg = xed_decoded_inst_get_reg(xedd, op_name);
  instr_op->width = static_cast<int8_t>(
      xed_get_register_width_bits64(instr_op->u.reg));
}

static intptr_t NextDecodedAddress(const Instruction *instr) {
  return reinterpret_cast<intptr_t>(instr->decoded_pc + instr->length);
}

// Pull out a PC-relative branch target from the XED instruction.
static void ConvertRelativeBranch(Instruction *instr, Operand *instr_op,
                                  xed_decoded_inst_t *xedd) {
  instr->has_pc_rel_op = true;
  instr->has_fixed_length = true;
  instr_op->type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  instr_op->width = GRANARY_ARCH_ADDRESS_WIDTH;
  instr_op->rel.imm = NextDecodedAddress(instr) +
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

// Returns true if a register is the instruction pointer.
static bool RegIsInstructionPointer(xed_reg_enum_t reg) {
  return XED_REG_RIP == reg || XED_REG_EIP == reg || XED_REG_IP == reg;
}

// Convert a memory operand into an `Operand`.
static void ConverMemoryOperand(Instruction *instr, Operand *instr_op,
                                xed_decoded_inst_t *xedd, unsigned index) {
  instr_op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  instr_op->u.mem.seg = xed_decoded_inst_get_seg_reg(xedd, index);
  instr_op->u.mem.base = xed_decoded_inst_get_base_reg(xedd, index);
  instr_op->u.mem.index = xed_decoded_inst_get_index_reg(xedd, index);
  instr_op->u.mem.scale = xed_decoded_inst_get_scale(xedd, index);
  instr_op->u.mem.disp.displacement =
      static_cast<decltype(instr_op->u.mem.disp.displacement)>(
          xed_decoded_inst_get_memory_displacement(xedd, index));
  instr_op->u.mem.disp.displacement_width =
      xed_decoded_inst_get_memory_displacement_width_bits(xedd, index);

  instr_op->width = static_cast<int8_t>(xed3_operand_get_mem_width(xedd) * 8);
  instr->has_memory_op = true;
}

// Pull out an effective address from a LEA_GPRv_AGEN instruction. We actually
// treat the effective address as either an immediate or as a base/disp, unlike
// the expected XED_OPERAND_AGEN, and at encoding time convert back to an AGEN.
//
// Note: XED_OPERAND_AGEN's memory operand index is 0. See docs for function
//       `xed_agen`.
static void ConvertEffectiveAddress(Instruction *instr, Operand *instr_op,
                                    xed_decoded_inst_t *xedd, unsigned index) {
  if (RegIsInstructionPointer(xed_decoded_inst_get_base_reg(xedd, index))) {
    auto disp = xed_decoded_inst_get_memory_displacement(xedd, index);
    instr->has_pc_rel_op = true;
    instr_op->type = XED_ENCODER_OPERAND_TYPE_PTR;  // Overloaded meaning.
    instr_op->rel.imm = NextDecodedAddress(instr) + disp;

    if (XED_ICLASS_LEA != instr->iclass) {
      instr_op->width = static_cast<int8_t>(xed3_operand_get_mem_width(xedd) * 8);
      instr->has_memory_op = true;
    } else {
      instr_op->width = GRANARY_ARCH_ADDRESS_WIDTH;
    }
  } else {
    ConverMemoryOperand(instr, instr_op, xedd, index);
  }
}

// Pull out an immediate operand from the XED instruction.
static void ConvertImmediateOperand(Operand *instr_op, xed_decoded_inst_t *xedd,
                                    xed_operand_enum_t op_name) {
  if (XED_OPERAND_IMM0SIGNED == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_SIMM0;
    instr_op->u.imm0 = static_cast<decltype(instr_op->u.imm0)>(
        xed_decoded_inst_get_signed_immediate(xedd));

  } else if (XED_OPERAND_IMM0 == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_IMM0;
    instr_op->u.imm0 = xed_decoded_inst_get_unsigned_immediate(xedd);

  } else if (XED_OPERAND_IMM1 == op_name ||
             XED_OPERAND_IMM1_BYTES == op_name) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_IMM1;
    instr_op->u.imm1 = xed_decoded_inst_get_second_immediate(xedd);
  }
  instr_op->width = static_cast<int8_t>(
      xed_decoded_inst_get_immediate_width_bits(xedd));
}

// Returns true if an implicit operand is ambiguous. An implicit operand is
// ambiguous if there are multiple encodings for the same iclass, and the given
// operand (indexed by `op`) is explicit for some iforms but not others.
static bool IsAmbiguousOperand(xed_iclass_enum_t iclass, xed_iform_enum_t iform,
                               unsigned op_num);

#include "generated/xed2-intel64/ambiguous_operands.cc"

// Convert a `xed_operand_t` into an `Operand`. This operates on explicit
// operands only, and when an increments `instr->num_ops` when a new explicit
// operand is found.
static void ConvertDecodedOperand(Instruction *instr, xed_decoded_inst_t *xedd,
                                  const xed_operand_t *op, unsigned op_num) {
  auto iform = xed_decoded_inst_get_iform_enum(xedd);
  if (XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op) ||
      IsAmbiguousOperand(instr->iclass, iform, op_num)) {
    auto op_name = xed_operand_name(op);
    auto op_type = xed_operand_type(op);
    auto instr_op = &(instr->ops[instr->num_ops++]);
    instr_op->rw = xed_operand_rw(op);

    if (xed_operand_is_register(op_name)) {
      ConvertRegisterOperand(instr_op, xedd, op_name);
    } else if (XED_OPERAND_RELBR == op_name) {
      ConvertRelativeBranch(instr, instr_op, xedd);
    } else if (XED_OPERAND_AGEN == op_name) {
      ConvertEffectiveAddress(instr, instr_op, xedd, 0);
    } else if (XED_OPERAND_MEM0 == op_name) {
      ConvertEffectiveAddress(instr, instr_op, xedd, 0);
    } else if (XED_OPERAND_MEM1 == op_name) {
      ConvertEffectiveAddress(instr, instr_op, xedd, 1);
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
  auto num_ops = xed_inst_noperands(xedi);
  for (auto o = 0U; o < num_ops; ++o) {
    if (instr->num_ops < XED_ENCODER_OPERANDS_MAX) {
      ConvertDecodedOperand(instr, xedd, xed_inst_operand(xedi, o), o);
    }
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
  InitInstruction(instr,
                  xed_decoded_inst_get_iclass(xedd),
                  xed_decoded_inst_get_category(xedd),
                  0);
  instr->length = static_cast<decltype(instr->length)>(
      xed_decoded_inst_get_length(xedd));
  instr->effective_operand_width = static_cast<int8_t>(
      xed_decoded_inst_get_operand_width(xedd));
  GRANARY_IF_DEBUG( instr->needs_encoding = true; )
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
static void SelectRelBranchOperand(const Instruction *instr,
                                    const Operand *op,
                                    xed_encoder_operand_t *ir_op) {
  ir_op->width = 32;
  if (instr->encoded_pc) {
    ir_op->u.brdisp = static_cast<decltype(ir_op->u.brdisp)>(
        op->rel.app_pc - (instr->encoded_pc + instr->length));
  } else {
    ir_op->u.brdisp = 0;
  }
}

enum {
  RIP_REL32_LEA_LEN = 7  // 48 (rex.W) 8d (opcode) 00 (sib) 00 00 00 00 (disp)
};

// Convert a Granary LEA operand into a XED_ENCODER_OPERAND_TYPE_MEM operand.
static void SelectMemoryOperand(const Instruction *instr,
                                const Operand *op,
                                xed_encoder_operand_t *ir_op) {
  if (instr->has_pc_rel_op) {
    ir_op->width = GRANARY_ARCH_ADDRESS_WIDTH;
    ir_op->type = XED_ENCODER_OPERAND_TYPE_MEM;
    ir_op->u.mem.seg = XED_REG_INVALID;
    ir_op->u.mem.base = XED_REG_RIP;
    ir_op->u.mem.index = XED_REG_INVALID;
    ir_op->u.mem.scale = 0;
    ir_op->u.mem.disp.displacement_width = 32;
    ir_op->u.mem.disp.displacement =
        static_cast<decltype(ir_op->u.mem.disp.displacement)>(
            op->rel.pc - (instr->encoded_pc + RIP_REL32_LEA_LEN));
  } else {
    ir_op->width = static_cast<decltype(ir_op->width)>(op->width);
    ir_op->u = op->u;
  }
}

// Convert an `Operand` into a `xed_encoder_operand_t`.
static void SelectOperand(const Instruction *instr, const Operand *op,
                          xed_encoder_operand_t *ir_op, int8_t *max_width) {
  ir_op->type = op->type;
  if (instr->has_pc_rel_op && XED_ENCODER_OPERAND_TYPE_BRDISP == op->type) {
    SelectRelBranchOperand(instr, op, ir_op);
  } else if (instr->has_pc_rel_op && XED_ENCODER_OPERAND_TYPE_PTR == op->type) {
    SelectMemoryOperand(instr, op, ir_op);
  } else {
    ir_op->width = static_cast<decltype(ir_op->width)>(op->width);
    ir_op->u = op->u;
  }

  // Set the effective operand width to max width of all operands.
  if (op->width > *max_width) {
    *max_width = op->width;
  }
}

// Adjust the effective operand width. This is a special case for return
// instructions.
//
// TODO(pag): Is the effective operand width for `RET <imm>` also
//            GRANARY_ARCH_ADDRESS_WIDTH?
static void AdjustEffectiveOperandWidth(xed_encoder_instruction_t *ir) {
  if (!ir->effective_operand_width) {
    if (XED_ICLASS_RET_NEAR == ir->iclass ||
        XED_ICLASS_RET_FAR == ir->iclass) {
      ir->effective_operand_width = GRANARY_ARCH_ADDRESS_WIDTH;
    }
  }
}

// Tries to figure out the effective operand width for this instruction.
static uint32_t GetEffectiveOperandWidth(const Instruction *instr,
                                         int8_t width) {
  if (0 >= instr->effective_operand_width) {
    return static_cast<uint32_t>(width);
  } else {
    return static_cast<uint32_t>(instr->effective_operand_width);
  }
}

// Convert an `Instruction` instances into an `xed_encoder_instruction_t`.
static void SelectInstruction(const Instruction *instr,
                              xed_encoder_instruction_t *ir) {
  int8_t width = 0;
  ir->mode = XED_STATE;
  ir->iclass = instr->iclass;
  ir->prefixes = instr->prefixes;
  ir->noperands = static_cast<decltype(ir->noperands)>(instr->num_ops);
  for (int8_t i(0); i < instr->num_ops; ++i) {
    SelectOperand(instr, &(instr->ops[i]), &(ir->operands[i]), &width);
  }
  ir->effective_operand_width = GetEffectiveOperandWidth(instr, width);
  ir->effective_address_width = instr->has_memory_op ?
      GRANARY_ARCH_ADDRESS_WIDTH : 0;
  AdjustEffectiveOperandWidth(ir);
}

// Encode an instruction into the instruction's encode buffer.
static void EncodeInstruction(Instruction *instr, CachePC pc) {
  if (pc) {
    instr->encoded_pc = pc;
  }
  xed_encoder_request_t xedd;
  xed_encoder_instruction_t ir;
  SelectInstruction(instr, &ir);
  xed_encoder_request_zero_set_mode(&xedd, &XED_STATE);
  xed_encoder_request_set_iclass(&xedd, instr->iclass);
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
