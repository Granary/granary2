/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/driver/xed2-intel64/decode.h"
#include "granary/driver/xed2-intel64/instruction.h"

#include "granary/breakpoint.h"

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

// Fill in an operand as if it's a register operand.
static void FillRegisterOperand(Operand *instr_op, xed_reg_enum_t reg) {
  instr_op->type = XED_ENCODER_OPERAND_TYPE_REG;
  instr_op->reg.DecodeArchRegister(reg);
  instr_op->width = static_cast<int8_t>(instr_op->reg.BitWidth());
}

// Pull out a register operand from the XED instruction.
static void ConvertRegisterOperand(Operand *instr_op,
                                   const xed_decoded_inst_t *xedd,
                                   xed_operand_enum_t op_name) {
  auto reg = xed_decoded_inst_get_reg(xedd, op_name);
  instr_op->type = XED_ENCODER_OPERAND_TYPE_REG;
  instr_op->reg.DecodeArchRegister(reg);
  instr_op->width = static_cast<int8_t>(xed_get_register_width_bits64(reg));
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

// Decode an individual LEA seg/base/disp register into an operand.
static void DecodeLEAImm(Instruction *lea, intptr_t imm, bool is_sticky) {
  if (imm) {
    auto op = &(lea->ops[lea->num_ops++]);
    op->imm.as_int = imm;
    op->rw = XED_OPERAND_ACTION_R;
    op->is_sticky = is_sticky;
  }
}

// Decode an individual LEA seg/base/disp register into an operand.
static void DecodeLEAReg(Instruction *lea, xed_reg_enum_t reg, bool is_sticky) {
  if (XED_REG_INVALID != reg) {
    auto op = &(lea->ops[lea->num_ops++]);
    op->reg.DecodeArchRegister(reg);
    op->rw = XED_OPERAND_ACTION_R;
    op->is_sticky = is_sticky;
  }
}

// Decode a single memory operand into a pseudo LEA instruction that writes to
// a temporary virtual register.
//
// The `all_sticky` parameter is meant to communicate the constraint that none
// of the operands of this pseudo LEA can be changed. This comes up for implicit
// memory operands (e.g. rep movs, rep stos, xlat).
//
// The order of operands is: disp, seg, base, index, scale.
static VirtualRegister LoadMemoryOperand(DecodedBasicBlock *block,
                                         const xed_decoded_inst_t *xedd,
                                         unsigned index, bool is_sticky) {
  auto disp = xed_decoded_inst_get_memory_displacement(xedd, index);
  auto scale = xed_decoded_inst_get_scale(xedd, index);
  auto segment_reg = xed_decoded_inst_get_seg_reg(xedd, index);
  auto base_reg = xed_decoded_inst_get_base_reg(xedd, index);
  auto index_reg = xed_decoded_inst_get_index_reg(xedd, index);

  if (XED_REG_INVALID == segment_reg && 0 == disp && 1 >= scale &&
      (XED_REG_INVALID == base_reg || XED_REG_INVALID == index_reg)) {
    if (XED_REG_INVALID != base_reg) {
      return VirtualRegister::FromArchRegister(base_reg);
    } else {
      return VirtualRegister::FromArchRegister(index_reg);
    }

  } else {
    Instruction lea;
    lea.iclass = XED_ICLASS_LEA;
    lea.category = XED_CATEGORY_MISC;
    lea.effective_operand_width = arch::ADDRESS_WIDTH_BITS;
    lea.has_memory_op = false;
    lea.num_ops = 1;

    lea.ops[0].reg = block->AllocateVirtualRegister();
    lea.ops[0].type = XED_ENCODER_OPERAND_TYPE_REG;
    lea.ops[0].width = arch::GPR_WIDTH_BITS;
    lea.ops[0].rw = XED_OPERAND_ACTION_W;
    lea.ops[0].is_sticky = true;

    DecodeLEAImm(&lea, disp, is_sticky);
    DecodeLEAReg(&lea, segment_reg, is_sticky);
    DecodeLEAReg(&lea, base_reg, is_sticky);
    DecodeLEAReg(&lea, index_reg, is_sticky);
    DecodeLEAImm(&lea, static_cast<intptr_t>(scale), is_sticky);

    lea.num_explicit_ops = lea.num_ops;

    GRANARY_ASSERT(XED_ENCODER_OPERANDS_MAX >= lea.num_ops);

    // Add the instruction into the block.
    auto instr = new NativeInstruction(&lea);
    block->AppendInstruction(std::unique_ptr<granary::Instruction>(instr));
    return lea.ops[0].reg;
  }
}

// Decode a load effective address (LEA) instruction. We don't decode it
// it according to our typical pattern because of
static void ConvertLoadEffectiveAddress(Instruction *instr,
                                        const xed_decoded_inst_t *xedd) {
  auto base_reg = xed_decoded_inst_get_base_reg(xedd, 0);
  instr->num_ops = 1;

  instr->ops[0].rw = XED_OPERAND_ACTION_W;
  ConvertRegisterOperand(&(instr->ops[0]), xedd, XED_OPERAND_REG0);

  if (RegIsInstructionPointer(base_reg)) {
    instr->num_ops = 2;
    auto instr_op = &(instr->ops[1]);
    instr_op->type = XED_ENCODER_OPERAND_TYPE_IMM0;  // Overloaded meaning.
    instr_op->addr.as_ptr = GetPCRelativeMemoryAddress(instr, xedd, 0);
    instr_op->width = static_cast<int8_t>(
        xed3_operand_get_mem_width(xedd) * 8); // Width of addressed memory.
    instr_op->rw = XED_OPERAND_ACTION_R;
    instr_op->is_sticky = true;
  } else {
    DecodeLEAReg(instr, xed_decoded_inst_get_seg_reg(xedd, 0), false);
    DecodeLEAImm(instr, xed_decoded_inst_get_memory_displacement(xedd, 0),
                 false);
    DecodeLEAReg(instr, xed_decoded_inst_get_seg_reg(xedd, 0), false);
    DecodeLEAReg(instr, base_reg, false);
    DecodeLEAReg(instr, xed_decoded_inst_get_index_reg(xedd, 0), false);
    DecodeLEAImm(instr,
                 static_cast<intptr_t>(xed_decoded_inst_get_scale(xedd, 0)),
                 false);
  }

  instr->num_explicit_ops = instr->num_ops;
}

// Convert a memory operand into an `Operand`.
static void ConverMemoryOperand(DecodedBasicBlock *block, Instruction *instr,
                                Operand *instr_op,
                                const xed_decoded_inst_t *xedd,
                                unsigned index) {
  auto is_sticky = instr->has_prefix_rep || instr->has_prefix_repne ||
                   XED_ICLASS_XLAT == instr->iclass;

  instr_op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  instr_op->reg = LoadMemoryOperand(block, xedd, index, is_sticky);
  instr_op->width = static_cast<int8_t>(xed3_operand_get_mem_width(xedd) * 8);
  instr_op->is_sticky = is_sticky;
}

// Pull out an effective address from a LEA_GPRv_AGEN instruction. We actually
// treat the effective address as either an immediate or as a base/disp, unlike
// the expected XED_OPERAND_AGEN, and at encoding time convert back to an AGEN.
//
// Note: XED_OPERAND_AGEN's memory operand index is 0. See docs for function
//       `xed_agen`.
static void ConvertBaseDisp(DecodedBasicBlock *block, Instruction *instr,
                            Operand *instr_op, const xed_decoded_inst_t *xedd,
                            unsigned index) {
  instr->has_memory_op = true;
  if (RegIsInstructionPointer(xed_decoded_inst_get_base_reg(xedd, index))) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_PTR;  // Overloaded meaning.
    instr_op->addr.as_ptr = GetPCRelativeMemoryAddress(instr, xedd, index);
    instr_op->width = static_cast<int8_t>(
        xed3_operand_get_mem_width(xedd) * 8); // Width of addressed memory.
  } else {
    ConverMemoryOperand(block, instr, instr_op, xedd, index);
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

// Convert a non-terminal operand into a Granary operand. This will sometimes
// cheat by converting non-terminal operands into a close-enough representation
// that benefits other parts of Granary (e.g. the virtual register system). Not
// all non-terminal operands have a decoding that Granary cares about.
static bool ConvertNonTerminalOperand(Operand *instr_op,
                                      const xed_operand_t *op) {
  switch (xed_operand_nonterminal_name(op)) {
    case XED_NONTERMINAL_AR10:
      FillRegisterOperand(instr_op, XED_REG_R10); return true;
    case XED_NONTERMINAL_AR11:
      FillRegisterOperand(instr_op, XED_REG_R11); return true;
    case XED_NONTERMINAL_AR12:
      FillRegisterOperand(instr_op, XED_REG_R12); return true;
    case XED_NONTERMINAL_AR13:
      FillRegisterOperand(instr_op, XED_REG_R13); return true;
    case XED_NONTERMINAL_AR14:
      FillRegisterOperand(instr_op, XED_REG_R14); return true;
    case XED_NONTERMINAL_AR15:
      FillRegisterOperand(instr_op, XED_REG_R15); return true;
    case XED_NONTERMINAL_AR8:
      FillRegisterOperand(instr_op, XED_REG_R8); return true;
    case XED_NONTERMINAL_AR9:
      FillRegisterOperand(instr_op, XED_REG_R9); return true;
    case XED_NONTERMINAL_ARAX:
      FillRegisterOperand(instr_op, XED_REG_RAX); return true;
    case XED_NONTERMINAL_ARBP:
      FillRegisterOperand(instr_op, XED_REG_RBP); return true;
    case XED_NONTERMINAL_ARBX:
      FillRegisterOperand(instr_op, XED_REG_RBX); return true;
    case XED_NONTERMINAL_ARCX:
      FillRegisterOperand(instr_op, XED_REG_RCX); return true;
    case XED_NONTERMINAL_ARDI:
      FillRegisterOperand(instr_op, XED_REG_RDI); return true;
    case XED_NONTERMINAL_ARDX:
      FillRegisterOperand(instr_op, XED_REG_RDX); return true;
    case XED_NONTERMINAL_ARSI:
      FillRegisterOperand(instr_op, XED_REG_RSI); return true;
    case XED_NONTERMINAL_ARSP:
      FillRegisterOperand(instr_op, XED_REG_RSP); return true;
    case XED_NONTERMINAL_OEAX:
      FillRegisterOperand(instr_op, XED_REG_EAX); return true;
    case XED_NONTERMINAL_ORAX:
      FillRegisterOperand(instr_op, XED_REG_RAX); return true;
    case XED_NONTERMINAL_ORBP:
      FillRegisterOperand(instr_op, XED_REG_RBP); return true;
    case XED_NONTERMINAL_ORDX:
      FillRegisterOperand(instr_op, XED_REG_RDX); return true;
    case XED_NONTERMINAL_ORSP:
      FillRegisterOperand(instr_op, XED_REG_RSP); return true;
    case XED_NONTERMINAL_RIP:
      FillRegisterOperand(instr_op, XED_REG_RIP); return true;
    case XED_NONTERMINAL_SRBP:
      FillRegisterOperand(instr_op, XED_REG_RBP); return true;
    case XED_NONTERMINAL_SRSP:
      FillRegisterOperand(instr_op, XED_REG_RSP); return true;
    default: return false;
  }
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
static void ConvertDecodedOperand(DecodedBasicBlock *block, Instruction *instr,
                                  const xed_decoded_inst_t *xedd,
                                  unsigned op_num) {
  auto xedi = xed_decoded_inst_inst(xedd);
  auto op = xed_inst_operand(xedi, op_num);
  auto iform = xed_decoded_inst_get_iform_enum(xedd);
  auto op_name = xed_operand_name(op);
  auto op_type = xed_operand_type(op);
  auto instr_op = &(instr->ops[op_num]);
  bool is_explicit = XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op) ||
                     IsAmbiguousOperand(instr->iclass, iform, op_num);

  instr_op->rw = xed_operand_rw(op);
  instr_op->is_sticky = !is_explicit;

  if (xed_operand_is_register(op_name)) {
    ConvertRegisterOperand(instr_op, xedd, op_name);
  } else if (XED_OPERAND_RELBR == op_name) {
    ConvertRelativeBranch(instr, instr_op, xedd);
  } else if (XED_OPERAND_MEM0 == op_name) {
    ConvertBaseDisp(block, instr, instr_op, xedd, 0);
  } else if (XED_OPERAND_MEM1 == op_name) {
    ConvertBaseDisp(block, instr, instr_op, xedd, 1);
  } else if (XED_OPERAND_TYPE_IMM == op_type ||
             XED_OPERAND_TYPE_IMM_CONST == op_type) {
    ConvertImmediateOperand(instr_op, xedd, op_name);
  } else if (XED_OPERAND_TYPE_NT_LOOKUP_FN == op_type) {  // More complicated.
    if (!ConvertNonTerminalOperand(instr_op, op)) {
      instr_op->type = XED_ENCODER_OPERAND_TYPE_INVALID;
      GRANARY_ASSERT(!is_explicit);  // TODO(pag): Implement this!
    }
  } else {
    // Ignore `XED_OPERAND_AGEN`, which is only for LEA.
    instr_op->type = XED_ENCODER_OPERAND_TYPE_INVALID;
    GRANARY_ASSERT(false);  // TODO(pag): Implement this!
  }

  if (is_explicit) {
    ++instr->num_explicit_ops;
  } else {

  }
}

// Convert the operands of a `xed_decoded_inst_t` to `Operand` types.
static void ConvertDecodedOperands(DecodedBasicBlock *block, Instruction *instr,
                                   const xed_decoded_inst_t *xedd) {
  auto num_ops = static_cast<unsigned>(instr->num_ops);
  for (auto o = 0U; o < num_ops; ++o) {
    ConvertDecodedOperand(block, instr, xedd, o);
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
static void ConvertDecodedInstruction(DecodedBasicBlock *block,
                                      Instruction *instr,
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
  instr->num_ops = static_cast<uint8_t>(xed_inst_noperands(xedi));
  instr->effective_operand_width = static_cast<int8_t>(
      xed_decoded_inst_get_operand_width(xedd));

  if (GRANARY_UNLIKELY(XED_ICLASS_LEA == instr->iclass)) {
    ConvertLoadEffectiveAddress(instr, xedd);
  } else {
    ConvertDecodedOperands(block, instr, xedd);
  }
}
}  // namespace

// Decode an x86-64 instruction into a Granary `Instruction`, by first going
// through XED's `xed_decoded_inst_t` IR.
AppPC InstructionDecoder::DecodeInternal(DecodedBasicBlock *block,
                                         Instruction *instr, AppPC pc) {
  if (pc) {
    xed_decoded_inst_t xedd;
    if (XED_ERROR_NONE == DecodeBytes(&xedd, pc)) {
      ConvertDecodedInstruction(block, instr, &xedd, pc);
      return pc + instr->decoded_length;
    }
  }
  return nullptr;
}

#if 0
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
    ir_op->width = arch::ADDRESS_WIDTH_BITS;
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
      ir->effective_operand_width = arch::ADDRESS_WIDTH_BITS;
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
  ir->noperands = static_cast<decltype(ir->noperands)>(instr->num_explicit_ops);
  for (int8_t i(0); i < instr->num_explicit_ops; ++i) {
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

// Encode a XED instruction intermediate representation into an x86
// instruction.
CachePC InstructionDecoder::EncodeInternal(Instruction *instr, CachePC pc) {
  if (instr->needs_encoding || instr->has_pc_rel_op) {
    EncodeInstruction(instr, pc);
    instr->needs_encoding = false;
  }
  CopyEncodedBytes(instr, pc);
  return pc + instr->length;
}
#endif
}  // namespace driver
}  // namespace granary
