/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_OPERAND_H_
#define GRANARY_CFG_OPERAND_H_

#include "granary/base/cast.h"
#include "granary/base/container.h"
#include "granary/base/string.h"
#include "granary/base/type_trait.h"

#include "granary/code/register.h"

namespace granary {

// Forward declarations.
class DecodedBasicBlock;
class NativeInstruction;
class Operand;
class MemoryOperand;
class RegisterOperand;
class ImmediateOperand;

namespace arch {
class Operand;
}  // namespace arch

// Type of a string that can be used to convert an operand to a string.
typedef FixedLengthString<31> OperandString;

// A reference to an operand contained within a native instruction. Operand
// references don't have a strict kind (i.e. memory, immediate, register)
// because their backing operand can be replaced, and so the kind invariant
// would change.
class OperandRef {
 public:
  // Initialize this operand.
  GRANARY_INTERNAL_DEFINITION inline OperandRef(arch::Operand *op_)
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

  GRANARY_POINTER(arch::Operand) * GRANARY_CONST op;
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
  GRANARY_INTERNAL_DEFINITION Operand(arch::Operand *op_);

  virtual ~Operand(void);

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

  // Returns whether or not this operand is explicit.
  //
  // Note: This is only valid on operands matched from instructions and not on
  //       manually created operands.
  //
  // Note: This has a driver-specific implementation.
  bool IsExplicit(void) const;

  // Return the width (in bits) of this operand, or -1 if its width is not
  // known.
  //
  // Note: This has a driver-specific implementation.
  int BitWidth(void) const;

  // Return the width (in bytes) of this operand, or -1 if its width is not
  // known.
  //
  // Note: This has a driver-specific implementation.
  int ByteWidth(void) const;

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
  GRANARY_INTERNAL_DEFINITION void UnsafeReplace(arch::Operand *op_);

  // Returns a pointer to the internal, arch-specific memory operand that is
  // *internal* to this `Operand`.
  GRANARY_INTERNAL_DEFINITION
  const arch::Operand *Extract(void) const;

  // Returns a pointer to the internal, arch-specific memory operand that is
  // *referenced* by this `Operand`.
  GRANARY_INTERNAL_DEFINITION
  arch::Operand *UnsafeExtract(void) const;

  GRANARY_DECLARE_BASE_CLASS(Operand)

 GRANARY_PROTECTED:
  GRANARY_CONST OpaqueContainer<arch::Operand, 16, 16> op;

  friend class OperandRef;

  // Pointer to the native operand that backs `op`. We don't allow any of the
  // `Operand`-derived classes to manipulate or use this backing pointer, nor do
  // we actually use this pointer to derive properties of the backing operand.
  // Instead, it is used only to make a generic `OperandRef`, with which one can
  // replace the backing operand.
  //
  // This might point to a special tombstone operand that can't validly be
  GRANARY_POINTER(arch::Operand) * GRANARY_CONST op_ptr;
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
  // referenced memory has a width of `num_bytes`.
  //
  // Note: This has a driver-specific implementation.
  MemoryOperand(const VirtualRegister &ptr_reg, int num_bytes);

  // Generic initializer for a pointer to some data.
  template <typename T>
  inline explicit MemoryOperand(const T *ptr)
      : MemoryOperand(reinterpret_cast<const void *>(ptr),
                      static_cast<int>(GRANARY_MIN(8, sizeof(T)))) {}

  // Initialize a new memory operand from a pointer, where the
  // referenced memory has a width of `num_bytes`.
  //
  // Note: This has a driver-specific implementation.
  MemoryOperand(const void *ptr, int num_bytes);

  // Returns true if this is a compound memory operation. Compound memory
  // operations can have multiple smaller operands (e.g. registers) inside of
  // them. An example of a compound memory operand is a `base + index * scale`
  // (i.e. base/displacement) operand on x86.
  //
  // Note: This has a driver-specific implementation.
  bool IsCompound(void) const;

  // Is this an effective address (instead of being an actual memory access).
  //
  // Note: This has a driver-specific implementation.
  bool IsEffectiveAddress(void) const;

  // Try to match this memory operand as a pointer value.
  //
  // Note: This has a driver-specific implementation.
  bool IsPointer(void) const;

  // Try to match this memory operand as a pointer value.
  //
  // Note: This has a driver-specific implementation.
  bool MatchPointer(const void *&ptr) const;

