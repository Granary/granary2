/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_XED2_INTEL64_OPERAND_H_
#define GRANARY_DRIVER_XED2_INTEL64_OPERAND_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/pc.h"

#include "granary/cfg/operand.h"  // For `OperandString`.
#include "granary/code/register.h"  // For `VirtualRegister`.

#include "granary/driver/operand.h"
#include "granary/driver/xed2-intel64/xed.h"

namespace granary {

// Forward declarations.
class Operand;
class NativeInstruction;

namespace driver {

// Represents an operand to an x86-64 instruction.
class Operand : public OperandInterface {
 public:
  Operand(void)
      : type(XED_ENCODER_OPERAND_TYPE_INVALID),
        width(0),
        rw(XED_OPERAND_ACTION_INVALID),
        is_sticky(false) {}

  Operand(const Operand &op);

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

  inline bool IsRegister(void) const {
    return XED_ENCODER_OPERAND_TYPE_REG == type ||
           XED_ENCODER_OPERAND_TYPE_SEG0 == type ||
           XED_ENCODER_OPERAND_TYPE_SEG1 == type;
  }

  inline bool IsMemory(void) const {
    return XED_ENCODER_OPERAND_TYPE_MEM == type ||
           XED_ENCODER_OPERAND_TYPE_PTR == type;
  }

  inline bool IsImmediate(void) const {
    return XED_ENCODER_OPERAND_TYPE_BRDISP == type ||
           XED_ENCODER_OPERAND_TYPE_IMM0 == type ||
           XED_ENCODER_OPERAND_TYPE_SIMM0 == type ||
           XED_ENCODER_OPERAND_TYPE_IMM1 == type;
  }

  void EncodeToString(OperandString *str) const;

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

    // Indirect register reference via a created LEA instruction.
    NativeInstruction *reg_indirect;

  } __attribute__((packed));

  xed_encoder_operand_type_t type:8;
  int8_t width;  // Operand width in bits.
  xed_operand_action_enum_t rw:8;  // Readable, writable, etc.

  // This operand cannot be changed.
  bool is_sticky;

} __attribute__((packed));

static_assert(sizeof(Operand) <= 16,
    "Invalid structure packing of `granary::driver::Operand`.");

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_XED2_INTEL64_OPERAND_H_
