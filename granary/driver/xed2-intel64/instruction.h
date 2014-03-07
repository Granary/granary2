/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_XED2_INTEL64_INSTRUCTION_H_
#define GRANARY_DRIVER_XED2_INTEL64_INSTRUCTION_H_

#include "granary/base/base.h"
#include "granary/base/pc.h"

#include "granary/driver/xed2-intel64/xed.h"

#include "granary/code/register.h"

namespace granary {

// Forward declaration.
class Operand;   // For `granary/operand/operand.h`.

namespace driver {

class Operand {
 public:
  Operand(void)
      : type(XED_ENCODER_OPERAND_TYPE_INVALID),
        width(0),
        rw(XED_OPERAND_ACTION_INVALID),
        is_sticky(false) {}

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

  union {
    // Branch target.
    union {
      intptr_t as_int;
      uintptr_t as_uint;
      PC as_pc;
      AppPC as_app_pc;
      CachePC as_cache_pc;
    } branch_target;

    // Immediate constant.
    union {
      intptr_t as_int;
      uintptr_t as_uint;
    } imm;

    // Direct memory address.
    union {
      const void *as_ptr;
      intptr_t as_int;
      uintptr_t as_uint;
      PC as_pc;
    } addr;

    // Register. If this is a memory operand then this implies a de-reference
    // of this register.
    VirtualRegister reg;
  } __attribute__((packed));

  xed_encoder_operand_type_t type:8;
  int8_t width;  // Operand width in bits.
  xed_operand_action_enum_t rw:8;  // Readable, writable, etc.
  bool is_sticky;  // This operand cannot be changed.

} __attribute__((packed));

static_assert(sizeof(Operand) <= 16,
    "Invalid structure packing of `granary::driver::Operand`.");

// Represents a high-level API to the XED encoder/decoder. This API represents
// instructions at the granularity of instruction classes, and supports
// de-selection of `xed_decoded_inst_t` to `Instruction` and selections
// of `Instruction` to `xed_encoder_request_t`.
//
// An interesting side-effect of using virtual register operands is that
// instructions have no real "length". That is, it is only when the virtual
// register pass has replaced everything and these IR instructions have been
// lowered into `xed_encoder_request_t` (and then to `xed_decoded_inst_t`) that
// the length of a particular instruction becomes meaningful.
class Instruction {
 public:
  Instruction(void);
  Instruction(const Instruction &that);

  // Get the decoded length of this instruction.
  inline int DecodedLength(void) const {
    return static_cast<int>(decoded_length);
  }

  // Get the PC-relative branch target.
  inline PC BranchTarget(void) const {
    return ops[0].branch_target.as_pc;  // TODO(pag): CALL_/JMP_FAR
  }

  // Set the PC-relative branch target.
  inline void SetBranchTarget(PC pc) {
    ops[0].branch_target.as_pc = pc;
  }

  inline bool IsFunctionCall(void) const {
    return XED_CATEGORY_CALL == category;
  }

  inline bool IsFunctionReturn(void) const {
    return XED_ICLASS_RET_FAR == iclass || XED_ICLASS_RET_NEAR == iclass;
  }

  inline bool IsInterruptCall(void) const {
    return XED_CATEGORY_INTERRUPT == category;
  }

  inline bool IsInterruptReturn(void) const {
    return XED_ICLASS_IRET == iclass || XED_ICLASS_IRETD == iclass ||
           XED_ICLASS_IRETQ == iclass;
  }

  inline bool IsSystemCall(void) const {
    return XED_CATEGORY_SYSCALL == category;
  }

  inline bool IsSystemReturn(void) const {
    return XED_CATEGORY_SYSRET == category;
  }

  inline bool IsConditionalJump(void) const {
    return XED_CATEGORY_COND_BR == category;
  }

  inline bool IsUnconditionalJump(void) const {
    // TODO(pag): XABORT is included in this op category.
    return XED_CATEGORY_UNCOND_BR == category;
  }

  inline bool IsJump(void) const {
    return IsUnconditionalJump() || IsConditionalJump();
  }

  // Returns true if this instruction is a control-flow instruction with an
  // indirect target.
  bool HasIndirectTarget(void) const;

  inline AppPC GetAppPC(void) const {
    return decoded_pc;
  }

  inline bool IsNoOp(void) const {
    return XED_CATEGORY_NOP == category;
  }

  // Get the opcode name.
  const char *OpCodeName(void) const;

  // Invoke a function on every operand.
  void ForEachOperand(std::function<void(granary::Operand *)> func);

  // Where was this instruction encoded/decoded.
  union {
    AppPC decoded_pc;
    uintptr_t decoded_addr;
  } __attribute__((packed));

  // Instruction class. This roughly corresponds to an opcode.
  xed_iclass_enum_t iclass:16;
  xed_category_enum_t category:8;

  // Decoded length of this instruction, or 0 if it wasn't decoded.
  uint8_t decoded_length;

  // Instruction prefixes.
  //
  // TODO(pag): Remove branch hints? Might be needed for special non-
  //            control-flow instructions.
  bool has_prefix_rep:1;
  bool has_prefix_repne:1;
  bool has_prefix_lock:1;
  bool has_prefix_br_hint_taken:1;
  bool has_prefix_br_hint_not_taken:1;

  // Is this an atomic operation?
  bool is_atomic:1;

  // Does this instruction have a memory operand?
  bool has_memory_op:1;

  // Number of explicit operands.
  uint8_t num_explicit_ops:4;

  // Total number of operands.
  uint8_t num_ops:4;

  // The effective operand width at decode time, or -1 if unknown.
  int8_t effective_operand_width;

  // Explicit operands. The order between these and those referenced via
  // `xed_inst_t` is maintained.
  Operand ops[XED_ENCODER_OPERANDS_MAX];

} __attribute__((packed));

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_XED2_INTEL64_INSTRUCTION_H_
