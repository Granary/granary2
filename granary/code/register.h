/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_REGISTER_H_
#define GRANARY_CODE_REGISTER_H_

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/bitset.h"

namespace granary {

// Forward declarations.
class NativeInstruction;
class Operand;

GRANARY_INTERNAL_DEFINITION namespace arch {
class Instruction;
}  // namespace arch

// The kind of a virtual register.
enum VirtualRegisterKind : uint8_t {
  VR_KIND_UNKNOWN = 0,

  // Architectural register that cannot be re-scheduled.
  VR_KIND_ARCH_FIXED,

  // Architectural register that can potentially be re-scheduled.
  VR_KIND_ARCH_VIRTUAL,

  VR_KIND_VIRTUAL
};

// Defines the different types of virtual registers.
union VirtualRegister {
 public:
  inline VirtualRegister(void)
      : value(0) {}

  // Initialize a non-ARCH-specific virtual register.
  inline VirtualRegister(VirtualRegisterKind kind_, uint8_t num_bytes_,
                         uint16_t reg_num_)
      : reg_num(reg_num_),
        kind(kind_),
        num_bytes(num_bytes_),
        byte_mask(static_cast<uint8_t>(~(~0U << num_bytes))),
        preserved_byte_mask(0) {}

  // Copy constructor.
  inline VirtualRegister(const VirtualRegister &that)
      : value(that.value) {}

  // Convert an architectural register into a virtual register.
  //
  // Note: This has a architecture-specific implementation. See
  //       `granary/arch/*/register.cc` for the implementation.
  void DecodeFromNative(int arch_reg_id);

  // Returns a new virtual register that was created from an architectural
  // register.
  static VirtualRegister FromNative(int arch_reg_id) {
    VirtualRegister reg;
    reg.DecodeFromNative(arch_reg_id);
    return reg;
  }

  // Convert a virtual register into its associated architectural register.
  //
  // Note: This has an architecture-specific implementation.
  int EncodeToNative(void) const;

  // Return the flags register as a virtual register.
  //
  // Note: This has an architecture-specific implementation.
  static VirtualRegister Flags(void);

  // Return the instruction pointer register as a virtual register.
  //
  // Note: This has an architecture-specific implementation.
  static VirtualRegister InstructionPointer(void);

  // Return the stack pointer register as a virtual register.
  //
  // Note: This has an architecture-specific implementation.
  static VirtualRegister StackPointer(void);

  // Return the width (in bits) of this register.
  inline int BitWidth(void) const {
    return static_cast<int>(num_bytes) * 8;
  }

  // Return the width (in bytes) of this register.
  inline int ByteWidth(void) const {
    return static_cast<int>(num_bytes);
  }

  // Returns true if this register preserves any of the bytes of the backing
  // GPR on a write, or if all bytes of the register are overwritten.
  inline bool PreservesBytesOnWrite(void) const {
    return VR_KIND_ARCH_VIRTUAL == kind && 0 != preserved_byte_mask;
  }

  // Is this an architectural register?
  inline bool IsNative(void) const {
    return VR_KIND_ARCH_FIXED == kind || VR_KIND_ARCH_VIRTUAL == kind;
  }

  // Is this a general purpose register?
  inline bool IsGeneralPurpose(void) const {
    return VR_KIND_ARCH_VIRTUAL == kind || VR_KIND_VIRTUAL == kind;
  }

  // Is this a virtual register?
  inline bool IsVirtual(void) const {
    return VR_KIND_VIRTUAL == kind;
  }

  // Is this the stack pointer?
  //
  // Note: This has an architecture-specific implementation.
  bool IsStackPointer(void) const;

  // Is this the instruction pointer?
  //
  // Note: This has an architecture-specific implementation.
  bool IsInstructionPointer(void) const;

  // Is this the flags register?
  //
  // Note: This has an architecture-specific implementation.
  bool IsFlags(void) const;

  // Returns this register's internal number.
  inline int Number(void) const {
    return static_cast<int>(reg_num);
  }

  // Widen this virtual register to a specific bit width.
  //
  // Note: This has an architecture-specific implementation.
  void Widen(int dest_byte_width);

  // Compare one virtual register with another.
  //
  // Note: This does not consider bit width.
  inline bool operator==(const VirtualRegister that) const {
    return reg_num == that.reg_num && kind == that.kind;
  }

  // Compare one virtual register with another.
  //
  // Note: This does not consider bit width.
  inline bool operator!=(const VirtualRegister that) const {
    return reg_num != that.reg_num || kind != that.kind;
  }

 private:
  struct {
    // Register number. In the case of architectural registers, this is some
    // identifier that maps back to the driver-specific description for
    // architectural registers.
    uint16_t reg_num;

    // What kind of virtual register is this?
    VirtualRegisterKind kind;

    // Width (in bytes) of this register.
    uint8_t num_bytes;

