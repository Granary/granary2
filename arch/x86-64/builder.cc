/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"
#include "arch/util.h"

#include "arch/x86-64/builder.h"
#include "arch/x86-64/instruction.h"

namespace granary {
namespace arch {

// Table of all implicit operands.
extern const Operand * const IMPLICIT_OPERANDS[];

// Number of implicit operands for each iclass.
extern const uint8_t NUM_IMPLICIT_OPERANDS[];

// Initialize an emptry Granary `arch::Instruction` from a XED iclass,
// category, and the number of explicit operands.
void BuildInstruction(Instruction *instr, xed_iclass_enum_t iclass,
                      xed_iform_enum_t iform, unsigned isel,
                      xed_category_enum_t category) {
#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
  // Sanity check to make sure the backing memory is initialized with
  // reasonable values.
  GRANARY_ASSERT(XED_ICLASS_INVALID <= instr->iclass &&
                 XED_ICLASS_LAST > instr->iclass &&
                 XED_IFORM_INVALID <= instr->iform &&
                 XED_IFORM_LAST > instr->iform &&
                 0 <= instr->decoded_length &&
                 15 >= instr->decoded_length);

  auto note = instr->note_create;
#endif

  auto decoded_pc = instr->decoded_pc;
  auto decoded_len = instr->decoded_length;
  memset(instr, 0, sizeof *instr);
  instr->iclass = iclass;
  instr->iform = iform;
  instr->isel = isel;
  instr->category = category;
  instr->decoded_pc = decoded_pc;
  instr->decoded_length = decoded_len;

#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
  instr->note_create = note;
  auto mod = __builtin_return_address(1);
  (instr->note_create ? instr->note_alter : instr->note_create) = mod;
#endif

  // These are only really atomic if there's a memory op.
  //
  // TODO(pag): There might be other categories of atomic operations (with
  //            XED_ATTRIBUTE_HLE_ACQ_ABLE andXED_ATTRIBUTE_HLE_REL_ABLE, but
  //            only if they have memory operations. This might come up where
  //            an instruction is initially not atomic (e.g. reg->reg), but then
  //            is modified by a tool to be mem->reg or reg->mem, thus making it
  //            atomic.
  instr->is_atomic = XED_ICLASS_XCHG == iclass ||
                     XED_CATEGORY_SEMAPHORE == category;
}

// Copy implicit operands into the instruction.
void FinalizeInstruction(Instruction *instr) {
  const auto num_implicit_ops = NUM_IMPLICIT_OPERANDS[instr->isel];
  auto *implicit_ops = IMPLICIT_OPERANDS[instr->isel];
  memcpy(&(instr->ops[instr->num_explicit_ops]), implicit_ops,
         sizeof(Operand) * num_implicit_ops);
  instr->num_ops = num_implicit_ops + instr->num_explicit_ops;
  instr->AnalyzeStackUsage();
}

// Add this register as an operand to the instruction `instr`.
void RegisterBuilder::Build(Instruction *instr) {
  auto &op(instr->ops[instr->num_explicit_ops++]);
  op.type = XED_ENCODER_OPERAND_TYPE_REG;
  op.reg = reg;
  op.rw = action;
  op.width = static_cast<uint16_t>(reg.BitWidth());
  op.is_explicit = true;

  // Registers AH through BH are tricky to handle due to their location, so we
  // treat them as hard requirements for virtual register scheduling.
  if (reg.IsNative()) {
    auto arch_reg = static_cast<xed_reg_enum_t>(reg.EncodeToNative());
    if (XED_REG_AH <= arch_reg && arch_reg <= XED_REG_BH) {
      op.is_sticky = true;
    }
  }
}

// Add this immediate as an operand to the instruction `instr`.
void ImmediateBuilder::Build(Instruction *instr) {
  auto &op(instr->ops[instr->num_explicit_ops++]);
  op.imm.as_uint = as_uint;
  op.type = type;
  op.rw = XED_OPERAND_ACTION_R;
  if (0 >= width) {
    op.width = static_cast<uint16_t>(ImmediateWidthBits(as_uint));
  } else {
    op.width = static_cast<uint16_t>(width);
  }
  op.is_explicit = true;
}

// Add this memory as an operand to the instruction `instr`.
void MemoryBuilder::Build(Instruction *instr) {
  auto &instr_op(instr->ops[instr->num_explicit_ops++]);
  instr_op.width = 0;  // Unknown.
  instr_op.is_compound = false;
  switch (kind) {
    case BUILD_POINTER:
      instr_op.segment = XED_REG_DS;
      instr_op.type = XED_ENCODER_OPERAND_TYPE_PTR;
      instr_op.addr.as_ptr = ptr;
      break;
    case BUILD_REGISTER:
      instr_op.type = XED_ENCODER_OPERAND_TYPE_MEM;
      instr_op.reg = reg;
      break;
    case BUILD_OPERAND:
      instr_op = op;
      break;
  }
  instr_op.rw = action;
  instr_op.is_explicit = true;

  // Note: These need to be kept consistent with `ConvertMemoryOperand` in
  //       `decode.cc` and with `InlineAssemblyParser::ParseMemoryOperand`.
  switch (instr->iform) {
    case XED_IFORM_BNDCN_BND_AGEN:
    case XED_IFORM_BNDCL_BND_AGEN:
    case XED_IFORM_BNDCU_BND_AGEN:
    case XED_IFORM_BNDMK_BND_AGEN:
    case XED_IFORM_CLFLUSH_MEMmprefetch:
    case XED_IFORM_CLFLUSHOPT_MEMmprefetch:
    case XED_IFORM_LEA_GPRv_AGEN:
    case XED_IFORM_PREFETCHNTA_MEMmprefetch:
    case XED_IFORM_PREFETCHT0_MEMmprefetch:
    case XED_IFORM_PREFETCHT1_MEMmprefetch:
    case XED_IFORM_PREFETCHT2_MEMmprefetch:
    case XED_IFORM_PREFETCHW_0F0Dr1:
    case XED_IFORM_PREFETCHW_0F0Dr3:
    case XED_IFORM_PREFETCHWT1_MEMu8:
    case XED_IFORM_PREFETCH_EXCLUSIVE_MEMmprefetch:
    case XED_IFORM_PREFETCH_RESERVED_0F0Dr4:
    case XED_IFORM_PREFETCH_RESERVED_0F0Dr5:
    case XED_IFORM_PREFETCH_RESERVED_0F0Dr6:
    case XED_IFORM_PREFETCH_RESERVED_0F0Dr7:
      instr_op.is_effective_address = true;
      break;
    default: break;
  }

  switch (instr->category) {
    case XED_CATEGORY_CALL:
    case XED_CATEGORY_COND_BR:
    case XED_CATEGORY_UNCOND_BR:
      instr_op.width = arch::ADDRESS_WIDTH_BITS;
      break;
    default: break;
  }
}

// Add this branch target as an operand to the instruction `instr`.
void BranchTargetBuilder::Build(Instruction *instr) {
  auto &op(instr->ops[instr->num_explicit_ops++]);
  if (BRANCH_TARGET_LABEL == kind) {
    op.is_annotation_instr = true;
    op.annotation_instr = label;
    op.width = 32;
  } else {
    op.branch_target.as_pc = pc;
    op.width = arch::ADDRESS_WIDTH_BITS;
  }
  op.type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  op.rw = XED_OPERAND_ACTION_R;
  op.is_explicit = true;
}

}  // namespace arch
}  // namespace granary
