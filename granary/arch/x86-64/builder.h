/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_X86_64_BUILDER_H_
#define GRANARY_ARCH_X86_64_BUILDER_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/pc.h"
#include "granary/base/type_trait.h"

#include "granary/code/register.h"

#include "granary/arch/x86-64/instruction.h"

namespace granary {
namespace arch {

// Builder for a register operand.
class RegisterBuilder {
 public:
  RegisterBuilder(const RegisterBuilder &that) = default;
  RegisterBuilder(RegisterBuilder &&that) = default;

  inline RegisterBuilder(xed_reg_enum_t reg_, xed_operand_action_enum_t action_)
      : action(action_) {
    reg.DecodeFromNative(reg_);
  }

  inline RegisterBuilder(VirtualRegister reg_,
                         xed_operand_action_enum_t action_)
      : reg(reg_),
        action(action_) {}

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
        type(type_),
        width(-1) {}

  template <typename T, typename EnableIf<IsSignedInteger<T>::RESULT>::Type=0>
  inline ImmediateBuilder(T as_int_, xed_encoder_operand_type_t type_)
      : as_int(static_cast<intptr_t>(as_int_)),
        type(type_),
        width(-1) {}

  inline ImmediateBuilder(const Operand &that, xed_encoder_operand_type_t type_)
      : as_uint(that.imm.as_uint),
        type(type_),
        width(that.width) {}

  // Add this immediate as an operand to the instruction `instr`.
  void Build(Instruction *instr);

 private:
  union {
    uintptr_t as_uint;
    intptr_t as_int;
  };
  xed_encoder_operand_type_t type;
  int width;
};

// Builder for a memory operand.
class MemoryBuilder {
 public:
  MemoryBuilder(const MemoryBuilder &that) = default;
  MemoryBuilder(MemoryBuilder &&that) = default;

  inline MemoryBuilder(Operand op_, xed_operand_action_enum_t action_)
      : op(op_),
        action(action_),
        kind(BUILD_OPERAND) {}

  inline MemoryBuilder(VirtualRegister reg_, xed_operand_action_enum_t action_)
      : reg(reg_),
        action(action_),
        kind(BUILD_REGISTER) {}

  inline MemoryBuilder(xed_reg_enum_t reg_, xed_operand_action_enum_t action_)
      : action(action_),
        kind(BUILD_REGISTER)  {
    reg.DecodeFromNative(reg_);
  }

  inline MemoryBuilder(const void *ptr_, xed_operand_action_enum_t action_)
      : ptr(ptr_),
        action(action_),
        kind(BUILD_POINTER)  {}

  // Add this memory as an operand to the instruction `instr`.
  void Build(Instruction *instr);

 private:
  union {
    VirtualRegister reg;
    const void *ptr;
    Operand op;
  };
  xed_operand_action_enum_t action;
  enum {
    BUILD_POINTER,
    BUILD_REGISTER,
    BUILD_OPERAND
  } kind;
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

// Initialize an emptry Granary `arch::Instruction` from a XED iclass,
// category, and the number of explicit operands.
void BuildInstruction(Instruction *instr, xed_iclass_enum_t iclass,
                      xed_iform_enum_t iform, xed_category_enum_t category);

// Custom LEA instruction builder for source register operands.
template <typename A0, typename A1>
inline static void LEA_GPRv_GPRv(Instruction *instr, A0 a0, A1 a1) {
  BuildInstruction(instr, XED_ICLASS_LEA, XED_IFORM_LEA_GPRv_AGEN,
                   XED_CATEGORY_MISC);
  RegisterBuilder(a0, XED_OPERAND_ACTION_W).Build(instr);
  RegisterBuilder(a1, XED_OPERAND_ACTION_R).Build(instr);
}

// Custom LEA instruction builder for source register operands. This is like
// doing `dest = src1 + src2`.
template <typename A0, typename A1, typename A2>
inline static void LEA_GPRv_GPRv_GPRv(Instruction *instr, A0 a0, A1 a1, A2 a2) {
  BuildInstruction(instr, XED_ICLASS_LEA, XED_IFORM_LEA_GPRv_AGEN,
                   XED_CATEGORY_MISC);
  RegisterBuilder(a0, XED_OPERAND_ACTION_W).Build(instr);
  RegisterBuilder(a1, XED_OPERAND_ACTION_R).Build(instr);
  RegisterBuilder(a2, XED_OPERAND_ACTION_R).Build(instr);
}

// Custom LEA instruction builder for source immediate operands.
template <typename A0, typename A1>
inline static void LEA_GPRv_IMMv(Instruction *instr, A0 a0, A1 a1) {
  BuildInstruction(instr, XED_ICLASS_LEA, XED_IFORM_LEA_GPRv_AGEN,
                   XED_CATEGORY_MISC);
  RegisterBuilder(a0, XED_OPERAND_ACTION_W).Build(instr);
  ImmediateBuilder(a1, XED_ENCODER_OPERAND_TYPE_PTR).Build(instr);
}

// Custom LEA instruction builder for source immediate operands.
template <typename A0>
inline static void LEA_GPRv_AGEN(Instruction *instr, A0 a0, Operand a1) {
  BuildInstruction(instr, XED_ICLASS_LEA, XED_IFORM_LEA_GPRv_AGEN,
                   XED_CATEGORY_MISC);
  RegisterBuilder(a0, XED_OPERAND_ACTION_W).Build(instr);
  MemoryBuilder(a1, XED_OPERAND_ACTION_R).Build(instr);
}

// Make a simple base/displacement memory operand.
inline static Operand BaseDispMemOp(int32_t disp, xed_reg_enum_t base_reg,
                                    int width=-1) {
  Operand op;
  op.type = XED_ENCODER_OPERAND_TYPE_MEM;
  if (disp) {
    op.is_compound = true;
    op.mem.disp = disp;
    op.mem.reg_base = base_reg;
  } else {
    op.is_compound = false;
    op.reg.DecodeFromNative(base_reg);
  }
  op.width = static_cast<int8_t>(width);
  return op;
}

}  // namespace arch
}  // namespace granary

// Bring in the auto-generated instruction builder API.
#ifndef GRANARY_EXCLUDE_INSTRUCTION_BUILDER
# include "generated/xed2-intel64/instruction_builder.cc"
#endif

#endif  // GRANARY_ARCH_X86_64_BUILDER_H_
