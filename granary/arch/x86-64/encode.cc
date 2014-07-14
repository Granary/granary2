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
                         CachePC next_pc, xed_iclass_enum_t iclass
                         GRANARY_IF_DEBUG(, bool check_reachable)) {
  intptr_t target = 0;
  auto next_addr = reinterpret_cast<intptr_t>(next_pc);
  if (op.is_annot_encoded_pc) {
    target = static_cast<intptr_t>(op.ret_address->data);
  } else {
    target = op.branch_target.as_int;
  }
  xedo->type = op.type;
  const auto brdisp_64 = target - next_addr;
  const auto brdisp_32 = static_cast<int32_t>(brdisp_64);

  GRANARY_ASSERT(!check_reachable || brdisp_32 == brdisp_64);

  switch (iclass) {
    case XED_ICLASS_JRCXZ:
    case XED_ICLASS_LOOP:
    case XED_ICLASS_LOOPE:
    case XED_ICLASS_LOOPNE:
      xedo->width = 8;
      xedo->u.brdisp = static_cast<int8_t>(brdisp_32);
      GRANARY_ASSERT(!check_reachable || xedo->u.brdisp == brdisp_32);
      break;

    default:
      xedo->width = 32;
      xedo->u.brdisp = brdisp_32;
      break;
  }
}

// Encode a register operand.
static void EncodeReg(const Operand &op, xed_encoder_operand_t *xedo) {
  xedo->type = op.type;
  xedo->u.reg = static_cast<xed_reg_enum_t>(op.reg.EncodeToNative());
}

// Encode an immediate operand.
static void EncodeImm(const Operand &op, xed_encoder_operand_t *xedo,
                      xed_iclass_enum_t iclass) {
  xedo->type = op.type;
  if (XED_ENCODER_OPERAND_TYPE_IMM1 == op.type) {
    xedo->u.imm1 = static_cast<decltype(xedo->u.imm1)>(op.imm.as_uint);
  } else {
    xedo->u.imm0 = op.imm.as_uint;
  }
  if (XED_ICLASS_PUSH == iclass) {
    xedo->width = static_cast<uint32_t>(ImmediateWidthBits(op.imm.as_uint));
    if (16 == xedo->width) {
      xedo->width = 32;
    }
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
    if (op.mem.disp) {
      xedo->u.mem.disp.displacement = static_cast<uint64_t>(op.mem.disp);
      auto width = ImmediateWidthBits(op.mem.disp);
      width = 16 == width ? 32 : std::min(32, width);
      xedo->u.mem.disp.displacement_width = static_cast<uint32_t>(width);
      TruncateDisplacementToWidth(&(xedo->u.mem.disp));
    }
    xedo->u.mem.index = op.mem.reg_index;
    xedo->u.mem.scale = op.mem.scale;

    if (XED_REG_INVALID == xedo->u.mem.base &&
        XED_REG_INVALID != xedo->u.mem.index) {
      xedo->u.mem.disp.displacement_width = 32;
    }
  } else {
    xedo->u.mem.base = static_cast<xed_reg_enum_t>(op.reg.EncodeToNative());
  }
}

