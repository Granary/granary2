/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/types.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/driver.h"
#include "granary/environment.h"
#include "granary/mir.h"

namespace granary {
namespace detail {

// Defines a list of a basic blocks within a control-flow graph.
class BasicBlockList {
 public:
  ListHead list;
  std::unique_ptr<BasicBlock> block;

  inline explicit BasicBlockList(BasicBlock *block_)
      : block(block_) {
#ifdef GRANARY_DEBUG
    // Unusual case: the basic block is already linked in to a CFG's basic block
    // list.
    if (GRANARY_UNLIKELY(nullptr != block->list)) {
      granary_break_on_fault();
    }
#endif
    block->list = this;
  }

  // Basic block lists are allocated from a global memory pool using the
  // `new` and `delete` operators.
  GRANARY_DEFINE_NEW_ALLOCATOR(BasicBlockList, {
    SHARED = true,
    ALIGNMENT = 16
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(BasicBlockList);
};

// Move the iterator to the next basic block.
void BasicBlockIterator::operator++(void) {
  BasicBlockList *next(blocks->list.GetNext(blocks));
  do {
    blocks = next;
    next = nullptr;

    // Auto-clean up blocks while iterating over them.
    if (blocks && GRANARY_UNLIKELY(blocks->block->CanDestroy())) {
      next = blocks->list.GetNext(blocks);
      blocks->list.Unlink();
      delete blocks;
    }
  } while (GRANARY_UNLIKELY(nullptr != next));
}

// Get a basic block out of the iterator.
BasicBlock *BasicBlockIterator::operator*(void) const {
  return blocks->block.get();
}

}  // namespace detail

ControlFlowGraph::ControlFlowGraph(Environment *environment_,
                                   AppProgramCounter pc,
                                   BasicBlockMetaData *meta)
      : environment(environment_),
        first_block(nullptr),
        last_block(nullptr) {
  auto block = new InFlightBasicBlock(pc, meta);
  first_block = last_block = new detail::BasicBlockList(block);
  Materialize(block, first_block);

  // The control-flow graph has sole ownership over the initial basic block.
  // All other basic blocks are owned by control-transfer instructions.
  block->Acquire();
}

// Destroy the CFG.
ControlFlowGraph::~ControlFlowGraph(void) {
  for (detail::BasicBlockList *curr(first_block), *next(nullptr);
       curr; curr = next) {
    next = curr->list.GetNext(curr);
    delete curr;
  }
  first_block = last_block = nullptr;
}

// Create a new (future) basic block. This block is left as un-owned and
// will not appear in any iterators until some instruction takes ownership
// of it. This can be achieved by targeting this newly created basic block
// with a CTI.
FutureBasicBlock *ControlFlowGraph::Materialize(
    AppProgramCounter start_pc, const BasicBlockMetaData *meta) {
  auto block = new FutureBasicBlock(start_pc, meta);
  auto list_entry = new detail::BasicBlockList(block);
  InsertAfter(last_block, list_entry);
  return block;
}

BasicBlock *ControlFlowGraph::Materialize(
    const detail::BasicBlockSuccessor &target, const BasicBlockMetaData *meta) {
  GRANARY_UNUSED(target);
  GRANARY_UNUSED(meta);
  return nullptr;
}

BasicBlock *ControlFlowGraph::Materialize(
    const ControlFlowInstruction *instruction, const BasicBlockMetaData *meta) {
  GRANARY_UNUSED(instruction);
  GRANARY_UNUSED(meta);
  return nullptr;
}

namespace {

// A basic block that has not yet been decoded, and which we don't know about
// at this time because it's the target of an indirect jump/call.
static UnknownBasicBlock UNKNOWN_BLOCK;

// Return a control-flow instruction that targets a future basic block.
static Instruction *MakeDirectCTI(driver::DecodedInstruction *instr,
                                  AppProgramCounter target) {
  return new ControlFlowInstruction(
      instr, new FutureBasicBlock(target, nullptr));
}

// Convert a decoded instruction into the internal Granary instruction IR.
static Instruction *RaiseInstruction(
    driver::DecodedInstruction *instr) {

  if (instr->HasIndirectTarget()) {
    return new ControlFlowInstruction(instr, &UNKNOWN_BLOCK);
  } else if (instr->IsJump() || instr->IsFunctionCall()) {
    return MakeDirectCTI(instr, instr->BranchTarget());
  } else {
    return new NativeInstruction(instr);
  }
}

// Decode the list of instructions and appends them to the first instruction in
// a basic block. The last decoded instruction is returned.
static ControlFlowInstruction *DecodeInstructionList(Instruction *instr,
                                                     AppProgramCounter *pc) {
  driver::InstructionDecoder decoder;
  driver::DecodedInstruction dinstr;

  for (AppProgramCounter decoded_pc(*pc);
       !IsA<ControlFlowInstruction *>(instr) && decoder.DecodeNext(&dinstr, pc);
       decoded_pc = *pc) {

    instr = instr->InsertAfter(std::unique_ptr<Instruction>(
        RaiseInstruction(dinstr.Copy())));
  }
  return DynamicCast<ControlFlowInstruction *>(instr);
}

}  // namespace

// Materialize an in-flight basic block by decoding native instructions,
// annotating those instructions, and updating the CFG with new successor
// basic blocks.
void ControlFlowGraph::Materialize(InFlightBasicBlock *block,
                                   detail::BasicBlockList *block_list) {
  auto pc = block->app_start_pc;
  auto cti = DecodeInstructionList(block->FirstInstruction(), &pc);

  if (cti && (cti->IsFunctionCall() || cti->IsConditionalJump())) {
    cti->InsertAfter(mir::Jump(this, pc));
  }

  for (auto instr : block->Instructions()) {
    environment->AnnotateInstruction(instr);
    if (!(cti = DynamicCast<ControlFlowInstruction *>(instr))) {
      continue;
    }

    // Add the nodes into the control-flow graph in pre-order so that if we're
    // iterating over block when we'll see the materialized block(s) next.
    auto next_block = cti->TargetBlock();
    if (!next_block->list) {
      InsertAfter(block_list, new detail::BasicBlockList(next_block));
    }
  }
}

void ControlFlowGraph::InsertAfter(detail::BasicBlockList *list,
                                   detail::BasicBlockList *new_list) {
  list->list.SetNext(list, new_list);
  if (last_block == list) {
    last_block = new_list;
  }
}

}  // namespace granary
