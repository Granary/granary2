/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_XED2_INTEL64_BUILDER_H_
#define GRANARY_DRIVER_XED2_INTEL64_BUILDER_H_

#include "granary/base/base.h"
#include "granary/base/type_traits.h"

#include "granary/driver/xed2-intel64/instruction.h"

namespace granary {
namespace driver {

// Returns the bit width of an immediate integer. This is to calculate operand
// width when using the instruction builder IR.
unsigned ImmediateWidth(uint64_t imm);

// Returns the bit width of some signed immediate value. This accounts for
// sign extension.
template <typename T, typename EnableIf<IsSignedInteger<T>::RESULT>::Type=0>
inline static unsigned ImmediateWidth(T imm) {
  auto imm64 = static_cast<int64_t>(imm);
  return ImmediateWidth(static_cast<uint64_t>(0 > imm64 ? -imm64 : imm64));
}

// Represents a XED base/displacement operand.
struct BaseDisp : xed_memop_t {
  inline BaseDisp(void) {
    seg = XED_REG_INVALID;
    base = XED_REG_INVALID;
    index = XED_REG_INVALID;
    scale = 0;
    disp = {0, 0};
  }
};

// Represents a XED register operand.
struct Register {
  inline explicit Register(xed_reg_enum_t reg_)
      : reg(reg_) {}

  // Dereferencing a register converts the `Register` into a `BaseDisp` type.
  // This is equivalent of REG_*[0].
  inline BaseDisp operator*(void) const {
    BaseDisp bdisp;
    bdisp.base = reg;
    return bdisp;
  }

  // Accessing the `disp` byte converts the `Register` into a `BaseDisp`.
  template <typename T, typename EnableIf<IsSignedInteger<T>::RESULT>::Type=0>
  inline BaseDisp operator[](T disp) {
    BaseDisp bdisp;
    bdisp.base = reg;
    bdisp.disp.displacement = static_cast<uint64_t>(disp);
    bdisp.disp.displacement_width = ImmediateWidth(disp);
    return bdisp;
  }

  // Accessing the `disp` byte converts the `Register` into a `BaseDisp`.
  template <typename T, typename EnableIf<IsUnsignedInteger<T>::RESULT>::Type=0>
  inline BaseDisp operator[](T disp) {
    auto value = static_cast<uint64_t>(disp);
    BaseDisp bdisp;
    bdisp.base = reg;
    bdisp.disp.displacement = value;
    bdisp.disp.displacement_width = ImmediateWidth(value);
    return bdisp;
  }

  // XED register associated with this `Register` instance.
  xed_reg_enum_t reg;
};

// Represents a RIP-relative address.
struct RelativeAddress {
  template <typename T>
  inline explicit RelativeAddress(T *ptr)
      : addr(reinterpret_cast<intptr_t>(ptr)) {}

  intptr_t addr;
};

// Represents an immediate constant.
struct Immediate {
  template <typename T, typename EnableIf<IsSignedInteger<T>::RESULT>::Type=0>
  explicit Immediate(T imm)
      : value(static_cast<uint64_t>(static_cast<int64_t>(imm))),
        width(ImmediateWidth(value)) {}

  // TODO(pag): Assuming width corresponds to sign-extension might be dangerous
  //            when using MOV_* instructions; however, it also reveals some
  //            better instruction selection opportunities with MOVSX_*.
  template <typename T, typename EnableIf<IsUnsignedInteger<T>::RESULT>::Type=0>
  explicit Immediate(T imm)
      : value(static_cast<uint64_t>(imm)),
        width(ImmediateWidth(imm)) {}

  template <typename T>
  explicit Immediate(T *ptr)
      : value(reinterpret_cast<uintptr_t>(ptr)),
        width(ImmediateWidth(value)) {}