// Encode a pointer memory operand.
static void EncodePtr(const Operand &op, xed_encoder_operand_t *xedo,
                      CachePC next_pc) {
  xedo->type = XED_ENCODER_OPERAND_TYPE_MEM;
  auto next_addr = reinterpret_cast<intptr_t>(next_pc);

  // Segment offset.
  if (XED_REG_INVALID != op.segment && XED_REG_DS != op.segment) {
    xedo->u.mem.disp.displacement = op.addr.as_uint;
    if (op.addr.as_int >= 0) { // Unsigned, apply a 31-bit mask.
      xedo->u.mem.disp.displacement &= 0x7FFFFFFFULL;
    }
    xedo->u.mem.disp.displacement_width = 32;
    xedo->u.mem.seg = op.segment;

  // RIP-relative address.
  } else  if (op.is_annot_encoded_pc) {
    auto addr = static_cast<intptr_t>(op.ret_address->data);
    xedo->u.mem.disp.displacement = static_cast<uint32_t>(addr - next_addr);
    xedo->u.mem.disp.displacement_width = 32;
    xedo->u.mem.base = XED_REG_RIP;

  // Hard-coded address.
  } else {
    auto mem_addr = op.addr.as_uint;
    xedo->u.mem.disp.displacement = mem_addr;
    xedo->u.mem.disp.displacement_width = arch::ADDRESS_WIDTH_BITS;

    auto high_32 = mem_addr >> 32;
    auto sign_bit_32 = 1UL & (mem_addr >> 31);

    if (0x0FFFFFFFFULL == high_32) {
      if (sign_bit_32) {
        xedo->u.mem.disp.displacement_width = 32;
      }
    } else if (!high_32) {
      if (!sign_bit_32) {
        xedo->u.mem.disp.displacement_width = 32;
      }
    }
    if (32 == xedo->u.mem.disp.displacement_width) {
      xedo->u.mem.disp.displacement &= 0x0FFFFFFFFULL;  // 32 bit mask.

    // Convert into a RIP-relative displacement when it fits.
    } else if (64 == xedo->u.mem.disp.displacement_width) {
      auto diff = op.addr.as_int - next_addr;
      if (32 >= ImmediateWidthBits(diff)) {
        xedo->u.mem.disp.displacement = static_cast<uint32_t>(diff);
        xedo->u.mem.disp.displacement_width = 32;
        xedo->u.mem.base = XED_REG_RIP;
      }
    }
  }
}

// Perform late-mangling of an LEA instruction.
static void LateMangleLEA(Instruction *instr) {
  GRANARY_ASSERT(3 == instr->num_explicit_ops);
  GRANARY_ASSERT(instr->ops[1].IsRegister());
  GRANARY_ASSERT(instr->ops[2].IsRegister());
  GRANARY_ASSERT(instr->ops[1].reg.IsNative());
  GRANARY_ASSERT(instr->ops[2].reg.IsNative());
  GRANARY_ASSERT(instr->ops[1].reg.IsGeneralPurpose());
  GRANARY_ASSERT(instr->ops[2].reg.IsGeneralPurpose());
  auto base_reg = static_cast<xed_reg_enum_t>(
      instr->ops[1].reg.EncodeToNative());
  auto index_reg = static_cast<xed_reg_enum_t>(
      instr->ops[2].reg.EncodeToNative());
  auto &op1(instr->ops[1]);
  instr->ops[2].type = XED_ENCODER_OPERAND_TYPE_INVALID;
  op1.type = XED_ENCODER_OPERAND_TYPE_MEM;
  op1.is_effective_address = true;
  op1.is_compound = true;
  op1.mem.disp = 0;
  op1.mem.reg_base = base_reg;
  op1.mem.reg_index = index_reg;
  op1.mem.scale = 1;
  instr->num_explicit_ops = 2;
}

