/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_SSA_H_
#define GRANARY_CODE_ASSEMBLE_SSA_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/list.h"
#include "granary/base/new.h"

#include "granary/code/register.h"

namespace granary {

// Forward declarations.
class SSAPhi;
class SSAPhiOperand;

// Generic SSA variable.
//
// This implementation roughly follows "Simple and Efficient Construction of
// Static Single Assignment Form" by Braun, M. et al.
class SSAVariable {
 public:
  virtual ~SSAVariable(void) = default;

  union {
    VirtualRegister reg;
    SSAVariable *parent;
    SSAPhiOperand *next;
  };

  GRANARY_DECLARE_BASE_CLASS(SSAVariable)

 protected:
  inline SSAVariable(void)
      : reg() {}

  explicit inline SSAVariable(VirtualRegister reg_)
      : reg(reg_) {}

  explicit inline SSAVariable(SSAPhiOperand *next_)
      : next(next_) {}

  explicit inline SSAVariable(SSAVariable *parent_)
      : parent(parent_) {}

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAVariable);
};

// This node is a definition of a register.
class SSARegister : public SSAVariable {
 public:
  explicit inline SSARegister(VirtualRegister reg_)
      : SSAVariable(reg_) {}

  virtual ~SSARegister(void) = default;

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSAVariable, SSARegister)

 private:
  SSARegister(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSARegister);
};

// An operand to a variadic PHI node.
class SSAPhiOperand : public SSAVariable {
 public:
  inline SSAPhiOperand(SSAVariable *var_, SSAPhiOperand *next_)
      : SSAVariable(next_),
        var(var_) {}

  virtual ~SSAPhiOperand(void) = default;

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSAVariable, SSAPhiOperand)

  // Get the variable referenced by this operand.
  SSAVariable *Variable(void);

 private:
  SSAPhiOperand(void) = delete;

  SSAVariable *var;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAPhiOperand);
};

typedef LinkedListIterator<SSAPhiOperand> SSAPhiOperandIterator;

// SSA PHI-function for combining two-or more reaching definitions into a single
// definition.
class SSAPhi : public SSAVariable {
 public:
  virtual ~SSAPhi(void) = default;

  explicit inline SSAPhi(VirtualRegister reg)
      : SSAVariable(reg),
        ops(nullptr) {}

  // Returns a PHI operand iterator.
  inline SSAPhiOperandIterator Operands(void) const {
    return SSAPhiOperandIterator(ops);
  }

  // Add a new reaching definition to this Phi node.
  void AddOperand(SSAVariable *var);

  // Try to convert this PHI node into a trivial PHI node. If possible, this
  // will cause the memory to go through an "unsafe" type conversion.
  void TryTrivialize(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSAVariable, SSAPhi)

 private:
  SSAPhiOperand *ops;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAPhi);
};

// Represents a trivial PHI node. That is, a PHI node that combines only a
// single operand.
class SSATrivialPhi : public SSAVariable {
 public:
  virtual ~SSATrivialPhi(void);

  explicit inline SSATrivialPhi(SSAVariable *parent_)
      : SSAVariable(parent_) {}

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSAVariable, SSATrivialPhi)

 private:
  SSATrivialPhi(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSATrivialPhi);
};

// Table of SSA variables. Meant to be used when visiting the instructions
// of a fragment in reverse order.
class SSAVariableTable {
 public:
  SSAVariableTable(void);
  ~SSAVariableTable(void);

  // Add in a concrete definition for a virtual register, where the register
  // being defined is read and written, or conditionally written, and therefore
  // should share the same storage and any definitions that reach the current
  // definition.
  SSAVariable *AddInheritingDefinition(VirtualRegister reg);

  // Add in a concrete definition for a virtual register. If a matching
  // definition is present in the `missing_defs` table, then the definition
  // there is modified in-place to represent the new definition, removed from
  // the `missing_defs` table, and returned.
  SSAVariable *AddSimpleDefinition(VirtualRegister reg);

  // Declare that a register is being used. This adds a definition of the
  // variable into the `missing_defs` table.
  void DeclareUse(VirtualRegister reg);

  // Promote missing definitions associated with uses in a fragment into live
  // definitions that leave the fragment.
  void PromoteMissingDefinitions(void);

  // Propagate definitions from one SSA variable table into another. This only
  // propagates definitions if they are missing in the `dest` table's
  // `missing_defs` table. If the destination table has multiple predecessors
  // then a PHI node is propagated in place of the definition from the current
  // table.
  bool PropagateMissingDefinitions(SSAVariableTable *dest,
                                   int dest_num_predecessors);

  // For each PHI node in the destination table, add an operand to that PHI
  // from the current table.
  void AddPhiOperands(SSAVariableTable *dest);

  // Simplify all PHI nodes.
  void SimplifyPhiNodes(void);

  GRANARY_DEFINE_NEW_ALLOCATOR(SSAVariableTable, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  enum {
    NUM_SLOTS = arch::NUM_GENERAL_PURPOSE_REGISTERS * 2
  };

  // Returns the index into one of the storage SSA variable hash tables where
  // the `SSAVariable` associated with `reg` exists, or where it should
  // go.
  int GetVarIndex(VirtualRegister reg, SSAVariable * const  *tab);

  // Returns a reference to the `SSAVariable` instance associated with `reg` in
  // the SSA varable hash table `tab`.
  SSAVariable *&GetVar(VirtualRegister reg, SSAVariable **tab);

  // Removes and returns the `SSAVariable` instance associated with a missing
  // definition of `reg`.
  SSAVariable *RemoveMissingDef(VirtualRegister reg);

  // Variables that aren't defined on entry to this fragment.
  SSAVariable *missing_defs[NUM_SLOTS];
  bool owns_missing_def[NUM_SLOTS];

  // Variables that are defined in this fragment and can reach to the next
  // fragment.
  SSAVariable *live_defs[NUM_SLOTS];

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAVariableTable);
};

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_SSA_H_
