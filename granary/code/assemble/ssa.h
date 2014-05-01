/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_SSA_H_
#define GRANARY_CODE_ASSEMBLE_SSA_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"

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

// Location at which a SSA Node is stored.
class SSASpillStorage {
 public:
  int slot;
  VirtualRegister reg;
};

// Generic SSA Node.
class SSANode {
 public:
  inline SSANode(SSAFragment *frag_, VirtualRegister reg_)
      : storage(),
        frag(frag_),
        reg(reg_) {}

  virtual ~SSANode(void);

  DisjointSet<SSASpillStorage *> storage;

  // SSAFragment in which this register is defined.
  SSAFragment * const frag;

  // The register associated with this node.
  const VirtualRegister reg;

  GRANARY_DECLARE_BASE_CLASS(SSANode)

 private:
  SSANode(void) = delete;
};

// Represents a node that is a selection of two or more available values. During
// the process of SSA construction, an `SSAControlPhiNode` might have 0 or 1
// incoming values/nodes. If, after construction, the node has 0 incoming values
// then the node needs to be converted into an `SSARegister`. If the node
// has a single incoming value, or 2 incoming values where one of those values
// is the node itself, then the node is converted into an `SSATrivialPhiNode`.
class SSAControlPhiNode : public SSANode {
 public:
  virtual ~SSAControlPhiNode(void) = default;
  SSAControlPhiNode(SSAFragment *frag_, VirtualRegister reg_);

  // Allocate and free.
  static void *operator new(std::size_t);
  static void *operator new(std::size_t, void *);
  static void operator delete(void *address);

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSANode, SSAControlPhiNode)

  // Add an operand to the PHI node.
  void AddOperand(SSANode *node);

  // Try to convert this PHI node into an alias or a register node. If this
  // succeeds at trivializing the PHI node then `true` is returned, otherwise
  // `false` is returned.
  bool UnsafeTryTrivialize(void);

  TinyVector<SSANode *, 2> operands;
};

// Represents a node that is directly inherited from some other location. The
// alias node is like a PHI node with only a single, always chosen selection.
// Therefore, the semantics of the alias are that the node is a placeholder
// for its incoming value, and that any use of
class SSAAliasNode : public SSANode {
 public:
  virtual ~SSAAliasNode(void) = default;
  SSAAliasNode(SSAFragment *frag_, SSANode *incoming_node_);

  // Allocate and free.
  static void *operator new(std::size_t);
  static void *operator new(std::size_t, void *);
  static void operator delete(void *address);

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSANode, SSAAliasNode)

  // The aliased value of this node.
  SSANode * const aliased_node;

 private:
  SSAAliasNode(void) = delete;
};

// Represents a "data PHI" node, where there is a control dependency embedded
// within an instruction, but where the specifics of the control dependency are
// opaque to us. For example, a read/write or conditional write
class SSADataPhiNode : public SSANode {
 public:
  virtual ~SSADataPhiNode(void) = default;
  SSADataPhiNode(SSAFragment *frag_, SSANode *incoming_node_);

  // Allocate and free.
  static void *operator new(std::size_t);
  static void *operator new(std::size_t, void *);
  static void operator delete(void *address);

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSANode, SSADataPhiNode)

  // The node on which this data-PHI node is (internally / opaquely) control-
  // dependent.
  SSANode * const dependent_node;

 private:
  SSADataPhiNode(void) = delete;
};

// Represents a register node, where this node directly refers to some
// definition of a register by some `NativeInstruction`. If this register node
// is not associated with a `NativeInstruction` (i.e. `instr == nullptr`) then
// this `SSARegister` was created as part of an incoming definition from a
// non-existent block (i.e. predecessor of the entry fragment to the fragment
// control-flow graph), and was created via trivialization of an
// `SSAControlPhiNode`.
class SSARegisterNode : public SSANode {
 public:
  virtual ~SSARegisterNode(void) = default;
  SSARegisterNode(SSAFragment *frag_, Instruction *instr_,
                  VirtualRegister reg_);

  // Allocate and free.
  static void *operator new(std::size_t);
  static void *operator new(std::size_t, void *);
  static void operator delete(void *address);

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSANode, SSARegisterNode)

  // Instruction that defines this register. We use this in combination with
  // `frag` when doing copy-propagation.
  Instruction *instr;

  // Register defined by this node.
  const VirtualRegister reg;

 private:
  SSARegisterNode(void) = delete;
};