    // Mask of which bytes of an architectural register this value represents.
    // For example, on x86 the class of registers [rax, eax, ax, ah, al] all
    // represent different selections of bytes within the same general purpose
    // register (rax).
    uint8_t byte_mask;

    // Mask of which bytes of an architectural register are preserved. The idea
    // here is that if we've got a write to `reg_num`, where not all bytes
    // are set (i.e. `byte_mask != 0xFF`), then we consider `reg_num` to be
    // dead before the write (assuming the same instruction doesn't also read)
    // if `byte_mask == (byte_mask | preserved_byte_mask)`, i.e. if all bytes
    // not represented by the register are not preserved.
    uint8_t preserved_byte_mask;
  } __attribute__((packed));

  uint64_t value;

} __attribute__((packed));

static_assert(sizeof(uint64_t) >= sizeof(VirtualRegister),
    "Invalid packing of union `VirtualRegister`.");

// Base implementation of a register tracker.
class RegisterTracker : protected BitSet<arch::NUM_GENERAL_PURPOSE_REGISTERS> {
 public:
  RegisterTracker(void) = default;

  RegisterTracker(const RegisterTracker &that) {
    Copy(that);
  }

  // Kill all registers.
  inline void KillAll(void) {
    SetAll(false);
  }

  // Revive all registers.
  inline void ReviveAll(void) {
    SetAll(true);
  }

  // Kill a specific register.
  inline void Kill(int num) {
    Set(static_cast<unsigned>(num), false);
  }

  // Kill a specific register.
  inline void Kill(VirtualRegister reg) {
    if (reg.IsNative() && reg.IsGeneralPurpose()) {
      Kill(reg.Number());
    }
  }

  // Kill a specific register, where we treat this register is being part of
  // a write. This takes into account the fact that two or more registers might
  // alias the same data.
  inline void WriteKill(VirtualRegister reg) {
    if (reg.IsNative() && reg.IsGeneralPurpose()) {
      if (reg.PreservesBytesOnWrite()) {
        Revive(reg.Number());
      } else {
        Kill(reg.Number());
      }
    }
  }

  // Returns true if a register is dead.
  inline bool IsDead(int num) const {
    return !Get(static_cast<unsigned>(num));
  }

  // Revive a specific register.
  inline void Revive(int num) {
    Set(static_cast<unsigned>(num), true);
  }

  // Kill a specific register.
  inline void Revive(VirtualRegister reg) {
    if (reg.IsNative() && reg.IsGeneralPurpose()) {
      Revive(reg.Number());
    }
  }

  // Returns true if a register is live.
  inline bool IsLive(int num) const {
    return Get(static_cast<unsigned>(num));
  }

  // Union some other live register set with the current live register set.
  // Returns true if there was a change in the set of live registers. This is
  // useful when we want to be conservative about the potentially live
  // registers out of a specific block.
  bool Union(const RegisterTracker &that);

  // Intersect some other live register set with the current live register set.
  // Returns true if there was a change in the set of live registers. This is
  // useful when we want to be conservative about the potentially dead registers
  // out of a specific block.
  bool Intersect(const RegisterTracker &that);

  // Returns true if two register usage tracker sets are equivalent.
  bool Equals(const RegisterTracker &that) const;

  // Overwrites one register usage tracker with another.
  RegisterTracker &operator=(const RegisterTracker &that) {
    if (this != &that) {
      this->Copy(that);
    }
    return *this;
  }

 protected:
  typedef typename RemoveReference<decltype(storage[0])>::Type StorageT;
  enum {
    STORAGE_LEN = sizeof storage / sizeof storage[0]
  };
};

// A class that tracks conservatively live, general-purpose registers within a
// straight-line sequence of instructions.
//
// A register is conservatively live if there exists a control-flow path to a
// use of the register, where along that path there is no intermediate
// definition of the register.
//
// Note: By default, all registers are treated as live.
class LiveRegisterTracker: public RegisterTracker{
 public:
  inline LiveRegisterTracker(void) {
    KillAll();
  }

  // Update this register tracker by visiting the operands of an instruction.
  //
  // Note: This treats conditional writes to a register as reviving that
  //       register.
  void Visit(NativeInstruction *instr);

  inline void Join(const LiveRegisterTracker &that) {
    Union(that);
  }
};

// A class that tracks conservatively dead, general-purpose registers within a
// straight-line sequence of instructions.
//
// A register is conservatively dead if there exists any control-flow path from
// the current instruction of a definition of the instruction.
//
// Note: By default, all registers are treated as live.
class DeadRegisterTracker : public RegisterTracker {
 public:
  inline DeadRegisterTracker(void) {
    ReviveAll();
  }

  // Update this register tracker by visiting the operands of an instruction.
  //
  // Note: This treats conditional writes to a register as reviving that
  //       register.
  void Visit(NativeInstruction *instr);

  inline void Join(const DeadRegisterTracker &that) {
    Intersect(that);
  }
};

}  // namespace granary

#endif  // GRANARY_CODE_REGISTER_H_
