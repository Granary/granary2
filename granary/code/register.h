/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_REGISTER_H_
#define GRANARY_CODE_REGISTER_H_

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/bitset.h"

#include "granary/breakpoint.h"

namespace granary {

// Forward declarations.
class NativeInstruction;
class Operand;

GRANARY_INTERNAL_DEFINITION namespace arch {
class Instruction;
class Operand;
}  // namespace arch

// The kind of a virtual register.
enum VirtualRegisterKind : uint8_t {
  kVirtualRegisterKindInvalid = 0,

  // Architectural register that cannot be re-scheduled.
  kVirtualRegisterKindUnschedulable,

  // Architectural general-purpose register.
  kVirtualRegisterKindArchGpr,

  // Virtual general-purpose register.
  kVirtualRegisterKindVirtualGpr

  // Index into the virtual register storage location. This is used at virtual
  // register allocation time, and allows us to manage the differences between
  // user space and kernel space at a lower level.
  //
  // Note: This can and should only be used as a memory operand!!
  _GRANARY_IF_INTERNAL( kVirtualRegisterKindSlot )
};

#ifdef GRANARY_INTERNAL
enum : uint16_t {
  kMinTemporaryVirtualRegister = 1024,
  kMinTraceVirtualRegister = 2048,
  kMinGlobalVirtualRegister = 3072
};
#endif

// Defines the different types of virtual registers.
union alignas(alignof(void *)) VirtualRegister {
 public:
  inline VirtualRegister(void)
      : value(0) {}

  // Initialize a non-ARCH-specific virtual register.
  inline VirtualRegister(VirtualRegisterKind kind_, uint8_t num_bytes_,
                         uint16_t reg_num_)
      : value(0) {
    reg_num = reg_num_;
    kind = kind_;
    num_bytes = num_bytes_;
    byte_mask = static_cast<uint8_t>(~(~0U << num_bytes));
    GRANARY_ASSERT(num_bytes && !(num_bytes & (num_bytes - 1)));
  }

  // Copy constructor.
  inline VirtualRegister(const VirtualRegister &that)
      : value(that.value) {}

  // Copy assignment.
  inline VirtualRegister &operator=(const VirtualRegister &that) {
    value = that.value;
    return *this;
  }

  template <typename T,
            typename EnableIf<!IsUnsignedInteger<T>::RESULT>::Type=0>
  inline void DecodeFromNative(T arch_reg_id) {
    DecodeFromNative(static_cast<uint32_t>(arch_reg_id));
  }

  // Convert an architectural register into a virtual register.
  //
  // Note: This has a architecture-specific implementation. See
  //       `arch/*/register.cc` for the implementation.
  void DecodeFromNative(uint32_t arch_reg_id);

  // Returns a new virtual register that was created from an architectural
  // register.
  inline static VirtualRegister FromNative(uint32_t arch_reg_id) {
    VirtualRegister vr;
    vr.DecodeFromNative(arch_reg_id);
    return vr;
  }

  template <typename T,
            typename EnableIf<!IsUnsignedInteger<T>::RESULT>::Type=0>
  inline static VirtualRegister FromNative(T arch_reg_id) {
    return FromNative(static_cast<uint32_t>(arch_reg_id));
  }

  // Convert a virtual register into its associated architectural register.
  //
  // Note: This has an architecture-specific implementation.
  uint32_t EncodeToNative(void) const;

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

  // Return the frame pointer register as a virtual register.
  //
  // Note: This has an architecture-specific implementation.
  static VirtualRegister FramePointer(void);

  // Return the width (in bits) of this register.
  inline size_t BitWidth(void) const {
    return num_bytes * 8UL;
  }

  // Return the width (in bytes) of this register.
  inline size_t ByteWidth(void) const {
    return num_bytes;
  }

  // Returns true if this register preserves any of the bytes of the backing
  // GPR on a write, or if all bytes of the register are overwritten.
  inline bool PreservesBytesOnWrite(void) const {
    return 0 != preserved_byte_mask;
  }

  // Returns the effective size (in bytes) of a write to this register. This
  // could be bigger than the width of the register in bytes.
  //
  // Note: This has an architecture-specific implementation.
  size_t EffectiveWriteWidth(void) const;

  // Is this an architectural register?
  inline bool IsNative(void) const {
    return kVirtualRegisterKindUnschedulable == kind ||
           kVirtualRegisterKindArchGpr == kind;
  }

  // Is this a general purpose register?
  inline bool IsGeneralPurpose(void) const {
    return kVirtualRegisterKindArchGpr == kind ||
           kVirtualRegisterKindVirtualGpr == kind;
  }

  // Is this a virtual register?
  inline bool IsVirtual(void) const {
    return kVirtualRegisterKindVirtualGpr == kind;
  }

  inline bool IsValid(void) const {
    return kVirtualRegisterKindInvalid != kind;
  }

  // Is this a virtual spill slot? Virtual spill slots are used to identify
  // memory locations that are used for virtual register spilling/filling.
  GRANARY_INTERNAL_DEFINITION inline bool IsVirtualSlot(void) const {
    return kVirtualRegisterKindSlot == kind;
  }

