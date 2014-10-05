/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "arch/decode.h"
#include "arch/x86-64/early_mangle.h"
#include "arch/x86-64/instruction.h"

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
bool InstructionDecoder::DecodeNext(Instruction *instr, AppPC *pc) {
  *pc = DecodeInternal(instr, *pc);
  return nullptr != *pc;
}

// Decode an x86 instruction into an instruction IR.
bool InstructionDecoder::Decode(Instruction *instr, AppPC pc) {
  return nullptr != DecodeInternal(instr, pc);
}

// Mangle a decoded instruction. Separated from the `Decode` step because
// mangling might involve adding many new instructions to deal with some
// instruction set peculiarities, and sometimes we only want to speculatively
// decode and instruction and not add these extra instructions to a block.
void InstructionDecoder::Mangle(DecodedBasicBlock *block, Instruction *instr) {
  GRANARY_ASSERT(XED_ICLASS_INVALID != instr->iclass);
  MangleDecodedInstruction(block, instr);
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
  instr_op->width = static_cast<int16_t>(xed_get_register_width_bits64(reg));

  if (XED_REG_AH <= reg && reg <= XED_REG_BH) {
    instr->uses_legacy_registers = true;
  }

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

  // On 64 bit, all segment registers except `GS` and `FS` are zero. Some
  // instructions (e.g. `MOVS`) implicitly use `ES` and `DS`, but we're only
  // decoding explicit operands.
  switch (segment_reg) {
    case XED_REG_CS:
    case XED_REG_DS:
    case XED_REG_ES:
    case XED_REG_SS:
      segment_reg = XED_REG_INVALID;
      break;
    default:
      // `LEA` doesn't go to memory (GDT or LDT) and therefore ignores any
      // segment selectors present on the memory ops.
      if (XED_ICLASS_LEA == instr->iclass) {
        segment_reg = XED_REG_INVALID;
      }
      break;
  }

  instr_op->type = XED_ENCODER_OPERAND_TYPE_MEM;

  // Hard-coded offset from a segment register.
  if (XED_REG_INVALID == base_reg && XED_REG_INVALID == index_reg) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_PTR;
    instr_op->is_compound = false;
    instr_op->addr.as_int = disp;
    if (XED_REG_INVALID == segment_reg) {
      segment_reg = XED_REG_DS;
    }

  // Try to simplify the memory operand to a non-compound one.
  } else if (XED_REG_INVALID == base_reg && !disp && 1 == scale &&
             XED_REG_RSP != index_reg) {
    instr_op->reg.DecodeFromNative(static_cast<int>(index_reg));
    instr_op->is_compound = false;
    if (XED_REG_INVALID != segment_reg) {
      instr_op->reg.ConvertToSegmentOffset();
    }
  } else if (XED_REG_INVALID == index_reg && !disp &&
             XED_REG_RSP != base_reg) {
    instr_op->reg.DecodeFromNative(static_cast<int>(base_reg));
    instr_op->is_compound = false;
    if (XED_REG_INVALID != segment_reg) {
      instr_op->reg.ConvertToSegmentOffset();
    }
  } else {
    instr_op->mem.disp = static_cast<int32_t>(disp);
    instr_op->mem.reg_base = base_reg;
    instr_op->mem.reg_index = index_reg;
    instr_op->mem.scale = static_cast<uint8_t>(scale);
    instr_op->is_compound = true;
  }

  instr_op->segment = segment_reg;
  instr_op->width = static_cast<int16_t>(xed3_operand_get_mem_width(xedd) * 8);
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
  auto mem_op_width = static_cast<int16_t>(
      xed3_operand_get_mem_width(xedd) * 8);
  if (RegIsInstructionPointer(xed_decoded_inst_get_base_reg(xedd, index))) {
    instr_op->type = XED_ENCODER_OPERAND_TYPE_PTR;  // Overloaded meaning.
    instr_op->addr.as_ptr = GetPCRelativeMemoryAddress(instr, xedd, index);
    instr_op->segment = XED_REG_DS;
    instr_op->width = mem_op_width; // Width of addressed memory.

    if (!instr_op->width) {
      instr_op->width = instr->effective_operand_width;
    }
  } else {
    ConvertMemoryOperand(instr, instr_op, xedd, index);
  }
  if (GRANARY_UNLIKELY(!instr_op->width)) {
    if (XED_ICLASS_LEA == instr->iclass) {
      instr_op->width = instr->effective_operand_width;
    } else if (mem_op_width) {
      instr_op->width = mem_op_width;
    }
  }
  GRANARY_ASSERT(0 != instr_op->width);
}

