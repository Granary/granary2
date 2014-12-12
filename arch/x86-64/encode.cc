/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/base.h"
#include "arch/encode.h"
#include "arch/util.h"
#include "arch/x86-64/instruction.h"

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
  if (instr->effective_operand_width) {
    xede->effective_operand_width = static_cast<uint32_t>(
        instr->effective_operand_width);
  }

  // Limit the effective operand width for instructions using AGENs.
  switch (instr->iform) {
    case XED_IFORM_BNDCN_BND_AGEN:
    case XED_IFORM_BNDCL_BND_AGEN:
    case XED_IFORM_BNDCU_BND_AGEN:
    case XED_IFORM_BNDMK_BND_AGEN:
    case XED_IFORM_LEA_GPRv_AGEN:
      xede->effective_operand_width = std::min(
          static_cast<uint32_t>(arch::ADDRESS_WIDTH_BITS),
          xede->effective_operand_width);
      break;
    default: break;
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
  if (op.is_annotation_instr) {
    target = op.annotation_instr->Data<intptr_t>();
  } else {
    target = op.branch_target.as_int;
  }
  xedo->type = op.type;
  const auto brdisp_64 = target - next_addr;
  const auto brdisp_32 = static_cast<int32_t>(brdisp_64);

  GRANARY_ASSERT(!check_reachable || (brdisp_32 == brdisp_64));
  GRANARY_ASSERT(!check_reachable || (0 <= brdisp_32 || -5 > brdisp_32));

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

// Encode a memory operand.
static void EncodeMem(const Operand &op, xed_encoder_operand_t *xedo,
                      xed_iclass_enum_t iclass) {
  GRANARY_ASSERT(!op.is_annotation_instr);
  xedo->type = op.type;
  xedo->u.mem.seg = XED_REG_DS != op.segment ? op.segment : XED_REG_INVALID;
  if (op.is_compound) {
    if (op.mem.base.IsValid()) {
      xedo->u.mem.base = static_cast<xed_reg_enum_t>(
          op.mem.base.EncodeToNative());
    }
    if (op.mem.index.IsValid()) {
      GRANARY_ASSERT(0 != op.mem.scale);
      xedo->u.mem.index = static_cast<xed_reg_enum_t>(
          op.mem.index.EncodeToNative());
    }
    xedo->u.mem.scale = op.mem.scale;
    if (op.mem.disp) {
      xedo->u.mem.disp.displacement = static_cast<uint64_t>(op.mem.disp);
      auto width = ImmediateWidthBits(op.mem.disp);
      width = 16 == width ? 32 : std::min(32, width);
      xedo->u.mem.disp.displacement_width = static_cast<uint32_t>(width);
    }

    if (!xedo->u.mem.base && xedo->u.mem.index && 1 == xedo->u.mem.scale) {
      xedo->u.mem.base = xedo->u.mem.index;
      xedo->u.mem.index = XED_REG_INVALID;
    }

    if (!xedo->u.mem.index) {
      if (!xedo->u.mem.disp.displacement) {
        xedo->u.mem.disp.displacement_width = 0;
      }
    } else if (!xedo->u.mem.base) {
      xedo->u.mem.disp.displacement_width = 32;
    }

  } else {
    xedo->u.mem.base = static_cast<xed_reg_enum_t>(op.reg.EncodeToNative());
  }
  if (op.is_effective_address) {
    if (XED_ICLASS_LEA == iclass) {
      xedo->width = arch::ADDRESS_WIDTH_BITS;
    } else if (!xedo->width) {
      xedo->width = 8;
    }
  }
}

// Encode a pointer memory operand.
static void EncodePtr(const Operand &op, xed_encoder_operand_t *xedo,
                      CachePC next_pc) {
  xedo->type = XED_ENCODER_OPERAND_TYPE_MEM;
  auto next_addr = reinterpret_cast<intptr_t>(next_pc);

  // Segment offset.
  if (XED_REG_INVALID != op.segment && XED_REG_DS != op.segment) {
    GRANARY_ASSERT(!op.is_annotation_instr);
    GRANARY_ASSERT(op.addr.as_int == static_cast<int32_t>(op.addr.as_uint));
    xedo->u.mem.disp.displacement = op.addr.as_uint;
    if (op.addr.as_int >= 0) { // Unsigned, apply a 31-bit mask.
      xedo->u.mem.disp.displacement &= 0x7FFFFFFFULL;
    }
    xedo->u.mem.disp.displacement_width = 32;
    xedo->u.mem.seg = op.segment;

  // RIP-relative address.
  } else if (op.is_annotation_instr) {
    auto addr = op.annotation_instr->Data<intptr_t>();
    xedo->u.mem.disp.displacement = static_cast<uint32_t>(addr - next_addr);
    xedo->u.mem.disp.displacement_width = 32;
    xedo->u.mem.base = XED_REG_RIP;

  // Hard-coded address.
  } else {
    auto mem_addr = op.addr.as_uint;
    xedo->u.mem.disp.displacement = mem_addr;
    xedo->u.mem.disp.displacement_width = ADDRESS_WIDTH_BITS;

    auto high_32 = mem_addr >> 32;
    auto sign_bit_32 = 1UL & (mem_addr >> 31);

    // Make sure we can sign-extend it.
    if (0x0FFFFFFFFULL == high_32) {
      if (sign_bit_32) xedo->u.mem.disp.displacement_width = 32;
    } else if (!high_32) {
      if (!sign_bit_32) xedo->u.mem.disp.displacement_width = 32;
    }

    // Convert into a RIP-relative displacement when it fits.
    //
    // TODO(pag): Mask high order bits if 32 bits? For segment offsets, this
    //            doesn't seem to matter.
    if (ADDRESS_WIDTH_BITS == xedo->u.mem.disp.displacement_width &&
      AddrIsOffsetReachable(next_addr, op.addr.as_int)) {
      auto diff = op.addr.as_int - next_addr;
      xedo->u.mem.disp.displacement = static_cast<uint32_t>(diff);
      xedo->u.mem.disp.displacement_width = 32;
      xedo->u.mem.base = XED_REG_RIP;
    }
  }
  if (op.is_effective_address) {
    xedo->width = std::min(static_cast<uint32_t>(arch::ADDRESS_WIDTH_BITS),
                           xedo->width);
  }
}

// Encode the operands of the instruction.
static void EncodeOperands(const Instruction *instr,
                           xed_encoder_instruction_t *xede, CachePC pc
                           GRANARY_IF_DEBUG(, bool check_reachable)) {
  auto op_width = 0U;
  for (uint16_t op_index = 0; op_index < instr->num_explicit_ops; ++op_index) {
    const auto &op(instr->ops[op_index]);
    auto &xedo(xede->operands[op_index]);
    xedo.width = static_cast<uint32_t>(std::max(0UL, op.BitWidth()));

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
        EncodeMem(op, &xedo, instr->iclass);
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
    op_width = std::max(op_width, xedo.width);
  }

  // Make sure that we've got an effective operand width.
  if (GRANARY_UNLIKELY(!instr->effective_operand_width && op_width)) {
    xede->effective_operand_width = op_width;
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
    case XED_IFORM_PUSHF:
    case XED_IFORM_PUSHFQ:
      xede->noperands = 0;
      break;
    default:
      break;
  }
}

// Try to atomically write an instruction into the code cache.
static void AtomicCommit(CachePC pc, uint8_t itext[], uint8_t len) {
  GRANARY_ASSERT(8 >= len);
  uint64_t old_itext_val(0);
  memcpy(&old_itext_val, pc, 8);
  memcpy(&old_itext_val, itext, len);
  std::atomic_thread_fence(std::memory_order_acquire);
  *reinterpret_cast<uint64_t *>(pc) = old_itext_val;
  std::atomic_thread_fence(std::memory_order_release);
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

  // Step 1: Convert Granary IR into XED encoder IR.
  InitEncoderInstruction(instr, &xede);
  EncodeOperands(instr, &xede, pc GRANARY_IF_DEBUG(, !is_stage_encoding));
  EncodeSpecialCases(instr, &xede);

  // Ensure that we're always stage-encoding before encoding. Stage encoding
  // is used to compute the length of every instruction, as well as to ensure
  // that every instruction can indeed be encoded.
  instr->encoded_pc = pc;
  if (is_stage_encoding) {
    instr->encoded_length = 0;
  } else {
    GRANARY_ASSERT(0 < instr->encoded_length);
  }

  xed_encoder_request_t enc_req;

  // Step 2: Convert XED encoder IR into XED decoder IR.
  memset(&enc_req, 0, sizeof enc_req);
  xed_encoder_request_zero_set_mode(&enc_req, &XED_STATE);

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
  } else if (InstructionEncodeKind::COMMIT_ATOMIC == encode_kind) {
    AtomicCommit(pc, &(itext[0]), instr->encoded_length);
  }
  return pc + instr->encoded_length;
}

}  // namespace arch
}  // namespace granary
