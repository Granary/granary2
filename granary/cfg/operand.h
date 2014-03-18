/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_OPERAND_H_
#define GRANARY_CFG_OPERAND_H_

#include "granary/base/cast.h"
#include "granary/base/container.h"
#include "granary/base/string.h"

#include "granary/code/register.h"

namespace granary {

// Forward declarations.
class DecodedBasicBlock;
class NativeInstruction;
class Operand;
class MemoryOperand;
class RegisterOperand;
class ImmediateOperand;

namespace driver {
class Operand;
}  // namespace driver

// Type of a string that can be used to convert an operand to a string.
typedef FixedLengthString<31> OperandString;

// A reference to an operand contained within a native instruction. Operand
// references don't have a strict kind (i.e. memory, immediate, register)
// because their backing operand can be replaced, and so the kind invariant
// would change.
class OperandRef {
 public:
  // Initialize this operand.
  GRANARY_INTERNAL_DEFINITION inline OperandRef(driver::Operand *op_)
      : op(op_) {}

  // Returns true if this `OperandRef` references a memory operand, and if so,
  // updates `mem_op` to have the value of the referenced operand.
  //
  // Note: This operation is only valid if `OperandRef::IsValid` returns true.
  bool Match(MemoryOperand &mem_op) const;

  // Returns true if this `OperandRef` references a register operand, and if so,
  // updates `reg_op` to have the value of the referenced operand.
  //
  // Note: This operation is only valid if `OperandRef::IsValid` returns true.
  bool Match(RegisterOperand &reg_op) const;

  // Returns true if this `OperandRef` references an immediate operand, and if
  // so, updates `imm_op` to have the value of the referenced operand.
  //
  // Note: This operation is only valid if `OperandRef::IsValid` returns true.
  bool Match(ImmediateOperand &imm_op) const;

  // Try to replace the referenced operand with a concrete operand. Returns
  // false if the referenced operand is not allowed to be replaced. For example,
  // suppressed and implicit operands cannot be replaced.
  //
  // Note: This has a driver-specific implementation.
  bool ReplaceWith(const Operand &repl_op);

  // Returns true if this is a valid reference.
  bool IsValid(void) const;

 private:
  OperandRef(void) = delete;

  GRANARY_POINTER(driver::Operand) * GRANARY_CONST op;
};

// A generic operand from a native instruction. A generic interface is provided
// so that operands can be iterated.
class Operand {
 public:
  inline Operand(void)
      : op(),
        op_ptr(nullptr) {}

  // Copy semantics does
  Operand(const Operand &that);

  // Move semantics transfers referenceability.
  Operand(Operand &&that) = default;

  // Initialize this operand.
  GRANARY_INTERNAL_DEFINITION Operand(driver::Operand *op_);

  virtual ~Operand(void) = default;

  bool IsRead(void) const;
  bool IsWrite(void) const;
  bool IsConditionalRead(void) const;
  bool IsConditionalWrite(void) const;

  // Conveniences.
  inline bool IsReadWrite(void) const {
    return IsRead() && IsWrite();
  }

  // Returns whether or not this operand can be replaced / modified.
  //
  // Note: This has a driver-specific implementation.
  bool IsModifiable(void) const;

  // Return the width (in bits) of this operand, or -1 if its width is not
  // known.
  //
  // Note: This has a driver-specific implementation.
  int Width(void) const;

  // Convert this operand into a string.
  //
  // Note: This has a driver-specific implementation.
  void EncodeToString(OperandString *str) const;

  // Convert this operand into a reference, so that we can then replace it the
  // backing operand.
  //
  // Note: This operation is only valid on operands matched from instructions,
  //       and not manually created operands.
  OperandRef Ref(void) const;

  // Replace the internal operand memory. This method is "unsafe" insofar
  // as it assumes the caller is maintaining the invariant that the current
  // operand is being replaced with one that has the correct type.
  GRANARY_INTERNAL_DEFINITION void UnsafeReplace(driver::Operand *op_);

  GRANARY_DECLARE_BASE_CLASS(Operand)

 GRANARY_PROTECTED:
  GRANARY_CONST OpaqueContainer<driver::Operand, 16> op;

  friend class OperandRef;