// Pull out an immediate operand from the XED instruction.
static void ConvertImmediateOperand(Instruction *instr,
                                    Operand *instr_op,
                                    const xed_decoded_inst_t *xedd,
                                    xed_operand_enum_t op_name) {
  if (XED_OPERAND_IMM0SIGNED == op_name ||
      xed_operand_values_get_immediate_is_signed(xedd)) {
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
  } else {
    GRANARY_ASSERT(false);
  }
  instr_op->width = static_cast<int16_t>(
      xed_decoded_inst_get_immediate_width_bits(xedd));

  // Ensure that we reflect the size of the stack pointer change in the size of
  // the immediate.
  if (XED_ICLASS_PUSH == instr->iclass &&
      instr->effective_operand_width > instr_op->width) {
    instr_op->width = instr->effective_operand_width;

    // Get XED to do the appropriate sign-extension for us ;-)
    instr_op->imm.as_int = static_cast<intptr_t>(
        xed_decoded_inst_get_signed_immediate(xedd));
  }
}

// Returns `true` if a particular operand is a register operand. In practice
// `BASE0` and `BASE1` never show up as explicit operands, and are instead part
// of the implicit/suppressed operands like stack pushing/popping, etc.
static bool IsRegisterOperand(xed_operand_enum_t op_name) {
  switch (op_name) {
    case XED_OPERAND_REG0:
    case XED_OPERAND_REG1:
    case XED_OPERAND_REG2:
    case XED_OPERAND_REG3:
    case XED_OPERAND_REG4:
    case XED_OPERAND_REG5:
    case XED_OPERAND_REG6:
    case XED_OPERAND_REG7:
    case XED_OPERAND_REG8:
    case XED_OPERAND_BASE0:
    case XED_OPERAND_BASE1:
      return true;
    default:
      return false;
  }
}

