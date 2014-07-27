/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/new.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/ssa.h"

#include "granary/util.h"

#define GRANARY_DEFINE_SSA_ALLOCATOR(cls) \
  void *cls::operator new(std::size_t) { \
    return new SSANodeMemory; \
  } \
  void *cls::operator new(std::size_t, void *mem) { \
    return mem; \
  } \
  void cls::operator delete(void *address) { \
    delete reinterpret_cast<SSANodeMemory *>(address); \
  }

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (SSANode, 2),
      (SSAControlPhiNode, 2 * 3),
      (SSAAliasNode, 2 * 5),
      (SSADataPhiNode, 2 * 7),
      (SSARegisterNode, 2 * 11))

GRANARY_DEFINE_BASE_CLASS(SSANode)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSANode, SSAControlPhiNode)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSANode, SSAAliasNode)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSANode, SSADataPhiNode)
GRANARY_DEFINE_DERIVED_CLASS_OF(SSANode, SSARegisterNode)

SSANode::SSANode(SSAFragment *frag_, VirtualRegister reg_)
    : id(NODE_UNSCHEDULED),
      frag(frag_),
      reg(reg_) {
  GRANARY_ASSERT(reg.IsGeneralPurpose());
}

SSANode::~SSANode(void) {}

SSAControlPhiNode::SSAControlPhiNode(SSAFragment *frag_, VirtualRegister reg_)
    : SSANode(frag_, reg_),
      operands() {}

// Add an operand to the PHI node.
void SSAControlPhiNode::AddOperand(SSANode *node) {
  if (node) {
    for (auto op_node : operands) {
      if (op_node == node) {
        return;
      }
    }
    id.Union(reinterpret_cast<SSANode *>(this), node);
    operands.Append(node);
  }
}

namespace {

// Finds the annotation instruction that "defines" the PHI node.
static Instruction *FindDefiningInstruction(SSAControlPhiNode *phi) {
  for (auto instr : InstructionListIterator(phi->frag->instrs)) {
    if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
      if (IA_SSA_NODE_DEF == ainstr->annotation) {
        if (ainstr->Data<SSANode *>() == phi) return instr;
        continue;
      }
    }

    // If we reach here then it's either not an annotation instruction, or not
    // the right kind of annotation. `6_track_ssa_vars` ensures that all of
    // the annotations are prepended to the fragments, so don't do useless
    // searching.
    break;
  }
  GRANARY_ASSERT(false);
  return nullptr;
}

// Try to recursively trivialize the operands of a trivial PHI node.
//
// Note: The `phi_operand` will never be an `SSAAliasNode` node.
static void TryRecursiveTrivialize(SSANode *phi_operand) {
  if (auto phi = DynamicCast<SSAControlPhiNode *>(phi_operand)) {
    phi->UnsafeTryTrivialize();
  }
}

// Overwrite a generalized PHI node with a different type of SSA variable.
static void UnsafeTrivializePhiNode(SSAControlPhiNode *phi,
                                    SSANode *phi_operand) {
  // Save the storage set, so that we can make sure everything continues to
  // link up after our conversion.
  auto storage = phi->id;

  // Make sure that memory associated with the operands is cleaned up.
  phi->~SSAControlPhiNode();

  // Happens if the initial write to a variable is a read and write
  // (e.g. xor a, a), conditionally written, or partially written as its
  // initial write. In this case we synthesize the operand as-if it's an
  // `SSARegisterNode`.
  if (!phi_operand) {
    auto reg = new (phi) SSARegisterNode(phi->frag,
                                         FindDefiningInstruction(phi),
                                         phi->reg);
    reg->id = storage;

  // Happens if we have a def that reaches to a cycle of uses, where within the
  // cycle there is no intermediate def.
  } else {
    auto alias = new (phi) SSAAliasNode(phi->frag, phi_operand);
    alias->id = storage;
    TryRecursiveTrivialize(phi_operand);
  }
}

}  // namespace

// Try to convert this PHI node into an alias or a register node. If this
// succeeds at trivializing the PHI node then `true` is returned, otherwise
// `false` is returned.
bool SSAControlPhiNode::UnsafeTryTrivialize(void) {
  SSANode *only_operand(nullptr);
  for (auto op_node : operands) {
    auto unaliased_operand = UnaliasedNode(op_node);
    if (unaliased_operand == only_operand || unaliased_operand == this) {
      continue;  // Unique value or self-reference.
    } else if (only_operand) {
      return false;  // Merges at least two operands.
    } else {
      only_operand = unaliased_operand;
    }
  }
  // Perform unsafe conversion to a register or alias node.
  UnsafeTrivializePhiNode(this, only_operand);
  return true;
}

