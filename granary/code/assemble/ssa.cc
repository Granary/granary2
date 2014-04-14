/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/container.h"
#include "granary/base/new.h"
#include "granary/base/string.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/ssa.h"

#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"
#include "granary/util.h"

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (SSAVariable, 2),
      (SSARegister, 2 * 3),
      (SSAPhi, 2 * 5),
      (SSAPhiOperand, 2 * 7),
      (SSATrivialPhi, 2 * 11),
      (SSAForward, 2 * 13))

GRANARY_DEFINE_BASE_CLASS(SSAVariable)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSARegister)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSAPhi)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSAPhiOperand)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSATrivialPhi)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSAForward)

// Represents an arbitrary SSA node. All SSA nodes are backed by the same amount
// of memory. This is useful because then an SSAPHI node can be converted into
// an SSATrivialPHI node simply by re-initializing the associated memory.
union SSANode {
  Container<SSARegister> reg;
  Container<SSAPhi> phi;
  Container<SSATrivialPhi> trivial_phi;
  Container<SSAPhiOperand> op;

  GRANARY_DEFINE_NEW_ALLOCATOR(SSANode, {
    SHARED = false,
    ALIGNMENT = 1
  })
} __attribute__((packed));

static_assert(0 == offsetof(SSANode, reg),
    "Invalid structure packing of `union SSANode`.");

static_assert(0 == offsetof(SSANode, phi),
    "Invalid structure packing of `union SSANode`.");

static_assert(0 == offsetof(SSANode, trivial_phi),
    "Invalid structure packing of `union SSANode`.");

static_assert(0 == offsetof(SSANode, op),
    "Invalid structure packing of `union SSANode`.");

// Returns the reaching definition associated with some variable `var`. In the
// case of trivial SSA variables, we follow as many reaching definitions as we
// can to form the
SSAVariable *DefinitionOf(SSAVariable *var) {
  while (auto trivial_phi = DynamicCast<SSATrivialPhi *>(var)) {
    if (trivial_phi->parent) {
      var = trivial_phi->parent;
    } else {
      break;
    }
  }
  return var;
}

// Returns the virtual register associated with some `SSAVariable` instance.
VirtualRegister RegisterOf(SSAVariable *var) {
  switch (var->TypeId()) {
    case kTypeIdSSARegister: {
      auto reg = DynamicCast<SSARegister *>(var);
      return reg->reg;
    }
    case kTypeIdSSAPhi: {
      auto phi = DynamicCast<SSAPhi *>(var);
      return phi->reg;
    }
    case kTypeIdSSAPhiOperand: {
      auto phi_op = DynamicCast<SSAPhiOperand *>(var);
      return RegisterOf(phi_op->Variable());
    }
    case kTypeIdSSATrivialPhi: {
      auto trivial_phi = DynamicCast<SSATrivialPhi *>(var);
      return RegisterOf(DefinitionOf(trivial_phi));
    }
    case kTypeIdSSAForward: {
      auto forward_def = DynamicCast<SSAForward *>(var);
      return RegisterOf(forward_def->parent);
    }
    default:
      GRANARY_ASSERT(false);
      return VirtualRegister();
  }
}

// Get the variable referenced by this operand.
SSAVariable *SSAPhiOperand::Variable(void) {
  return var = DefinitionOf(var);
}

// Add a new reaching definition to this Phi node.
void SSAPhi::AddOperand(SSAVariable *var) {
  var = DefinitionOf(var);
  for (auto op : Operands()) {
    if (op->Variable() == var) {
      return;  // Redundant operand.
    }
  }
  auto ssa_node = new SSANode;
  next = new (ssa_node) SSAPhiOperand(var, next);
}

// Just to get the vtables ;-)
SSATrivialPhi::~SSATrivialPhi(void) {}
SSAForward::~SSAForward(void) {}

