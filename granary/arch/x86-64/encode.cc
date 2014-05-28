/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/base.h"
#include "granary/arch/encode.h"
#include "granary/arch/util.h"
#include "granary/arch/x86-64/instruction.h"

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Decoder state that sets the mode to 64-bit.
extern xed_state_t XED_STATE;

// Encode t an instruction IR at `*pc` and update `pc`.
bool InstructionEncoder::EncodeNext(Instruction *instr, CachePC *pc) {
  *pc = EncodeInternal(instr, *pc);
  return nullptr != *pc;
}

// Encode an instruction IR into an x86 instruction.
bool InstructionEncoder::Encode(Instruction *instr, CachePC pc) {
  return nullptr != EncodeInternal(instr, pc);
}

namespace {

// Initialize the XED encoding IR from some of the basic info stored in
// Granary's instruction IR.
static void InitEncoderInstruction(const Instruction *instr,
                                   xed_encoder_instruction_t *xede) {
  memset(xede, 0, sizeof *xede);
  xede->mode = XED_STATE;
  xede->iclass = instr->iclass;
  xede->effective_operand_width = 0;
  if (-1 < instr->effective_operand_width) {
    xede->effective_operand_width = static_cast<uint32_t>(
        instr->effective_operand_width);
  }
  xede->effective_address_width = arch::ADDRESS_WIDTH_BITS;
  xede->noperands = instr->num_explicit_ops;
  xede->prefixes.i = 0;
  xede->prefixes.s.lock = instr->has_prefix_lock;
  xede->prefixes.s.rep = instr->has_prefix_rep;
  xede->prefixes.s.repne = instr->has_prefix_repne;
}

// Encode a branch displacement operand.
static void EncodeBrDisp(const Operand &op, xed_encoder_operand_t *xedo,
                         CachePC next_pc) {
  auto target = op.branch_target.as_int;
  auto next_addr = reinterpret_cast<intptr_t>(next_pc);
  xedo->type = op.type;
  xedo->width = 32;
  xedo->u.brdisp = static_cast<int32_t>(target - next_addr);
}

// Encode a register operand.
static void EncodeReg(const Operand &op, xed_encoder_operand_t *xedo) {
  xedo->type = op.type;
  xedo->u.reg = static_cast<xed_reg_enum_t>(op.reg.EncodeToNative());
}

// Encode an immediate operand.
static void EncodeImm(const Operand &op, xed_encoder_operand_t *xedo) {
  xedo->type = op.type;
  if (XED_ENCODER_OPERAND_TYPE_IMM1 == op.type) {
    xedo->u.imm1 = static_cast<decltype(xedo->u.imm1)>(op.imm.as_uint);
  } else {
    xedo->u.imm0 = op.imm.as_uint;
  }
}

// Truncate the displacement value according to the computed width of the
// displacement.
static void TruncateDisplacementToWidth(xed_enc_displacement_t *disp) {
  switch (disp->displacement_width) {
    case 32:
      disp->displacement &= 0xFFFFFFFFULL;
      break;
    case 16:
      disp->displacement &= 0xFFFFULL;
      break;
    case 8:
      disp->displacement &= 0xFFULL;
      break;
    default: return;
  }
}

// Encode a memory operand.
static void EncodeMem(const Operand &op, xed_encoder_operand_t *xedo) {
  xedo->type = op.type;
  xedo->u.mem.seg = op.segment;
  if (op.is_compound) {
    xedo->u.mem.base = op.mem.reg_base;
    xedo->u.mem.disp.displacement = static_cast<uint64_t>(
        static_cast<int64_t>(op.mem.disp));
    if (xedo->u.mem.disp.displacement) {
      xedo->u.mem.disp.displacement_width = static_cast<uint32_t>(
          ImmediateWidthBits(op.mem.disp));
    } else {
      TruncateDisplacementToWidth(&(xedo->u.mem.disp));
    }
    xedo->u.mem.index = op.mem.reg_index;
    xedo->u.mem.scale = op.mem.scale;
  } else {
    xedo->u.mem.base = static_cast<xed_reg_enum_t>(op.reg.EncodeToNative());
  }
}

// Encode a pointer memory operand.
static void EncodePtr(const Operand &op, xed_encoder_operand_t *xedo,
                      CachePC next_pc) {
  // Absolute address, or segment offset.
  if (XED_REG_INVALID != op.segment) {
    xedo->type = XED_ENCODER_OPERAND_TYPE_MEM;
    xedo->u.mem.seg = XED_REG_DS == op.segment ? XED_REG_INVALID : op.segment;
    xedo->u.mem.disp.displacement = static_cast<uint64_t>(
        static_cast<int64_t>(op.mem.disp));
    xedo->u.mem.disp.displacement_width = static_cast<uint32_t>(
        ImmediateWidthBits(op.mem.disp));
    TruncateDisplacementToWidth(&(xedo->u.mem.disp));

  // RIP-relative address.
  } else {
    xedo->type = XED_ENCODER_OPERAND_TYPE_PTR;
    auto next_addr = reinterpret_cast<intptr_t>(next_pc);
    intptr_t mem_addr = 0;
    if (op.is_annot_encoded_pc) {
      mem_addr = static_cast<intptr_t>(op.ret_address->data);
    } else {
      mem_addr = op.addr.as_int;
    }

    xedo->u.brdisp = static_cast<int32_t>(mem_addr - next_addr);
  }
}

}  // namespace

