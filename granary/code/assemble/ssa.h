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
  SSANode(SSAFragment *frag_, VirtualRegister reg_);

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

// Returns a pointer to the `SSANode` that is defined at this instruction.
SSANode *DefinedNode(Instruction *instr);

// Returns the un-aliased node associated with the current node.
SSANode *UnaliasedNode(SSANode *node);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_SSA_H_