// The operand action of this SSA operand. The table below shows how the operand
// actions of architectural operands maps to the operand actions of SSA
// operands.
//
// The purpose of these actions are to canonicalize the various possible
// combinations of architectural operand actions down to a simpler form that is
// then used to guide
enum class SSAOperandAction {
  INVALID,
  CLEARED,    // Happens for things like `XOR A, A`. In this case, we set
              // the first operand to have an action `WRITE`, and the second
              // operand to have an action of `CLEARED`.

              // Register Operands      Memory Operands
              // -----------------      ---------------
  READ,       // R, CR                  all
  WRITE,      // W*
  READ_WRITE  // RW, CW, RCW

  // * Special case: If the write preserves some of the bytes of the original
  //                 register's value then we treat it as a `READ_WRITE` and not
  //                 as a `WRITE`.
};

// Represents a small group of `SSANode` pointers.
typedef TinyVector<SSANode *, 2> SSANodePack;

// The SSA representation of an operand to a `NativeInstruction`.
class SSAOperand {
 public:
  SSAOperand(void);

  // References the arch-specific instruction operand directly. This is used
  // when doing things like copy propagation and register re-scheduling.
  arch::Operand *operand;

  // Vector of pointers to `SSANode`s to which this operand refers.
  SSANodePack nodes;

  // Canonical action that determines how the dependencies should be interpreted
  // as well as created.
  SSAOperandAction action;

  // True if this is a register operand, false if it's a memory operand.
  bool is_reg;

} __attribute__((packed));

// Represents a small group of `SSAOperand`s that are part of an instruction.
typedef TinyVector<SSAOperand, 2> SSAOperandPack;

// Represents the operands of a `NativeInstruction`, but in SSA form.
class SSAInstruction {
 public:
  SSAInstruction(void);

  // Ordered as: `WRITE` > `CLEARED`.
  SSAOperandPack defs;

  // Ordered as: `READ_WRITE` > `READ`.
  SSAOperandPack uses;

  GRANARY_DEFINE_NEW_ALLOCATOR(SSAInstruction, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAInstruction);
};

// Returns a pointer to the `SSANode` that is used to define the register `reg`
// in the instruction `instr`, or `nullptr` if the register is not defined by
// the instruction.
SSANode *DefinedNodeForReg(Instruction *instr, VirtualRegister reg);

// Returns the un-aliased node associated with the current node.
SSANode *UnaliasedNode(SSANode *node);

#if 0

// Forward declarations.
class SSAPhi;
class SSAPhiOperand;
class NativeInstruction;
class SSAVariable;
class RegisterLocation;  // Defined in `8_schedule_registers.cc`.

// Returns the reaching definition associated with some variable `var`. In the
// case of trivial SSA variables, we follow as many reaching definitions as we
// can to form the
SSAVariable *DefinitionOf(SSAVariable *var);

// Returns the virtual register associated with some `SSAVariable` instance.
VirtualRegister RegisterOf(SSAVariable *var);

// Returns the `SSAVariable` associated with a definition of `reg` if this
// instruction defines the register `reg`.
SSAVariable *DefinitionOf(NativeInstruction *instr, VirtualRegister reg);

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

  RegisterLocation *loc;

  GRANARY_DECLARE_BASE_CLASS(SSAVariable)

 protected:
  inline SSAVariable(void)
      : reg(),
        loc(nullptr) {}

  explicit inline SSAVariable(VirtualRegister reg_)
      : reg(reg_),
        loc(nullptr) {}

  explicit inline SSAVariable(SSAPhiOperand *next_)
      : next(next_),
        loc(nullptr) {}

  explicit inline SSAVariable(SSAVariable *parent_)
      : parent(parent_),
        loc(nullptr) {}

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAVariable);
};

// This node is a definition of a register.
class SSARegister : public SSAVariable {
 public:
  inline SSARegister(VirtualRegister reg_,
                     NativeInstruction *instr_=nullptr)
      : SSAVariable(reg_),
        instr(instr_) {}

  virtual ~SSARegister(void) = default;

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSAVariable, SSARegister)

  // Instruction that defines this register.
  NativeInstruction *instr;

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

  inline explicit SSAPhi(VirtualRegister reg)
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

// Similar to a trivial PHI node, but where we have to treat this definition of
// the register as being a concrete definition. This occurs in cases like
// read & written or conditionally written registers.
class SSAForward : public SSAVariable {
 public:
  virtual ~SSAForward(void);

  inline SSAForward(SSAVariable *parent_, SSAVariable *next_instr_def_)
      : SSAVariable(parent_),
        next_instr_def(next_instr_def_) {}