// Encode a XED instruction intermediate representation into an x86
// instruction, and return the address of the next memory location into which
// an instruction can be encoded.
CachePC InstructionEncoder::EncodeInternal(Instruction *instr, CachePC pc) {
  xed_encoder_instruction_t xede;

  // Step 1: Convert Granary IR into XED encoder IR.
  InitEncoderInstruction(instr, &xede);
  auto op_index = 0;

  // Ensure that we're always stage-encoding before encoding. Stage encoding
  // is used to compute the length of every instruction, as well as to ensure
  // that every instruction can indeed be encoded.
  instr->encoded_pc = pc;
  if (InstructionEncodeKind::STAGED == encode_kind) {
    instr->encoded_length = 0;
  } else {
    GRANARY_ASSERT(0 < instr->encoded_length);
  }

  for (auto &op : instr->ops) {
    auto &xedo(xede.operands[op_index++]);
    switch (op.type) {
      case XED_ENCODER_OPERAND_TYPE_BRDISP:
        EncodeBrDisp(op, &xedo, pc + instr->encoded_length);
        break;
      case XED_ENCODER_OPERAND_TYPE_REG:
        EncodeReg(op, &xedo);
        break;
      case XED_ENCODER_OPERAND_TYPE_IMM0:
      case XED_ENCODER_OPERAND_TYPE_SIMM0:
      case XED_ENCODER_OPERAND_TYPE_IMM1:
        EncodeImm(op, &xedo);
        break;
      case XED_ENCODER_OPERAND_TYPE_MEM:
        EncodeMem(op, &xedo);
        break;
      case XED_ENCODER_OPERAND_TYPE_PTR:
        EncodePtr(op, &xedo, pc + instr->encoded_length);
        break;
      case XED_ENCODER_OPERAND_TYPE_INVALID:
      default:
        break;
    }
    if (!xedo.width) {
      xedo.width = static_cast<uint32_t>(std::max<int>(0, op.BitWidth()));
    }
  }

  xed_state_t dstate;
  xed_encoder_request_t enc_req;

  // Step 2: Convert XED encoder IR into XED decoder IR.
  dstate = XED_STATE;
  xed_encoder_request_zero_set_mode(&enc_req, &dstate);
  GRANARY_IF_DEBUG( auto ret = ) xed_convert_to_encoder_request(
      &enc_req, &xede);
  GRANARY_ASSERT(ret);

  // Step 3: Convert XED decoder IR into x86.
  uint8_t itext[XED_MAX_INSTRUCTION_BYTES] = {0};
  unsigned encoded_length = 0;
  GRANARY_IF_DEBUG( auto xed_error = ) xed_encode(
      &enc_req, itext, XED_MAX_INSTRUCTION_BYTES, &encoded_length);
  GRANARY_ASSERT(XED_ERROR_NONE == xed_error);

  instr->encoded_length = static_cast<uint8_t>(encoded_length);
  return pc + instr->encoded_length;
}

}  // namespace arch
}  // namespace granary