// Encode the operands of the instruction.
static void EncodeOperands(const Instruction *instr,
                           xed_encoder_instruction_t *xede, CachePC pc
                           GRANARY_IF_DEBUG(, bool check_reachable)) {
  auto op_width = 0;
  auto op_index = 0;

  for (auto &op : instr->ops) {
    auto &xedo(xede->operands[op_index++]);
    xedo.width = static_cast<uint32_t>(std::max<int>(0, op.BitWidth()));
    op_width = std::max(op_width, op.BitWidth());
    switch (op.type) {
      case XED_ENCODER_OPERAND_TYPE_BRDISP:
        EncodeBrDisp(op, &xedo, pc + instr->encoded_length, instr->iclass
                     GRANARY_IF_DEBUG(, check_reachable));
        break;
      case XED_ENCODER_OPERAND_TYPE_REG:
        EncodeReg(op, &xedo);
        break;
      case XED_ENCODER_OPERAND_TYPE_IMM0:
      case XED_ENCODER_OPERAND_TYPE_SIMM0:
      case XED_ENCODER_OPERAND_TYPE_IMM1:
        EncodeImm(op, &xedo, instr->iclass);
        break;
      case XED_ENCODER_OPERAND_TYPE_MEM:
        EncodeMem(op, &xedo);
        break;
      case XED_ENCODER_OPERAND_TYPE_PTR:
        // TODO(pag): Do reachability checks in `EncodePtr`, as is
        //            done in `EncodeBrDisp`.
        EncodePtr(op, &xedo, pc + instr->encoded_length);
        break;
      case XED_ENCODER_OPERAND_TYPE_INVALID:
      default:
        break;
    }
  }

  // Make sure that we've got an effective operand width.
  if (GRANARY_UNLIKELY(0 >= instr->effective_operand_width && op_width)) {
    xede->effective_operand_width = static_cast<uint32_t>(op_width);
  }
}

// Special cases that aren't easily caught by the ambiguous operand matcher.
//
// Note: In the case of `IMUL`, it's trivial to specialize the below listed
//       iforms when generating `ambiguous_operands.cc`, but they are
//       intentionally not specialized so that we can see the second operands
//       as registers. If we didn't see the second operands as registers then
//       we might miss those dependencies when using only the iclass to find
//       the implicit operands.
static void EncodeSpecialCases(const Instruction *instr,
                               xed_encoder_instruction_t *xede) {
  switch (instr->iform) {
    case XED_IFORM_IMUL_GPR8:
    case XED_IFORM_IMUL_GPRv:
    case XED_IFORM_IMUL_MEMb:
    case XED_IFORM_IMUL_MEMv:
      xede->noperands = 1;
      break;
    default:
      break;
  }
}

}  // namespace

// Encode a XED instruction intermediate representation into an x86
// instruction, and return the address of the next memory location into which
// an instruction can be encoded.
CachePC InstructionEncoder::EncodeInternal(Instruction *instr, CachePC pc) {
  // Special case: some instructions exist only for their side-effects on the
  // virtual register system, or as stand-in instructions (e.g. for out-edge
  // templates).
  if (GRANARY_UNLIKELY(instr->dont_encode)) {
    instr->encoded_pc = pc;
    instr->encoded_length = 0;
    return pc;
  }

  xed_encoder_instruction_t xede;
  const auto is_stage_encoding = InstructionEncodeKind::STAGED == encode_kind;

  // Make sure that something like the `LEA` produced from mangling `XLAT` is
  // correctly handled.
  if (GRANARY_UNLIKELY(XED_ICLASS_LEA == instr->iclass &&
                       2 != instr->num_explicit_ops)) {
    LateMangleLEA(instr);
  }

  // Step 1: Convert Granary IR into XED encoder IR.
  InitEncoderInstruction(instr, &xede);
  EncodeOperands(instr, &xede, pc GRANARY_IF_DEBUG(, !is_stage_encoding));
  EncodeSpecialCases(instr, &xede);

  auto old_encoded_pc = instr->encoded_pc;

  // Ensure that we're always stage-encoding before encoding. Stage encoding
  // is used to compute the length of every instruction, as well as to ensure
  // that every instruction can indeed be encoded.
  instr->encoded_pc = pc;
  if (is_stage_encoding) {
    instr->encoded_length = 0;
  } else {
    GRANARY_ASSERT(0 < instr->encoded_length);
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

  if (InstructionEncodeKind::COMMIT == encode_kind) {
    memcpy(pc, &(itext[0]), instr->encoded_length);
  }
  GRANARY_USED(old_encoded_pc);
  return pc + instr->encoded_length;
}

}  // namespace arch
}  // namespace granary