  uint64_t value;
  unsigned width;
};

// Import operands into an `Instruction` instance.
void ImportOperand(Instruction *instr, Operand *op,
                   xed_operand_action_enum_t rw, Register reg);
void ImportOperand(Instruction *instr, Operand *op,
                   xed_operand_action_enum_t rw, BaseDisp bdisp);
void ImportOperand(Instruction *instr, Operand *op,
                   xed_operand_action_enum_t rw, RelativeAddress addr);
void ImportOperand(Instruction *instr, Operand *op,
                   xed_operand_action_enum_t rw, Immediate imm,
                   xed_encoder_operand_type_t type);

#ifndef GRANARY_DEFINE_XED_REG
# define GRANARY_DEFINE_XED_REG(mnemonic) \
    extern Register GRANARY_CAT(REG_, mnemonic)
#endif  // !GRANARY_DEFINE_XED_REG

GRANARY_DEFINE_XED_REG(AX);
GRANARY_DEFINE_XED_REG(CX);
GRANARY_DEFINE_XED_REG(DX);
GRANARY_DEFINE_XED_REG(BX);
GRANARY_DEFINE_XED_REG(SP);
GRANARY_DEFINE_XED_REG(BP);
GRANARY_DEFINE_XED_REG(SI);
GRANARY_DEFINE_XED_REG(DI);
GRANARY_DEFINE_XED_REG(R8W);
GRANARY_DEFINE_XED_REG(R9W);
GRANARY_DEFINE_XED_REG(R10W);
GRANARY_DEFINE_XED_REG(R11W);
GRANARY_DEFINE_XED_REG(R12W);
GRANARY_DEFINE_XED_REG(R13W);
GRANARY_DEFINE_XED_REG(R14W);
GRANARY_DEFINE_XED_REG(R15W);
GRANARY_DEFINE_XED_REG(EAX);
GRANARY_DEFINE_XED_REG(ECX);
GRANARY_DEFINE_XED_REG(EDX);
GRANARY_DEFINE_XED_REG(EBX);
GRANARY_DEFINE_XED_REG(ESP);
GRANARY_DEFINE_XED_REG(EBP);
GRANARY_DEFINE_XED_REG(ESI);
GRANARY_DEFINE_XED_REG(EDI);
GRANARY_DEFINE_XED_REG(R8D);
GRANARY_DEFINE_XED_REG(R9D);
GRANARY_DEFINE_XED_REG(R10D);
GRANARY_DEFINE_XED_REG(R11D);
GRANARY_DEFINE_XED_REG(R12D);
GRANARY_DEFINE_XED_REG(R13D);
GRANARY_DEFINE_XED_REG(R14D);
GRANARY_DEFINE_XED_REG(R15D);
GRANARY_DEFINE_XED_REG(RAX);
GRANARY_DEFINE_XED_REG(RCX);
GRANARY_DEFINE_XED_REG(RDX);
GRANARY_DEFINE_XED_REG(RBX);
GRANARY_DEFINE_XED_REG(RSP);
GRANARY_DEFINE_XED_REG(RBP);
GRANARY_DEFINE_XED_REG(RSI);
GRANARY_DEFINE_XED_REG(RDI);
GRANARY_DEFINE_XED_REG(R8);
GRANARY_DEFINE_XED_REG(R9);
GRANARY_DEFINE_XED_REG(R10);
GRANARY_DEFINE_XED_REG(R11);
GRANARY_DEFINE_XED_REG(R12);
GRANARY_DEFINE_XED_REG(R13);
GRANARY_DEFINE_XED_REG(R14);
GRANARY_DEFINE_XED_REG(R15);
GRANARY_DEFINE_XED_REG(AL);
GRANARY_DEFINE_XED_REG(CL);
GRANARY_DEFINE_XED_REG(DL);
GRANARY_DEFINE_XED_REG(BL);
GRANARY_DEFINE_XED_REG(SPL);
GRANARY_DEFINE_XED_REG(BPL);
GRANARY_DEFINE_XED_REG(SIL);
GRANARY_DEFINE_XED_REG(DIL);
GRANARY_DEFINE_XED_REG(R8B);
GRANARY_DEFINE_XED_REG(R9B);
GRANARY_DEFINE_XED_REG(R10B);
GRANARY_DEFINE_XED_REG(R11B);
GRANARY_DEFINE_XED_REG(R12B);
GRANARY_DEFINE_XED_REG(R13B);
GRANARY_DEFINE_XED_REG(R14B);
GRANARY_DEFINE_XED_REG(R15B);
GRANARY_DEFINE_XED_REG(AH);
GRANARY_DEFINE_XED_REG(CH);
GRANARY_DEFINE_XED_REG(DH);
GRANARY_DEFINE_XED_REG(BH);

#undef GRANARY_DEFINE_XED_REG

}  // namespace driver
}  // namespace granary

// Bring in the auto-generated instruction builder API.
#include "generated/xed2-intel64/instruction_builder.cc"

#endif  // GRANARY_DRIVER_XED2_INTEL64_BUILDER_H_