  // Pointer to the native operand that backs `op`. We don't allow any of the
  // `Operand`-derived classes to manipulate or use this backing pointer, nor do
  // we actually use this pointer to derive properties of the backing operand.
  // Instead, it is used only to make a generic `OperandRef`, with which one can
  // replace the backing operand.
  //
  // This might point to a special tombstone operand that can't validly be
  GRANARY_POINTER(driver::Operand) * GRANARY_CONST op_ptr;
};

// Represents a memory operand. Memory operands are either pointers (i.e.
// addresses to some location in memory) or register operands containing an
// address.
class MemoryOperand : public Operand {
 public:
  using Operand::Operand;

  inline MemoryOperand(void)
      : Operand() {}

  // Initialize a new memory operand from a virtual register, where the
  // referenced memory has a width of `num_bits`.
  //
  // Note: This has a driver-specific implementation.
  MemoryOperand(const VirtualRegister &ptr_reg, int num_bits);

  // Initialize a new memory operand from a pointer, where the
  // referenced memory has a width of `num_bits`.
  //
  // Note: This has a driver-specific implementation.
  MemoryOperand(const void *ptr, int num_bits);

  virtual ~MemoryOperand(void) = default;

  // Try to match this memory operand as a pointer value.
  //
  // Note: This has a driver-specific implementation.
  bool MatchPointer(const void *&ptr) const;

  // Try to match this memory operand as a register value. That is, the address
  // is stored in the matched register.
  //
  // Note: This has a driver-specific implementation.
  bool MatchRegister(VirtualRegister &reg) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Operand, MemoryOperand)
};

// Represents a register operand. This might be a general-purpose register, a
// non-general purpose architectural register, or a virtual register.
class RegisterOperand : public Operand {
 public:
  using Operand::Operand;

  inline RegisterOperand(void)
      : Operand() {}

  virtual ~RegisterOperand(void) = default;

  // Driver-specific implementations.
  bool IsNative(void) const;
  bool IsVirtual(void) const;

  // Extract the register.
  VirtualRegister Register(void) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Operand, RegisterOperand)
};

// Represents an immediate integer operand.
class ImmediateOperand : public Operand {
 public:
  using Operand::Operand;

  inline ImmediateOperand(void)
      : Operand() {}

  virtual ~ImmediateOperand(void) = default;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Operand, ImmediateOperand)
};

// High-level operand actions. Underneath these high-level actions we can
// specialize to different types of reads and write with:
//
//    Read        -> Conditional Read (IsConditionalRead)
//    Write       -> Conditional Write (IsConditionalWrite)
//    Read/Write  -> Read and conditionally written (IsConditionalWrite)
//    Read/Write  -> Conditionally read, always written (IsConditionalRead)
//
// To prevent ambiguities when matching, e.g. attempting to match the same
// Read/Write operand with two separate match operands, we make Read/Write
// operands explicit, such that a Read(...) can't match against a Read/Write
// operand.
enum class OperandAction {
  ANY,
  READ,
  WRITE,
  READ_ONLY,
  WRITE_ONLY,
  READ_AND_WRITE
};

// Generic operand matcher.
class OperandMatcher {
 public:
  Operand * GRANARY_CONST op;
  const OperandAction action;
};

// Returns an operand matcher against an operand that is read.
inline static OperandMatcher ReadFrom(Operand &op) {
  return {&op, OperandAction::READ};
}

// Returns an operand matcher against an operand that is only read.
inline static OperandMatcher ReadOnlyFrom(Operand &op) {
  return {&op, OperandAction::READ_ONLY};
}

// Returns an operand matcher against an operand that is written.
inline static OperandMatcher WriteTo(Operand &op) {
  return {&op, OperandAction::WRITE};
}

// Returns an operand matcher against an operand that is only written.
inline static OperandMatcher WriteOnlyTo(Operand &op) {
  return {&op, OperandAction::WRITE_ONLY};
}

// Returns an operand matcher against an operand that is read and written.
inline static OperandMatcher ReadAndWriteTo(Operand &op) {
  return {&op, OperandAction::READ_AND_WRITE};
}

// Returns an operand matcher against an operand that is read and written.
inline static OperandMatcher ReadOrWriteTo(Operand &op) {
  return {&op, OperandAction::ANY};
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

#endif  // GRANARY_CFG_OPERAND_H_