  // Try to match this memory operand as a register value. That is, the address
  // is stored in the matched register.
  //
  // Note: This has a driver-specific implementation.
  bool MatchRegister(VirtualRegister &reg) const;

  // Try to match several registers from the memory operand. This is applicable
  // when this is a compound memory operand, e.g. `base + index * scale`. This
  // also works when the memory operand is not compound.
  //
  // Note: This has a architecture-specific implementation.
  size_t CountMatchedRegisters(
      std::initializer_list<VirtualRegister *> regs) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Operand, MemoryOperand)
};

// Represents a register operand. This might be a general-purpose register, a
// non-general purpose architectural register, or a virtual register.
class RegisterOperand : public Operand {
 public:
  using Operand::Operand;
  inline RegisterOperand(void)
      : Operand() {}

  // Initialize a new register operand from a virtual register.
  //
  // Note: This has a driver-specific implementation.
  explicit RegisterOperand(const VirtualRegister reg);

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

  // Initialize a immediate operand from a signed integer, where the value has
  // a width of `width_bytes`.
  //
  // Note: This has a driver-specific implementation.
  ImmediateOperand(intptr_t imm, int width_bytes);

  // Initialize a immediate operand from a unsigned integer, where the value
  // has a width of `width_bytes`.
  //
  // Note: This has a driver-specific implementation.
  ImmediateOperand(uintptr_t imm, int width_bytes);

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

enum class OperandConstraint {
  MATCH,
  BIND
};

// Generic operand matcher.
class OperandMatcher {
 public:
  Operand * GRANARY_CONST op;
  const OperandAction action;
  const OperandConstraint constraint;
};

// Returns an operand matcher against an operand that is read.
inline static OperandMatcher ReadFrom(Operand &op) {
  return {&op, OperandAction::READ, OperandConstraint::BIND};
}

// Returns an operand matcher against an operand that is only read.
inline static OperandMatcher ReadOnlyFrom(Operand &op) {
  return {&op, OperandAction::READ_ONLY, OperandConstraint::BIND};
}

// Returns an operand matcher against an operand that is written.
inline static OperandMatcher WriteTo(Operand &op) {
  return {&op, OperandAction::WRITE, OperandConstraint::BIND};
}

// Returns an operand matcher against an operand that is only written.
inline static OperandMatcher WriteOnlyTo(Operand &op) {
  return {&op, OperandAction::WRITE_ONLY, OperandConstraint::BIND};
}

// Returns an operand matcher against an operand that is read and written.
inline static OperandMatcher ReadAndWriteTo(Operand &op) {
  return {&op, OperandAction::READ_AND_WRITE, OperandConstraint::BIND};
}

// Returns an operand matcher against an operand that is read and written.
inline static OperandMatcher ReadOrWriteTo(Operand &op) {
  return {&op, OperandAction::ANY, OperandConstraint::BIND};
}

// TODO(pag): Only doing exact matching on register operands.

// Returns an operand matcher against an operand that is read.
inline static OperandMatcher ExactReadFrom(RegisterOperand &op) {
  return {&op, OperandAction::READ, OperandConstraint::MATCH};
}

// Returns an operand matcher against an operand that is only read.
inline static OperandMatcher ExactReadOnlyFrom(RegisterOperand &op) {
  return {&op, OperandAction::READ_ONLY, OperandConstraint::MATCH};
}

// Returns an operand matcher against an operand that is written.
inline static OperandMatcher ExactWriteTo(RegisterOperand &op) {
  return {&op, OperandAction::WRITE, OperandConstraint::MATCH};
}

// Returns an operand matcher against an operand that is only written.
inline static OperandMatcher ExactWriteOnlyTo(RegisterOperand &op) {
  return {&op, OperandAction::WRITE_ONLY, OperandConstraint::MATCH};
}

// Returns an operand matcher against an operand that is read and written.
inline static OperandMatcher ExactReadAndWriteTo(RegisterOperand &op) {
  return {&op, OperandAction::READ_AND_WRITE, OperandConstraint::MATCH};
}

// Returns an operand matcher against an operand that is read and written.
inline static OperandMatcher ExactReadOrWriteTo(RegisterOperand &op) {
  return {&op, OperandAction::ANY, OperandConstraint::MATCH};
}

}  // namespace granary

#endif  // GRANARY_CFG_OPERAND_H_
