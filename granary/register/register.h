/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_REGISTER_REGISTER_H_
#define GRANARY_REGISTER_REGISTER_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

enum VirtualRegisterKind : uint8_t {
  VR_KIND_UNKNOWN = 0,

  // Architectural register that cannot be re-scheduled.
  VR_KIND_ARCH_FIXED,

  // Archtectural register that can potentially be re-scheduled.
  VR_KIND_ARCH_VIRTUAL,

  // Temporary virtual register, treated as single-def, multiple use.
  VR_KIND_TEMPORARY_VIRTUAL,

  // Generic virtual register that can be multiply defined and used. The
  // restriction here is that it can only be used within a local control-flow
  // graph. In different blocks, the register can have different backing, but
  // the end
  VR_KIND_GENERIC_VIRTUAL
};

// Defines the different types of virtual registers.
union VirtualRegister {
 public:
  inline VirtualRegister(void)
      : value(0) {}

  // Convert an architectural register into a virtual register.
  //
  // Note: This has a driver-specific implementation. See
  //       `granary/driver/*/register.cc` for the implementation.
  void DecodeArchRegister(uint64_t arch_reg_id);

  // Convert a virtual register into its associated architectural register.
  uint64_t EncodeArchRegister(void) const;

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

    // `false` if the register can be freely changed, and `true` otherwise. An
    // example is that some instructions have implicit/suppressed operands that
    // reference specific registers.
    bool is_sticky;
  } __attribute__((packed));

  uint64_t value;

} __attribute__((packed));

static_assert(sizeof(uint64_t) == sizeof(VirtualRegister),
    "Invalid packing of union `VirtualRegister`.");

}  // namespace granary

#endif  // GRANARY_REGISTER_REGISTER_H_