  GRANARY_DECLARE_DERIVED_CLASS_OF(SSAVariable, SSAForward)

  SSAVariable *next_instr_def;

 private:
  SSAForward(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAForward);
};

// Delete the memory associated with an SSA object.
void DestroySSAObj(void *obj);

enum {
  MAX_NUM_SSA_VARS = arch::NUM_GENERAL_PURPOSE_REGISTERS * 2
};

class SSAEntryVariable {
 public:
  SSAVariable *var;
  bool is_owned;

  inline bool operator==(const SSAEntryVariable &that) const {
    return var == that.var;
  }
};

typedef ArrayRangeIterator<SSAEntryVariable> SSAEntryDefsIterator;

typedef ArrayRangeIterator<SSAVariable *> SSAExitDefsIterator;

template <>
class LookupTableOperations<VirtualRegister, SSAVariable *> {
 public:
  static VirtualRegister KeyForValue(SSAVariable *var);
};

template <>
class LookupTableOperations<VirtualRegister, SSAEntryVariable> {
 public:
  inline static VirtualRegister KeyForValue(SSAEntryVariable var) {
    return LookupTableOperations<VirtualRegister, SSAVariable *>::
        KeyForValue(var.var);
  }
};

typedef FixedSizeLookupTable<VirtualRegister, SSAVariable *, MAX_NUM_SSA_VARS>
        SSAVariableTable;

// Table of SSA variables. Meant to be used when visiting the instructions
// of a fragment in reverse order.
class SSAVariableTracker {
 public:
  SSAVariableTracker(void);
  ~SSAVariableTracker(void);

  // Add in a concrete definition for a virtual register, where the register
  // being defined is read and written, or conditionally written, and therefore
  // should share the same storage and any definitions that reach the current
  // definition.
  SSAVariable *AddInheritingDefinition(VirtualRegister reg,
                                       SSAVariable *existing_instr_def);

  // Add in a concrete definition for a virtual register. If a matching
  // definition is present in the `missing_defs` table, then the definition
  // there is modified in-place to represent the new definition, removed from
  // the `missing_defs` table, and returned.
  SSAVariable *AddSimpleDefinition(VirtualRegister reg,
                                   NativeInstruction *instr);

  // Declare that a register is being used. This adds a definition of the
  // variable into the `missing_defs` table.
  void DeclareUse(VirtualRegister reg);

  // Promote missing definitions associated with uses in a fragment into live
  // definitions that leave the fragment.
  void PromoteMissingEntryDefs(void);

  // Propagate definitions from one SSA variable table into another. This only
  // propagates definitions if they are missing in the `dest` table's
  // `missing_defs` table. If the destination table has multiple predecessors
  // then a PHI node is propagated in place of the definition from the current
  // table.
  bool BackPropagateMissingDefsForUses(SSAFragment *predecessor,
                                       SSAVariableTracker *source);

  // For each PHI node in the destination table, add an operand to that PHI
  // from the current table.
  void AddPhiOperands(SSAVariableTracker *dest);

  // Simplify all PHI nodes.
  void SimplifyPhiNodes(void);

  GRANARY_DEFINE_NEW_ALLOCATOR(SSAVariableTracker, {
    SHARED = false,
    ALIGNMENT = 1
  })

  // Copy all entry definitions in this variable tracker into an SSA variable
  // table.
  void CopyEntryDefinitions(SSAVariableTable *vars);

  // Returns the definition of some register on entry to a fragment.
  SSAVariable *EntryDefinitionOf(VirtualRegister reg);

  // Returns the definition of some register on exit from a fragment.
  SSAVariable *ExitDefinitionOf(VirtualRegister reg);

  // Returns a C++11-compatible iterator over the entry definitions.
  inline SSAEntryDefsIterator EntryDefs(void) {
    return SSAEntryDefsIterator(entry_defs);
  }

  // Returns a C++11-compatible iterator over the entry definitions.
  inline SSAExitDefsIterator ExitDefs(void) {
    return SSAExitDefsIterator(exit_defs);
  }

 private:
  // Removes and returns the `SSAVariable` instance associated with a missing
  // definition of `reg`.
  SSAVariable *RemoveEntryDef(VirtualRegister reg);

  // Variables that aren't defined on entry to this fragment.
  FixedSizeLookupTable<VirtualRegister,
                       SSAEntryVariable,
                       MAX_NUM_SSA_VARS> entry_defs;

  // Variables that are defined in this fragment and can reach to the next
  // fragment.
  SSAVariableTable exit_defs;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SSAVariableTracker);
};
#endif
}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_SSA_H_