// Convert a `xed_operand_t` into an `Operand`. This operates on explicit
// operands only, and when an increments `instr->num_ops` when a new explicit
// operand is found.
static bool ConvertDecodedOperand(Instruction *instr,
                                  const xed_decoded_inst_t *xedd,
                                  unsigned op_num) {
  auto xedi = xed_decoded_inst_inst(xedd);
  auto op = xed_inst_operand(xedi, op_num);
  auto is_explicit = XED_OPVIS_EXPLICIT == xed_operand_operand_visibility(op);
  auto is_sticky = false;
  if (IsAmbiguousOperand(instr->iclass, instr->iform, op_num)) {
    is_explicit = true;
    is_sticky = true;
  }
  if (!is_explicit) {
    return false;
  }

  auto op_name = xed_operand_name(op);
  auto op_type = xed_operand_type(op);
  auto instr_op = &(instr->ops[op_num]);

  instr_op->rw = xed_operand_rw(op);
  instr_op->is_sticky = is_sticky;
  instr_op->is_explicit = true;

  if (IsRegisterOperand(op_name)) {
    ConvertRegisterOperand(instr, instr_op, xedd, op_name);
  } else if (XED_OPERAND_RELBR == op_name) {
    ConvertRelativeBranch(instr, instr_op, xedd);
  } else if (XED_OPERAND_MEM0 == op_name || XED_OPERAND_AGEN == op_name) {
    ConvertBaseDisp(instr, instr_op, xedd, 0);
  } else if (XED_OPERAND_MEM1 == op_name) {
    ConvertBaseDisp(instr, instr_op, xedd, 1);
  } else if (XED_OPERAND_TYPE_IMM == op_type ||
             XED_OPERAND_TYPE_IMM_CONST == op_type) {
    ConvertImmediateOperand(instr, instr_op, xedd, op_name);
  } else {
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
  // Only get the `REP` and `REPNE` prefixes if the prefixes aren't used for
  // instruction refinement (as is the case for SSE instructions). If we took
  // in the repeat prefixes for those instructions and passed them through to
  // then encoder then it will barf.
  if (xed_operand_values_has_real_rep(xedd)) {
    instr->has_prefix_rep = xed_operand_values_has_rep_prefix(xedd);
    instr->has_prefix_repne = xed_operand_values_has_repne_prefix(xedd);
  }
  instr->has_prefix_lock = xed_operand_values_has_lock_prefix(xedd);
}

// Convert a `xed_decoded_inst_t` into an `Instruction`.
static void ConvertDecodedInstruction(Instruction *instr,
                                      const xed_decoded_inst_t *xedd,
                                      AppPC pc) {
  auto xedi = xed_decoded_inst_inst(xedd);
  memset(instr, 0, sizeof *instr);
  instr->decoded_pc = pc;
  instr->iclass = xed_decoded_inst_get_iclass(xedd);
  instr->iform = xed_decoded_inst_get_iform_enum(xedd);
  instr->isel = static_cast<unsigned>(xed_decoded_inst_inst(xedd) -
                                      xed_inst_table_base());
  instr->category = xed_decoded_inst_get_category(xedd);
  instr->decoded_length = static_cast<uint8_t>(
      xed_decoded_inst_get_length(xedd));
  ConvertDecodedPrefixes(instr, xedd);
  instr->is_atomic = xed_operand_values_get_atomic(xedd) ||
                     instr->has_prefix_lock;
  instr->effective_operand_width = static_cast<int16_t>(
      xed_decoded_inst_get_operand_width(xedd));
  ConvertDecodedOperands(instr, xedd, xed_inst_noperands(xedi));
  instr->AnalyzeStackUsage();
  GRANARY_IF_DEBUG( instr->note_create = __builtin_return_address(0); )
}
}  // namespace

// Decode an x86-64 instruction into a Granary `Instruction`, by first going
// through XED's `xed_decoded_inst_t` IR.
AppPC InstructionDecoder::DecodeInternal(Instruction *instr, AppPC pc) {
  while (pc) {
    xed_decoded_inst_t xedd;
    auto error = DecodeBytes(&xedd, pc);
    if (XED_ERROR_NONE != error) {
      GRANARY_ASSERT(false);
      break;
    }

    // Skip NOPs.
    const auto category = xed_decoded_inst_get_category(&xedd);
    if (XED_CATEGORY_NOP == category || XED_CATEGORY_WIDENOP == category) {
      pc += xed_decoded_inst_get_length(&xedd);
      continue;
    }

    ConvertDecodedInstruction(instr, &xedd, pc);
    switch (instr->iclass) {
      case XED_ICLASS_UD2:
      case XED_ICLASS_HLT:  // TODO(pag): Add support for me!
        return nullptr;
      case XED_ICLASS_SWAPGS:  // TODO(pag): Add support for me!
      case XED_ICLASS_SYSRET:
        //granary_curiosity();
        return nullptr;
      case XED_ICLASS_XBEGIN:
      case XED_ICLASS_XEND:
      case XED_ICLASS_XABORT:
      case XED_ICLASS_XTEST:
        // TODO(pag): Implement me!!
        //
        // Note: We can't put an assertion here because if we're using the
        //       `whole_func` or `follow_jumps` tool and they walk into some
        //       CPU-specific lock-elision code, then we'll hit an issue.
        return nullptr;

      default: break;
    }

    auto next_pc = pc + instr->decoded_length;

    // Treat conditional jumps to the next instruction as NOPs.
    if (XED_ICLASS_XBEGIN != instr->iclass &&
        instr->IsConditionalJump() &&
        instr->BranchTargetPC() == next_pc) {
      pc = next_pc;
      continue;
    }

    return next_pc;
  }
  return nullptr;
}

}  // namespace arch
}  // namespace granary
