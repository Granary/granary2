/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_OPERAND_H_
#define GRANARY_CODE_OPERAND_H_

#include "granary/base/cast.h"
#include "granary/base/string.h"

#include "granary/code/match_operand.h"

namespace granary {

// Forward declarations.
class DecodedBasicBlock;
class NativeInstruction;

GRANARY_INTERNAL_DEFINITION namespace driver {
// Forward declarations.
class Instruction;
class Operand;
}  // namespace driver

// Type of a string that can be used to convert an operand to a string.
typedef FixedLengthString<31> OperandString;

// A generic operand to a native instruction.
class Operand {
 public:
  inline Operand(void)
      : instr(nullptr),
        op(nullptr) {}

  virtual ~Operand(void) = default;

  // Driver-specific implementations.
  bool IsRead(void) const;
  bool IsWrite(void) const;
  bool IsConditionalRead(void) const;
  bool IsConditionalWrite(void) const;

  // Conveniences.
  inline bool IsReadWrite(void) const {
    return IsRead() && IsWrite();
  }

  // Convert this operand into a string.
  void EncodeToString(OperandString *str) const;

  // Initialize this operand.
  GRANARY_INTERNAL_DEFINITION Operand(driver::Instruction *instr_,
                                      driver::Operand *op_);

  GRANARY_DECLARE_BASE_CLASS(Operand)

 GRANARY_PROTECTED:
  // The driver instruction to which this operand belongs.
  GRANARY_POINTER(driver::Instruction) * GRANARY_CONST instr;

  // The native operand to which this operand refers, if its a reference.
  GRANARY_POINTER(driver::Operand) * GRANARY_CONST op;

  // TODO(pag): Might need some stronger semblance of a "barrier" instruction
  //            so that computed addresses and registers in progress aren't
  //            committed to instructions eagerly.
};

// Represents a memory operand. Memory operands are either pointers (i.e.
// addresses to some location in memory) or register operands containing an
// address.
class MemoryOperand : public Operand {
 public:
  using Operand::Operand;
  virtual ~MemoryOperand(void) = default;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Operand, MemoryOperand)
};

// Represents a register operand. This might be a general-purpose register, a
// non-general purpose architectural register, or a virtual register.
class RegisterOperand : public Operand {
 public:
  using Operand::Operand;
  virtual ~RegisterOperand(void) = default;

  // Driver-specific implementations.
  bool IsNative(void) const;
  bool IsVirtual(void) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Operand, RegisterOperand)

  // TODO(pag): Overload operators to get memory operands from this register?
  //            Need to think about what it would be like to do something like:
  //
  //                MemoryOperand mloc1;
  //                auto addr1 = EffectiveAddress(mloc1);
  //                auto mloc2 = addr[10];
  //                auto addr2 = EffectiveAddress(mloc2);
  //
  //            Solution might be to have an intermediate object representing
  //            an l-value mloc. It can be used anywhere that a MemoryOperand
  //            is acceptable, but only commits an operation when placed in an
  //            instruction.
};

// Represents an
class ImmediateOperand : public Operand {
 public:
  using Operand::Operand;
  virtual ~ImmediateOperand(void) = default;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Operand, ImmediateOperand)
};

// Returns an operand matcher against an operand that is read.
inline static detail::OperandMatcher ReadFrom(Operand &op) {
  return {&op, detail::OperandAction::READ, false};
}

// Returns an operand matcher against an operand that is written.
inline static detail::OperandMatcher WriteTo(Operand &op) {
  return {&op, detail::OperandAction::WRITE, false};
}

// Returns an operand matcher against an operand that is read and written.
inline static detail::OperandMatcher ReadAndWriteTo(Operand &op) {
  return {&op, detail::OperandAction::READ_WRITE, false};
}

// Returns an operand matcher against an operand that is read and written.
inline static detail::OperandMatcher ReadOrWriteTo(Operand &op) {
  return {&op, detail::OperandAction::ANY, false};
}

// Returns the effective address for a memory operand. The returned operand
// will either be a native or virtual register.
//
// Note: This has an driver-specific implementation.
//
// TODO(pag): How to make this work if the Operand needs to point to the actual
//            thing? I think I should distinguish between an operand ref and an
//            operand.
//RegisterOperand GetEffectiveAddress(MemoryOperand op);

}  // namespace granary

#endif  // GRANARY_CODE_OPERAND_H_
