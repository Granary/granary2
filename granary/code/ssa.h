/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_SSA_H_
#define GRANARY_CODE_SSA_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/disjoint_set.h"
#include "granary/base/tiny_vector.h"

#include "granary/code/register.h"

namespace granary {

// Forward declarations.
class NativeInstruction;
class SSAFragment;
namespace arch {
class Operand;
}  // namespace arch

// The operand action of this SSA operand. The table below shows how the operand
// actions of architectural operands maps to the operand actions of SSA
// operands.
//
// The purpose of these actions are to canonicalize the various possible
// combinations of architectural operand actions down to a simpler form that is
// then used to guide scheduling.
enum SSAOperandAction {
  kSSAOperandActionInvalid,

  // Happens for things like `XOR A, A`. In this case, we set the first operand
  // to have an action `WRITE`, and the second operand to have an action of
  // `kSSAOperandActionCleared`.
  kSSAOperandActionCleared,

                                  // Register Operands      Memory Operands
                                  // -----------------      ---------------
  kSSAOperandActionRead,          // R, CR
  kSSAOperandActionMemoryRead,    //                        all
  kSSAOperandActionWrite,         // W*
  kSSAOperandActionReadWrite      // RW, CW, RCW

  // * Special case: If the write preserves some of the bytes of the original
  //                 register's value then we treat it as a `READ_WRITE` and not
  //                 as a `WRITE`.
};

class SSARegisterWeb : public DisjointSet<VirtualRegister> {
 public:
  SSARegisterWeb(void) = default;

  using DisjointSet::DisjointSet;

  GRANARY_DEFINE_NEW_ALLOCATOR(SSARegisterWeb, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSARegisterWeb);
};

// The SSA representation of an operand to a `NativeInstruction`.
class SSAOperand {
 public:
  SSAOperand(void);

  // Canonical action that determines how the dependencies should be
  // interpreted as well as created.
  SSAOperandAction action;

  // References the arch-specific instruction operand directly. This is used
  // when doing things like copy propagation and register re-scheduling.
  arch::Operand *operand;

  // The web of all `SSAOperand`s that operate on this register.
  SSARegisterWeb reg_web;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAOperand);
};

// Represents the operands of a `NativeInstruction`, but in SSA form.
class SSAInstruction {
 public:
  SSAInstruction(void);

  size_t num_ops;
  SSAOperand ops[arch::Instruction::MAX_NUM_OPERANDS];

  GRANARY_DEFINE_NEW_ALLOCATOR(SSAInstruction, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAInstruction);
};

}  // namespace granary

#endif  // GRANARY_CODE_SSA_H_
