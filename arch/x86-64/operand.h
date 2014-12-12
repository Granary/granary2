/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_X86_64_OPERAND_H_
#define ARCH_X86_64_OPERAND_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/pc.h"

#include "granary/cfg/operand.h"  // For `OperandString`.
#include "granary/code/register.h"  // For `VirtualRegister`.

#include "arch/operand.h"
#include "arch/x86-64/xed.h"

namespace granary {

// Forward declarations.
class Operand;
class NativeInstruction;
class AnnotationInstruction;

namespace arch {

// Represents an operand to an x86-64 instruction.
class Operand : public OperandInterface {
 public:
  Operand(void)
      : a(0),
        b(0),
        c(0),
        d(0) {}

  Operand(const Operand &op);
  Operand &operator=(const Operand &that);

  inline bool IsValid(void) const {
    return XED_ENCODER_OPERAND_TYPE_INVALID != type;
  }

  inline bool IsRead(void) const {
    return xed_operand_action_read(rw);
  }

  inline bool IsWrite(void) const {
    return xed_operand_action_written(rw);
  }

  inline bool IsSemanticDefinition(void) const {
    return is_definition;
  }

  inline bool IsConditionalRead(void) const {
    return xed_operand_action_conditional_read(rw);
  }

  inline bool IsConditionalWrite(void) const {
    return xed_operand_action_conditional_write(rw);
  }

  inline bool IsRegister(void) const {
    return XED_ENCODER_OPERAND_TYPE_REG == type;
  }

  inline bool IsMemory(void) const {
    return XED_ENCODER_OPERAND_TYPE_MEM == type ||
           XED_ENCODER_OPERAND_TYPE_PTR == type;
  }

  inline bool IsCompoundMemory(void) const {
    return XED_ENCODER_OPERAND_TYPE_MEM == type && is_compound;
  }

  inline bool IsEffectiveAddress(void) const {
    return is_effective_address;
  }

  inline bool IsPointer(void) const {
    return XED_ENCODER_OPERAND_TYPE_PTR == type;
  }

  inline bool IsImmediate(void) const {
    // TODO(pag): Where do we assume that BRDISP can be an immediate???

    return XED_ENCODER_OPERAND_TYPE_BRDISP == type ||
           XED_ENCODER_OPERAND_TYPE_IMM0 == type ||
           XED_ENCODER_OPERAND_TYPE_SIMM0 == type ||
           XED_ENCODER_OPERAND_TYPE_IMM1 == type;
  }

  inline bool IsBranchTarget(void) const {
    return XED_ENCODER_OPERAND_TYPE_BRDISP == type;
  }


  inline bool IsExplicit(void) const {
    return is_explicit;
  }

  inline size_t ByteWidth(void) const {
    return width / 8;
  }

  inline size_t BitWidth(void) const {
    return width;
  }

  void EncodeToString(OperandString *str) const;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpacked"
  union {
    // Used for simple initialization.
    struct {
      uint64_t a, b, c, d;
    };
    struct {
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

        // Combined memory operation. Used as part of encoding.
        struct {
          VirtualRegister base;
          VirtualRegister index;
          int32_t disp;
          uint8_t scale;
        } __attribute__((packed)) mem;

        // Annotation instruction representing the location of a return address or
        // the target of a jump.
        AnnotationInstruction *annotation_instr;
      };

      alignas(alignof(uint64_t)) struct {
        xed_encoder_operand_type_t type:8;
        uint16_t width;  // Operand width in bits.
        xed_operand_action_enum_t rw:8;  // Readable, writable, etc.
        xed_reg_enum_t segment:8;  // Used for memory operations.

        bool is_sticky:1;  // This operand cannot be changed.
        bool is_explicit:1;  // This is an explicit operand.

        // This is a compound memory operand (base/displacement).
        bool is_compound:1;

        // Does this memory operand access memory? An example of a case where a
        // memory operand does not access memory is `LEA`.
        bool is_effective_address:1;

        // Does this pointer memory operand refer to an annotation instruction's
        // encoded program counter? This is used when mangling indirect calls,
        // because we need to manually PUSH the return address onto the stack.
        bool is_annotation_instr:1;

        // Is this a definition of a register? Sometimes we need to ignore the
        // semantics of x86 register usage, e.g. a write to an 8-bit virtual
        // register should always be treated as having an implicit data dependency.
        bool is_definition:1;
      } __attribute__((packed));
    };
  };

#pragma clang diagnostic pop
};

static_assert(offsetof(Operand, reg) == offsetof(Operand, mem.base),
    "Invalid structure packing of `granary::arch::Operand`.");

static_assert(sizeof(Operand) <= 32,
    "Invalid structure packing of `granary::arch::Operand`.");

// Returns true if an implicit operand is ambiguous. An implicit operand is
// ambiguous if there are multiple encodings for the same iclass, and the given
// operand (indexed by `op`) is explicit for some iforms but not others.
bool IsAmbiguousOperand(xed_iclass_enum_t iclass, xed_iform_enum_t iform,
                        unsigned op_num);

}  // namespace arch
}  // namespace granary

#endif  // ARCH_X86_64_OPERAND_H_