namespace {

// Overwrite a generalized PHI node with a different type of SSA variable.
static void InPlaceOverwritePhiNode(SSAPhi *node, SSAVariable *val) {
  // Happens if the initial write to a variable is a read and write
  // (e.g. xor a, a), conditionally written, or partially written as its
  // initial write. In this case we synthesize the operand as-if it's an
  // `SSARegister`.
  if (!val) {
    new (node) SSARegister(node->reg);

  // Happens if we have a def that reaches to a cycle of uses, where within the
  // cycle there is no intermediate def.
  } else {
    new (node) SSATrivialPhi(DefinitionOf(val));
  }
}

// Try to recursively trivialize the operands of a trivial PHI node.
static void TryRecursiveTrivialize(SSAPhiOperand *op) {
  SSAPhiOperand *next_op(nullptr);
  for (; op; op = next_op) {
    next_op = op->next;
    if (auto phi = DynamicCast<SSAPhi *>(op->Variable())) {
      phi->TryTrivialize();
    }
    delete UnsafeCast<SSANode *>(op);
  }
}

// Returns the last `SSAVariable` defined within the fragment `frag` that
// defines the register `reg`.
static SSAVariable *FindDefForUse(Fragment *frag, VirtualRegister reg) {
  for (auto instr : BackwardInstructionIterator(frag->last)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (auto var = DefinitionOf(ninstr, reg)) {
        return var;
      }
    }
  }
  return nullptr;
}

}  // namespace

// Returns the `SSAVariable` associated with a definition of `reg` if this
// instruction defines the register `reg`.
SSAVariable *DefinitionOf(NativeInstruction *instr, VirtualRegister reg) {
  if (auto var = GetMetaData<SSAVariable *>(instr)) {
    while (auto def_forward = DynamicCast<SSAForward *>(var)) {
      if (RegisterOf(def_forward->parent) == reg) {
        return var;
      }
      var = def_forward->next_instr_def;
    }
    if (var && RegisterOf(var) == reg) {
      return var;
    }
  }
  return nullptr;
}

// Try to convert this PHI node into a trivial PHI node. If possible, this
// will cause the memory to go through an "unsafe" type conversion.
void SSAPhi::TryTrivialize(void) {
  SSAVariable *same(nullptr);
  for (auto op : Operands()) {
    const auto op_var = op->Variable();
    if (op_var == same || op_var == this) {
      continue;  // Unique value or self-reference.
    } else if (same) {
      return;  // Merges at least two operands.
    } else {
      same = op_var;
    }
  }
  auto op = ops;
  InPlaceOverwritePhiNode(this, same);
  TryRecursiveTrivialize(op);
}

VirtualRegister LookupTableOperations<VirtualRegister,
                                      SSAVariable *>::KeyForValue(
                                          SSAVariable *var) {
  return var ? RegisterOf(var) : VirtualRegister();
}

SSAVariableTracker::SSAVariableTracker(void)
    : entry_defs(),
      exit_defs() {
  memset(this, 0, sizeof *this);
}

SSAVariableTracker::~SSAVariableTracker(void) {
  for (auto def : entry_defs) {
    if (def.is_owned) {
      delete UnsafeCast<SSANode *>(def.var);
    }
  }
}

// Add in a concrete definition for a virtual register, where the register
// being defined is read and written, or conditionally written, and therefore
// should share the same storage and any definitions that reach the current
// definition.
SSAVariable *SSAVariableTracker::AddInheritingDefinition(
    VirtualRegister reg, SSAVariable *existing_instr_def) {
  // New PHI node representing the value that is read and written, or
  // coniditionally written.
  const auto new_entry_def = new (new SSANode) SSAPhi(reg);

  // Make a forward def out of either the old missing def, or some new memory
  // if the variable was not yet used in this fragment.
  auto entry_def = entry_defs.Find(reg);
  void *current_def_mem = entry_def->var;
  if (!current_def_mem) {
    current_def_mem = new SSANode;
  }
  auto current_def = new (current_def_mem) SSAForward(new_entry_def,
                                                      existing_instr_def);

  // Add in the missing definition that is associated with the forward variable.
  entry_def->var = new_entry_def;
  entry_def->is_owned = true;
  return current_def;
}