  // Is this a "legacy" register? These registers come from older versions of
  // the ISA.
  inline bool IsLegacy(void) const {
    return is_legacy;
  }

  // Has this register been scheduled?
  GRANARY_INTERNAL_DEFINITION inline bool IsScheduled(void) const {
    return is_scheduled;
  }

  // Mark this register as scheduled.
  GRANARY_INTERNAL_DEFINITION inline void MarkAsScheduled(void) {
    is_scheduled = true;
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
  inline size_t Number(void) const {
    return reg_num;
  }

  // Widen this virtual register to a specific bit width.
  //
  // Note: This operates in place.
  //
  // Note: This has an architecture-specific implementation.
  void Widen(size_t dest_byte_width);

  // Return a copy of this virtual register, but where the new register has
  // the specified bit width.
  inline VirtualRegister WidenedTo(size_t dest_byte_width) const {
    auto widened = *this;
    widened.Widen(dest_byte_width);
    return widened;
  }

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

  // Mark the value of this register as being an alias for some displacement of
  // the stack pointer.
  inline void MarkAsStackPointerAlias(void) {
    is_stack_pointer = true;
  }

  // Does the current value of this register alias some displacement of the
  // stack pointer?
  inline bool IsStackPointerAlias(void) const {
    return is_stack_pointer;
  }

 private:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpacked"
  struct {
    // Register number. In the case of architectural registers, this is some
    // identifier that maps back to the architecture-specific description for
    // architectural registers.
    uint16_t reg_num;

    // What kind of virtual register is this?
    VirtualRegisterKind kind;

    // Width (in bytes) of this register.
    uint16_t num_bytes;

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

    // Is this register a legacy register?
    bool is_legacy:1;

    // Has this register been scheduled?
    bool is_scheduled:1;

    // Is this a stack pointer / alias of the stack pointer?
    bool is_stack_pointer:1;
  } __attribute__((packed));
#pragma clang diagnostic pop
  uint64_t value;
};

static_assert(sizeof(uint64_t) >= sizeof(VirtualRegister),
    "Invalid packing of union `VirtualRegister`.");

// Allocate a new virtual register.
VirtualRegister AllocateVirtualRegister(size_t num_bytes=arch::GPR_WIDTH_BYTES);

// Frees all virtual registers.
GRANARY_INTERNAL_DEFINITION
void FreeAllVirtualRegisters(void);

// Forward declaration.
class RegisterSet;

namespace detail {

template <bool kIsLive>
class RegisterSetIterator {
 public:
  typedef RegisterSetIterator<kIsLive> Iterator;

  RegisterSetIterator(void)
      : tracker(nullptr),
        num(arch::NUM_GENERAL_PURPOSE_REGISTERS) {}

  explicit RegisterSetIterator(const RegisterSet *tracker_)
      : tracker(tracker_),
        num(0U) {
    Advance();
  }

  bool operator!=(const Iterator &that) const {
    return num != that.num;
  }

  VirtualRegister operator*(void) const {
    GRANARY_ASSERT(0 <= num && arch::NUM_GENERAL_PURPOSE_REGISTERS > num);
    return VirtualRegister(kVirtualRegisterKindArchGpr, arch::GPR_WIDTH_BYTES, num);
  }

  inline void operator++(void) {
    ++num;
    Advance();
  }

 private:
  void Advance(void);

  const RegisterSet * const tracker;
  uint16_t num;
};

}  // namespace detail

// Base implementation of a register tracker.
class RegisterSet : protected BitSet<arch::NUM_GENERAL_PURPOSE_REGISTERS> {
 public:
  typedef detail::RegisterSetIterator<true> Iterator;

  RegisterSet(void) = default;

  inline RegisterSet(const RegisterSet &that) {
    Copy(that);
  }

  inline Iterator begin(void) const {
    return Iterator(this);
  }

