/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_XED2_INTEL64_BUILDER_H_
#define GRANARY_DRIVER_XED2_INTEL64_BUILDER_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/pc.h"
#include "granary/base/type_trait.h"

#include "granary/code/register.h"

#include "granary/driver/xed2-intel64/xed.h"

namespace granary {
namespace driver {

// Forward declarations.
class Instruction;

// Builder for a register operand.
class RegisterBuilder {
 public:
  RegisterBuilder(const RegisterBuilder &that) = default;
  RegisterBuilder(RegisterBuilder &&that) = default;

  inline RegisterBuilder(xed_reg_enum_t reg_, xed_operand_action_enum_t action_)
      : action(action_) {
    reg.DecodeFromNative(reg_);
  }

  // Add this register as an operand to the instruction `instr`.
  void Build(Instruction *instr);

 private:
  VirtualRegister reg;
  xed_operand_action_enum_t action;
};

// Builder for an immediate operand.
class ImmediateBuilder {
 public:
  ImmediateBuilder(const ImmediateBuilder &that) = default;
  ImmediateBuilder(ImmediateBuilder &&that) = default;

  template <typename T, typename EnableIf<IsUnsignedInteger<T>::RESULT>::Type=0>
  inline ImmediateBuilder(T as_uint_, xed_encoder_operand_type_t type_)
      : as_uint(static_cast<uintptr_t>(as_uint_)),
        type(type_) {}

  template <typename T, typename EnableIf<IsSignedInteger<T>::RESULT>::Type=0>
  inline ImmediateBuilder(T as_int_, xed_encoder_operand_type_t type_)
      : as_int(static_cast<intptr_t>(as_int_)),
        type(type_) {}

  // Add this immediate as an operand to the instruction `instr`.
  void Build(Instruction *instr);

 private:
  union {
    uintptr_t as_uint;
    intptr_t as_int;
  };
  xed_encoder_operand_type_t type;
};

// Builder for a memory operand.
class MemoryBuilder {
 public:
  MemoryBuilder(const MemoryBuilder &that) = default;
  MemoryBuilder(MemoryBuilder &&that) = default;

  inline MemoryBuilder(VirtualRegister reg_,
                       xed_operand_action_enum_t action_)
      : reg(reg_),
        action(action_),
        is_ptr(false) {}

  inline MemoryBuilder(xed_reg_enum_t reg_,
                       xed_operand_action_enum_t action_)
      : action(action_),
        is_ptr(false)  {
    reg.DecodeFromNative(reg_);
  }

  inline MemoryBuilder(const void *ptr_,
                       xed_operand_action_enum_t action_)
      : ptr(ptr_),
        action(action_),
        is_ptr(true)  {}

  // Add this memory as an operand to the instruction `instr`.
  void Build(Instruction *instr);

 private:
  union {
    VirtualRegister reg;
    const void *ptr;
  };
  xed_operand_action_enum_t action;
  bool is_ptr;
};

// Builder for a branch target.
class BranchTargetBuilder {
 public:
  BranchTargetBuilder(const BranchTargetBuilder &that) = default;
  BranchTargetBuilder(BranchTargetBuilder &&that) = default;

  inline explicit BranchTargetBuilder(PC pc_)
      : pc(pc_) {}

  template <typename RetT, typename... ArgsT>
  inline explicit BranchTargetBuilder(RetT (*pc_)(ArgsT...))
      : pc(UnsafeCast<PC>(pc_)) {}

  // Add this branch target as an operand to the instruction `instr`.
  void Build(Instruction *instr);

 private:
  PC pc;
};

// Initialize an emptry Granary `driver::Instruction` from a XED iclass,
// category, and the number of explicit operands.
void BuildInstruction(Instruction *instr, xed_iclass_enum_t iclass,
                      xed_category_enum_t category, uint8_t num_explicit_ops);

}  // namespace driver
}  // namespace granary

// Bring in the auto-generated instruction builder API.
#include "generated/xed2-intel64/instruction_builder.cc"

#endif  // GRANARY_DRIVER_XED2_INTEL64_BUILDER_H_