// Add in a concrete definition for an architectural register. If a matching
// definition is present in the `entry_defs` table, then the definition
// there is modified in-place to represent the new definition, removed from
// the `entry_defs` table, and returned.
SSAVariable *SSAVariableTracker::AddSimpleDefinition(VirtualRegister reg,
                                                     NativeInstruction *instr) {
  void *node_mem = RemoveMissingDef(reg);
  if (!node_mem) {
    node_mem = new SSANode;
  }
  return new (node_mem) SSARegister(reg, instr);
}

// Declare that a register is being used. This adds a definition of the
// variable into the `entry_defs` table.
void SSAVariableTracker::DeclareUse(VirtualRegister reg) {
  auto entry_def = entry_defs.Find(reg);
  if (!entry_def->var) {
    entry_def->var = new (new SSANode) SSAPhi(reg);
    entry_def->is_owned = true;
  }
}

// Promote missing definitions associated with uses in a fragment into live
// definitions that leave the fragment.
void SSAVariableTracker::PromoteMissingDefinitions(void) {
  for (auto entry_def : entry_defs) {
    if (entry_def.var) {
      auto exit_def = exit_defs.Find(RegisterOf(entry_def.var));
      if (!*exit_def) {
        *exit_def = entry_def.var;
      }
    }
  }
}

// Propagate definitions from one SSA variable table into another. The source
// table is treated as being the successor of the current table, hence the
// current is the predecessor of the source table.
bool SSAVariableTracker::BackPropagateMissingDefsForUses(
    Fragment *predecessor, SSAVariableTracker *source) {
  auto changed = false;
  for (auto &source_entry_def : source->entry_defs) {
    if (source_entry_def.var && source_entry_def.is_owned) {
      auto reg = RegisterOf(source_entry_def.var);

      // Predecessor already defines it. A different successor has done the
      // lookup process for us. This could mean a true definition, or just an
      // inject definition via a missing def in another successor.
      auto exit_def = exit_defs.Find(reg);
      if (*exit_def) {
        continue;
      }

      changed = true;

      // Predecessor already defines it.
      if (auto def = FindDefForUse(predecessor, reg)) {
        *exit_def = def;
        continue;
      }

      // Predecessor uses it, but does not define it.
      auto entry_use = entry_defs.Find(reg);
      if (entry_use->var) {
        *exit_def = entry_use->var;
        continue;
      }

      // Predecessor neither defines nor uses it.
      entry_use->var = new (new SSANode) SSAPhi(reg);
      entry_use->is_owned = true;
      *exit_def = entry_use->var;
    }
  }
  return changed;
}

// For each PHI node in the destination table, add an operand to that PHI
// from the current table.
void SSAVariableTracker::AddPhiOperands(SSAVariableTracker *dest) {
  for (auto var : dest->entry_defs) {
    if (auto phi = DynamicCast<SSAPhi *>(var.var)) {
      auto def = exit_defs.Find(phi->reg);
      phi->AddOperand(*def);
    }
  }
}

// Simplify all PHI nodes.
void SSAVariableTracker::SimplifyPhiNodes(void) {
  for (auto var : entry_defs) {
    if (auto phi = DynamicCast<SSAPhi *>(var.var)) {
      phi->TryTrivialize();
    }
  }
}

// Copy all entry definitions in this variable tracker into an SSA variable
// table.
void SSAVariableTracker::CopyEntryDefinitions(SSAVariableTable *vars) {
  memset(vars, 0, sizeof *vars);
  for (auto def : entry_defs) {
    if (def.var) {
      auto var_def = DefinitionOf(def.var);
      *vars->Find(RegisterOf(var_def)) = var_def;
    }
  }
}

// Returns the definition of some register on entry to a fragment.
SSAVariable *SSAVariableTracker::EntryDefinitionOf(VirtualRegister reg) {
  return entry_defs.Find(reg)->var;
}

// Removes and returns the `SSAVariable` instance associated with a missing
// definition of `reg`.
SSAVariable *SSAVariableTracker::RemoveMissingDef(VirtualRegister reg) {
  auto entry_def = entry_defs.Find(reg);
  auto var = entry_def->var;
  entry_def->var = nullptr;
  entry_def->is_owned = false;
  return var;
}

}  // namespace granary