  inline Iterator end(void) const {
    return Iterator();
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
  inline void Kill(size_t num) {
    GRANARY_ASSERT(0 <= num && arch::NUM_GENERAL_PURPOSE_REGISTERS > num);
    Set(num, false);
  }

  // Kill a specific register.
  void Kill(VirtualRegister reg);

  // Kill a specific register, where we treat this register is being part of
  // a write. This takes into account the fact that two or more registers might
  // alias the same data.
  void WriteKill(VirtualRegister reg);

  // Returns true if a register is dead.
  inline bool IsDead(size_t num) const {
    GRANARY_ASSERT(0 <= num && arch::NUM_GENERAL_PURPOSE_REGISTERS > num);
    return !Get(num);
  }

  // Returns true if a register is live.
  inline bool IsDead(VirtualRegister reg) const {
    return IsDead(reg.Number());
  }

  // Revive a specific register.
  inline void Revive(size_t num) {
    GRANARY_ASSERT(0 <= num && arch::NUM_GENERAL_PURPOSE_REGISTERS > num);
    Set(num, true);
  }

  // Kill a specific register.
  void Revive(VirtualRegister reg);

  // Returns true if a register is live.
  inline bool IsLive(size_t num) const {
    GRANARY_ASSERT(0 <= num && arch::NUM_GENERAL_PURPOSE_REGISTERS > num);
    return Get(num);
  }

  // Returns true if a register is live.
  inline bool IsLive(VirtualRegister reg) const {
    return IsLive(reg.Number());
  }

  // Union some other live register set with the current live register set.
  // Returns true if there was a change in the set of live registers. This is
  // useful when we want to be conservative about the potentially live
  // registers out of a specific block.
  bool Union(const RegisterSet &that);

  // Intersect some other live register set with the current live register set.
  // Returns true if there was a change in the set of live registers. This is
  // useful when we want to be conservative about the potentially dead registers
  // out of a specific block.
  bool Intersect(const RegisterSet &that);

  // Returns true if two register usage tracker sets are equivalent.
  bool Equals(const RegisterSet &that) const;

  // Overwrites one register usage tracker with another.
  RegisterSet &operator=(const RegisterSet &that);

 protected:
  typedef typename RemoveReference<decltype(storage[0])>::Type StorageT;
  enum {
    STORAGE_LEN = sizeof storage / sizeof storage[0]
  };
};

static_assert(sizeof(RegisterSet) <= sizeof(uint64_t),
              "Invalid structure packing of `RegisterSet`.");

namespace detail {
template <bool kIsLive>
void RegisterSetIterator<kIsLive>::Advance(void) {
  for (; num < arch::NUM_GENERAL_PURPOSE_REGISTERS &&
         kIsLive != tracker->IsLive(num); ++num) {}
}
}  // namespace detail

// A class that tracks used general-purpose registers within a straight-line
// sequence of instructions.
//
// A register is used if the register appears anywhere in an instruction.
//
// Note: By default, all registers are treated as dead.
class UsedRegisterSet : public RegisterSet {
 public:
  inline UsedRegisterSet(void) {
    KillAll();
  }

  typedef detail::RegisterSetIterator<true> Iterator;

  inline Iterator begin(void) const {
    return Iterator(this);
  }

  inline Iterator end(void) const {
    return Iterator();
  }

  // Update this register tracker by marking all registers that appear in an
  // instruction as used.
  void Visit(const NativeInstruction *instr);

  // Note: This function has an architecture-specific implementation.
  GRANARY_INTERNAL_DEFINITION
  void Visit(const arch::Instruction *instr);

  // Note: This function has an architecture-specific implementation.
  GRANARY_INTERNAL_DEFINITION
  void Visit(const arch::Operand *op);

  // Update this register tracker by marking some registers as used (i.e.
  // restricted). This allows us to communicate some architecture-specific
  // encoding constraints to the register scheduler.
  void ReviveRestrictedRegisters(const NativeInstruction *instr);

  // Note: This function has an architecture-specific implementation.
  //
  // Note: This should be used in combination with `Visit` in order to extend
  //       the "used set" to mark certain registers as off-limits (via marking
  //       them as used).
  GRANARY_INTERNAL_DEFINITION
  void ReviveRestrictedRegisters(const arch::Instruction *instr);

  inline void Join(const UsedRegisterSet &that) {
    Union(that);
  }
};

static_assert(sizeof(UsedRegisterSet) <= sizeof(uint64_t),
              "Invalid structure packing of `RegisterSet`.");

// A class that tracks conservatively live, general-purpose registers within a
// straight-line sequence of instructions.
//
// A register is conservatively live if there exists a control-flow path to a
// use of the register, where along that path there is no intermediate
// definition of the register.
//
// Note: By default, all registers are treated as dead.
class LiveRegisterSet : public RegisterSet {
 public:
  inline LiveRegisterSet(void) {
    KillAll();
  }

  typedef detail::RegisterSetIterator<true> Iterator;

  inline Iterator begin(void) const {
    return Iterator(this);
  }

  inline Iterator end(void) const {
    return Iterator();
  }

  // Update this register tracker by visiting the operands of an instruction.
  //
  // Note: This treats conditional writes to a register as reviving that
  //       register.
  //
  // Note: This *only* inspects register usage in the instruction, and does
  //       *not* consider any implied register usage (e.g. treat all regs as
  //       live before a jump). Implied register usage is treated as a policy
  //       decision that must be made by the user of a register tracker, and
  //       not by the tracker itself.
  void Visit(const NativeInstruction *instr);

  // Note: This function has an architecture-specific implementation.
  GRANARY_INTERNAL_DEFINITION
  void Visit(const arch::Instruction *instr);

  // Note: This function has an architecture-specific implementation.
  GRANARY_INTERNAL_DEFINITION
  void Visit(const arch::Operand *op);

  inline void Join(const LiveRegisterSet &that) {
    Union(that);
  }
};

static_assert(sizeof(LiveRegisterSet) <= sizeof(uint64_t),
              "Invalid structure packing of `RegisterSet`.");

}  // namespace granary

#endif  // GRANARY_CODE_REGISTER_H_
