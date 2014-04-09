/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/container.h"
#include "granary/base/new.h"
#include "granary/base/string.h"

#include "granary/code/assemble/ssa.h"

#include "granary/breakpoint.h"

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (SSAVariable, 2),
      (SSARegister, 2 * 3),
      (SSAPhi, 2 * 5),
      (SSAPhiOperand, 2 * 7),
      (SSATrivialPhi, 2 * 11))

GRANARY_DEFINE_BASE_CLASS(SSAVariable)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSARegister)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSAPhi)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSAPhiOperand)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSAVariable, SSATrivialPhi)

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

namespace {

// Returns the reaching definition associated with some variable `var`. In the
// case of trivial SSA variables, we follow as many reaching definitions as we
// can to form the
static SSAVariable *DefinitionOf(SSAVariable *var) {
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
static VirtualRegister RegisterOf(SSAVariable *var) {
  switch (var->TypeId()) {
    case kTypeIdSSARegister: {
      auto reg = DynamicCast<SSARegister *>(var);
      return reg->reg;
    }
    case kTypeIdSSAPhi: {
      auto phi = DynamicCast<SSAPhi *>(var);
      return phi->reg;
    }
    case kTypeIdSSATrivialPhi: {
      auto trivial_phi = DynamicCast<SSATrivialPhi *>(var);
      return RegisterOf(DefinitionOf(trivial_phi));
    }
    case kTypeIdSSAPhiOperand: {
      auto phi_op = DynamicCast<SSAPhiOperand *>(var);
      return RegisterOf(phi_op->Variable());
    }
    default:
      GRANARY_ASSERT(false);
      return VirtualRegister();
  }
}

}  // namespace

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

// Just to get the vtable ;-)
SSATrivialPhi::~SSATrivialPhi(void) {}

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

}  // namespace

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

SSAVariableTable::SSAVariableTable(void) {
  memset(this, 0, sizeof *this);
}

SSAVariableTable::~SSAVariableTable(void) {
  for (auto i = 0; i < NUM_SLOTS; ++i) {
    if (missing_defs[i] && owns_missing_def[i]) {
      delete UnsafeCast<SSANode *>(missing_defs[i]);
    }
  }
}

// Add in a concrete definition for a virtual register, where the register
// being defined is read and written, or conditionally written, and therefore
// should share the same storage and any definitions that reach the current
// definition.
SSAVariable *SSAVariableTable::AddInheritingDefinition(VirtualRegister reg) {
  auto node_mem = new SSANode;
  if (auto existing_var = GetVar(reg, missing_defs)) {
    return new (node_mem) SSATrivialPhi(existing_var);
  } else {
    DeclareUse(reg);
    return AddInheritingDefinition(reg);
  }
}

// Add in a concrete definition for an architectural register. If a matching
// definition is present in the `missing_defs` table, then the definition
// there is modified in-place to represent the new definition, removed from
// the `missing_defs` table, and returned.
SSAVariable *SSAVariableTable::AddSimpleDefinition(VirtualRegister reg) {
  void *node_mem = RemoveMissingDef(reg);
  if (!node_mem) {
    node_mem = new SSANode;
  }
  auto def = new (node_mem) SSARegister(reg);
  auto i = GetVarIndex(reg, live_defs);
  if (!live_defs[i]) {
    live_defs[i] = def;
  }
  return def;
}

// Declare that a register is being used. This adds a definition of the
// variable into the `missing_defs` table.
void SSAVariableTable::DeclareUse(VirtualRegister reg) {
  auto i = GetVarIndex(reg, missing_defs);
  if (!missing_defs[i]) {
    auto ssa_node = new SSANode;
    missing_defs[i] = new (ssa_node) SSAPhi(reg);
    owns_missing_def[i] = true;
  }
}

// Promote missing definitions associated with uses in a fragment into live
// definitions that leave the fragment.
void SSAVariableTable::PromoteMissingDefinitions(void) {
  for (auto missing_def : missing_defs) {
    if (missing_def) {
      auto i = GetVarIndex(RegisterOf(missing_def), live_defs);
      if (!live_defs[i]) {
        live_defs[i] = missing_def;
      }
    }
  }
}

// Propagate definitions from one SSA variable table into another. This only
// propagates definitions if they are missing in the `dest` table's
// `missing_defs` table. If the destination table has multiple predecessors
// then a PHI node is propagated in place of the definition from the current
// table.
bool SSAVariableTable::PropagateMissingDefinitions(SSAVariableTable *dest,
                                                   int dest_num_predecessors) {
  bool changed(false);
  for (auto def : live_defs) {
    if (def) {
      auto reg = RegisterOf(def);
      auto i = dest->GetVarIndex(reg, dest->missing_defs);
      auto &dest_def(dest->missing_defs[i]);
      if (!dest_def) {
        changed = true;
        if (1 == dest_num_predecessors) {
          dest_def = def;
        } else {
          auto ssa_node = new SSANode;
          dest_def = new (ssa_node) SSAPhi(reg);
          dest->owns_missing_def[i] = true;
        }
      }
    }
  }
  if (changed) {
    dest->PromoteMissingDefinitions();
  }
  return changed;
}

// For each PHI node in the destination table, add an operand to that PHI
// from the current table.
void SSAVariableTable::AddPhiOperands(SSAVariableTable *dest) {
  for (auto var : dest->missing_defs) {
    if (auto phi = DynamicCast<SSAPhi *>(var)) {
      phi->AddOperand(GetVar(phi->reg, live_defs));
    }
  }
}

// Simplify all PHI nodes.
void SSAVariableTable::SimplifyPhiNodes(void) {
  for (auto var : missing_defs) {
    if (auto phi = DynamicCast<SSAPhi *>(var)) {
      phi->TryTrivialize();
    }
  }
}

// Returns the index into one of the storage SSA variable hash tables where
// the `SSAVariable` associated with `reg` exists, or where it should
// go.
int SSAVariableTable::GetVarIndex(VirtualRegister reg,
                                  SSAVariable * const *tab) {
  for (int i = reg.Number(), max_i = i + NUM_SLOTS; i < max_i; ++i) {
    auto index = i % NUM_SLOTS;
    if (!tab[index] || RegisterOf(tab[index]) == reg) {
      return index;
    }
  }
  GRANARY_ASSERT(false);
  return 0;
}

// Returns a reference to the `SSAVariable` instance associated with `reg` in
// the SSA varable hash table `tab`.
SSAVariable *&SSAVariableTable::GetVar(VirtualRegister reg,
                                       SSAVariable **tab) {
  return tab[GetVarIndex(reg, tab)];
}

// Removes and returns the `SSAVariable` instance associated with a missing
// definition of `reg`.
SSAVariable *SSAVariableTable::RemoveMissingDef(VirtualRegister reg) {
  auto i = GetVarIndex(reg, missing_defs);
  if (missing_defs[i]) {
    auto def = missing_defs[i];
    missing_defs[i] = nullptr;
    owns_missing_def[i] = false;
    auto prev_index = i++;
    for (auto max_i = i + NUM_SLOTS; i < max_i; ++i) {
      auto index = i % NUM_SLOTS;
      if (!missing_defs[index] || RegisterOf(missing_defs[index]) != reg) {
        break;
      } else {
        missing_defs[prev_index] = missing_defs[index];
        owns_missing_def[prev_index] = owns_missing_def[index];
        owns_missing_def[index] = false;
        prev_index = index;
      }
    }
    return def;
  } else {
    return nullptr;
  }
}

}  // namespace granary

