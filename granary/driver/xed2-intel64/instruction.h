/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_XED2_INTEL64_INSTRUCTION_H_
#define GRANARY_DRIVER_XED2_INTEL64_INSTRUCTION_H_

#include "granary/base/base.h"
#include "granary/base/new.h"
#include "granary/base/types.h"

#include "granary/driver/xed2-intel64/xed.h"

namespace granary {
namespace driver {

// Basically a `xed_encoder_operand_t`, but where if the instruction contains
// a PC-relative operand (e.g. CALL/JMP/Jcc/LEA) then we maintain the resolved
// address in `rel_imm`/`rel_pc`.
class Operand {
 public:
  inline bool IsRead(void) const {
    return xed_operand_action_read(rw);
  }

  inline bool IsWrite(void) const {
    return xed_operand_action_written(rw);
  }

  inline bool IsConditionalRead(void) const {
    return xed_operand_action_conditional_read(rw);
  }

  inline bool IsConditionalWrite(void) const {
    return xed_operand_action_conditional_write(rw);
  }

  xed_encoder_operand_type_t type;
  union {
    decltype(xed_encoder_operand_t().u) u;
    union {
      intptr_t imm;
      PC pc;
      AppPC app_pc;
      CachePC cache_pc;
    } __attribute__((packed)) rel;
  } __attribute__((packed));
  xed_uint32_t width;  // Operand width in bits.

  // Extra field.
  xed_operand_action_enum_t rw;
};

// Make sure these two fields overlap so that we can treat relative immediates
// (signed) just like immediates (unsigned).
static_assert(offsetof(Operand, rel.imm) == offsetof(Operand, u.imm0),
    "Bad packing of `Operand::rel::imm`. Does not overlap `Operand::u::imm0`.");
static_assert(offsetof(Operand, rel.imm) == offsetof(Operand, rel.pc),
    "Bad packing of `Operand::rel::imm`. Does not overlap `Operand::rel::pc`.");

// Represents a high-level API to the XED encoder/decoder. This API represents
// instructions at the granularity of instruction classes, and supports
// de-selection of `xed_decoded_inst_t` to `Instruction` and selections
// of `Instruction` to `xed_encoder_request_t`.
class Instruction {
 public:
  PC BranchTarget(void) const;
  void SetBranchTarget(PC);

  bool IsFunctionCall(void) const;
  bool IsFunctionReturn(void) const;
  bool IsInterruptCall(void) const;
  bool IsInterruptReturn(void) const;
  bool IsSystemCall(void) const;
  bool IsSystemReturn(void) const;
  bool IsJump(void) const;
  bool IsUnconditionalJump(void) const;
  bool IsConditionalJump(void) const;
  bool HasIndirectTarget(void) const;

  inline AppPC GetAppPC(void) const {
    return decoded_pc;
  }

  int Length(void) const;
  bool IsNoOp(void) const;

  // Instruction class. This roughly corresponds to an opcode.
  xed_iclass_enum_t iclass;
  xed_category_enum_t category;
  xed_encoder_prefixes_t prefixes;

  // Length of the instruction in bytes. Invalid if `needs_encoding` is true.
  int8_t length;

  // Number of explicit operands.
  int8_t num_ops;

  // This instruction has been changed since being decoded, or was created and
  // so has never been encoded/decoded.
  bool needs_encoding:1;

  // Does this instruction have a PC-relative operand? This is used by
  // `granary/drivers/xed2-intel64/relativize.cc` to more quickly figure out
  // if an instruction is of interest.
  //
  // This behaves a bit like `needs_encoding`, because if we have a PC-relative
  // operand then it's treated as requiring relativization.
  bool has_pc_rel_op:1;

  // Does this instruction have a fixed, known length?
  bool has_fixed_length:1;

  // Is this an atomic operation?
  bool is_atomic:1;

  // Does this instruction have a memory operand?
  bool has_memory_op:1;

  // Does this instruction have a virtual register operand?
  bool has_virtual_reg_op:1;

  // Raw bytes of this instruction. When decoding an instruction, we copy the
  // decoded bytes into here. When encoding an instruction, we overwrite these
  // bytes.
  uint8_t encode_buffer[XED_MAX_INSTRUCTION_BYTES];

  // Explicit operands. The order between these and those referenced via
  // `xed_inst_t` is maintained.
  Operand ops[XED_ENCODER_OPERANDS_MAX];

  // Where was this instruction encoded/decoded.
  union {
    AppPC decoded_pc;
    CachePC encoded_pc;
    PC pc;
  } __attribute__((packed));

  GRANARY_DEFINE_NEW_ALLOCATOR(Instruction, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })
} __attribute__((packed));

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_XED2_INTEL64_INSTRUCTION_H_