SSAAliasNode::SSAAliasNode(SSAFragment *frag_, SSANode *incoming_node_)
    : SSANode(frag_, incoming_node_->reg),
      aliased_node(incoming_node_) {}

SSADataPhiNode::SSADataPhiNode(SSAFragment *frag_, SSANode *incoming_node_)
    : SSANode(frag_, incoming_node_->reg),
      dependent_node(incoming_node_) {}

SSARegisterNode::SSARegisterNode(SSAFragment *frag_, Instruction *instr_,
                                 VirtualRegister reg_)
    : SSANode(frag_, reg_),
      instr(instr_) {}

// Enough memory to hold an arbitrary `SSANode`.
union SSANodeMemory {
 public:
  alignas(SSAControlPhiNode) char _1[sizeof(SSAControlPhiNode)];
  alignas(SSAAliasNode) char _2[sizeof(SSAAliasNode)];
  alignas(SSADataPhiNode) char _3[sizeof(SSADataPhiNode)];
  alignas(SSARegisterNode) char _4[sizeof(SSARegisterNode)];

  GRANARY_DEFINE_NEW_ALLOCATOR(SSANodeMemory, {
    SHARED = false,
    ALIGNMENT = 1
  })
};

GRANARY_DEFINE_SSA_ALLOCATOR(SSAControlPhiNode)
GRANARY_DEFINE_SSA_ALLOCATOR(SSAAliasNode)
GRANARY_DEFINE_SSA_ALLOCATOR(SSADataPhiNode)
GRANARY_DEFINE_SSA_ALLOCATOR(SSARegisterNode)

#undef GRANARY_DEFINE_SSA_ALLOCATOR

SSAOperand::SSAOperand(void)
    : operand(nullptr),
      nodes(),
      action(SSAOperandAction::INVALID),
      is_reg(false) {}

SSAInstruction::SSAInstruction(void)
    : defs(),
      uses() {}

SSAInstruction::~SSAInstruction(void) {
  for (auto &def : defs) {
    if (SSAOperandAction::WRITE == def.action) {
      delete def.nodes[0];
    }
  }
  for (auto &use : uses) {
    if (SSAOperandAction::READ_WRITE == use.action) {
      if (IsA<SSADataPhiNode *>(use.nodes[0])) {
        delete use.nodes[0];
      }
    }
  }
}

namespace {

// Returns a pointer to the `SSANode` that is used to define the register `reg`
// in the instruction `instr`, or `nullptr` if the register is not defined by
// the instruction.
static SSANode *DefinedNodeForReg(NativeInstruction *instr,
                                  VirtualRegister reg) {
  if (auto ssa_instr = GetMetaData<SSAInstruction *>(instr)) {
    for (auto &op : ssa_instr->defs) {
      if (SSAOperandAction::WRITE == op.action) {
        auto node = op.nodes[0];
        if (node->reg == reg) return node;
      } else {  // `SSAOperandAction::CLEARED`.
        break;
      }
    }
    for (auto &op : ssa_instr->uses) {
      if (SSAOperandAction::READ_WRITE == op.action) {
        auto node = op.nodes[0];
        if (node->reg == reg) return node;
      } else {  // `SSAOperandAction::READ`.
        break;
      }
    }
  }
  return nullptr;
}

// Returns a pointer to the `SSANode` that is defined by a special annotation
// within a fragment's instruction list.
static SSANode *DefinedNodeForReg(AnnotationInstruction *instr,
                                  VirtualRegister reg) {
  if (IA_SSA_NODE_DEF == instr->annotation) {
    auto node = instr->Data<SSANode *>();
    if (node->reg == reg) {
      return node;
    }
  }
  return nullptr;
}

}  // namespace

// Returns a pointer to the `SSANode` that is used to define the register `reg`
// in the instruction `instr`, or `nullptr` if the register is not defined by
// the instruction.
SSANode *DefinedNodeForReg(Instruction *instr, VirtualRegister reg) {
  if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
    return DefinedNodeForReg(ninstr, reg);
  } else if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
    return DefinedNodeForReg(ainstr, reg);
  } else {
    return nullptr;
  }
}

// Returns the un-aliased node associated with the current node.
SSANode *UnaliasedNode(SSANode *node) {
  if (auto alias = DynamicCast<SSAAliasNode *>(node)) {
    return UnaliasedNode(alias->aliased_node);
  } else {
    return node;
  }
}

}  // namespace granary
