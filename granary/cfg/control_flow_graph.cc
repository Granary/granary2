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
#include "granary/metadata.h"
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
    granary_break_on_fault_if(GRANARY_UNLIKELY(nullptr != block->list));
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
  BasicBlockList *curr(blocks->list.GetNext(blocks));
  BasicBlockList *next(nullptr);

  // Auto-clean up blocks while iterating over them.
  for (; curr && curr->block->CanDestroy(); curr = next) {
    next = curr->list.GetNext(curr);
    curr->list.Unlink();
    delete curr;
  }

  blocks = curr;
}

// Get a basic block out of the iterator.
BasicBlock *BasicBlockIterator::operator*(void) const {
  return blocks->block.get();
}

}  // namespace detail

// Initialize the control flow graph with a single in-flight basic block.
LocalControlFlowGraph::LocalControlFlowGraph(Environment *environment_,
                                   AppProgramCounter pc,
                                   GenericMetaData *meta)
      : environment(environment_),
        entry_block(new InFlightBasicBlock(pc, meta)),
        first_block(nullptr),
        last_block(nullptr) {
  first_block = last_block = new detail::BasicBlockList(entry_block);
  MaterializeInFlight(entry_block, first_block);

  // The control-flow graph has sole ownership over the initial basic block.
  // All other basic blocks are owned by control-transfer instructions.
  entry_block->Acquire();
}

// Destroy the CFG.
LocalControlFlowGraph::~LocalControlFlowGraph(void) {

  // Start by marking every block as owned; we're destroying them anyway so
  // this sets up a simple invariant regarding the interaction between freeing
  // instructions and basic blocks.
  for (detail::BasicBlockList *curr(first_block);
       curr; curr = curr->list.GetNext(curr)) {
    curr->block->Acquire();
    granary_break_on_fault_if(curr->block->list != curr);
  }

  // Free up all of the instruction lists.
  for (detail::BasicBlockList *curr(first_block);
       curr; curr = curr->list.GetNext(curr)) {
    auto in_flight_block = DynamicCast<InFlightBasicBlock *>(curr->block.get());
    if (in_flight_block) {
      in_flight_block->FreeInstructionList();
    }
  }

  // Free up all the basic blocks.
  for (detail::BasicBlockList *curr(first_block), *next(nullptr);
       curr; curr = next) {
    next = curr->list.GetNext(curr);
    delete curr;
  }

  first_block = last_block = nullptr;
}

// Return the entry basic block of this control-flow graph.
InFlightBasicBlock *LocalControlFlowGraph::EntryBlock(void) const {
  return entry_block;
}

// Returns an object that can be used inside of a range-based for loop.
detail::BasicBlockIterator LocalControlFlowGraph::Blocks(void) const {
  return detail::BasicBlockIterator(first_block);
}

// Create a new (future) basic block. This block is left as un-owned and
// will not appear in any iterators until some instruction takes ownership
// of it. This can be achieved by targeting this newly created basic block
// with a CTI.
FutureBasicBlock *LocalControlFlowGraph::Materialize(AppProgramCounter start_pc) {
  auto meta = new GenericMetaData;
  auto block = new FutureBasicBlock(start_pc, meta);
  auto list_entry = new detail::BasicBlockList(block);
  InsertAfter(last_block, list_entry);
  return block;
}

// Materialize a potentially new basic block given a basic block successor
// (an instruction+target block) pair.
BasicBlock *LocalControlFlowGraph::Materialize(detail::BasicBlockSuccessor &target) {
  granary_break_on_fault_if(target.block != target.cti->TargetBlock());
  auto block = Materialize(target.cti);
  const_cast<BasicBlock *&>(target.block) = block;
  return block;
}

// Materialize a potentially new basic block given a CTI (that points to the
// block to be materialized) and the metadata to use when materializing the
// block.
BasicBlock *LocalControlFlowGraph::Materialize(const ControlFlowInstruction *cti) {
  auto old_block = DynamicCast<FutureBasicBlock *>(cti->TargetBlock());
  granary_break_on_fault_if(!old_block || !old_block->list);

  // We've already materialized this basic block in this session.
  auto target_pc = old_block->app_start_pc;
  auto meta = old_block->meta;
  auto found_block = FindMaterialized(target_pc, meta, old_block);

  // TODO(pag): Meta-data driven transitions!!! Need to make it possible to
  //            materialize a native instruction.

  // Don't have the block; go decode it.
  if (!found_block) {
    auto new_block = new InFlightBasicBlock(target_pc, meta);
    auto block_list = old_block->list;

    cti->ChangeTarget(new_block);
    block_list = InsertAfter(block_list, new detail::BasicBlockList(new_block));
    MaterializeInFlight(new_block, block_list);
    return new_block;

  // Got the block; change the target in the CTI.
  } else {
    cti->ChangeTarget(found_block);
    return found_block;
  }
}

namespace {

// Return a control-flow instruction that targets a future basic block.
static Instruction *MakeDirectCTI(driver::DecodedInstruction *instr,
                                  AppProgramCounter target) {
  return new ControlFlowInstruction(
      instr, new FutureBasicBlock(target, nullptr));
}

// Convert a decoded instruction into the internal Granary instruction IR.
static std::unique_ptr<Instruction> RaiseInstruction(
    driver::DecodedInstruction *instr) {
  Instruction *ret_instr(nullptr);
  if (instr->HasIndirectTarget()) {
    ret_instr = new ControlFlowInstruction(instr, new UnknownBasicBlock);
  } else if (instr->IsJump() || instr->IsFunctionCall()) {
    ret_instr = MakeDirectCTI(instr, instr->BranchTarget());
  } else {
    ret_instr = new NativeInstruction(instr);
  }
  return std::unique_ptr<Instruction>(ret_instr);
}

}  // namespace

// Decode the list of instructions and appends them to the first instruction in
// a basic block. The last decoded instruction is returned.
void LocalControlFlowGraph::DecodeInstructionList(Instruction *instr,
                                             AppProgramCounter pc) {
  driver::InstructionDecoder decoder;
  driver::DecodedInstruction dinstr;
  bool synthesize_native_jump(false);

  for (; !IsA<ControlFlowInstruction *>(instr); ) {
    if (!decoder.DecodeNext(&dinstr, &pc)) {
      synthesize_native_jump = true;
      break;
    }
    instr = instr->InsertAfter(std::move(RaiseInstruction(dinstr.Copy())));
  }

  if (synthesize_native_jump) {
    // TODO(pag): Implement this!!
  }

  // Synthesize a fall-through jump.
  ControlFlowInstruction *cti(DynamicCast<ControlFlowInstruction *>(instr));
  if (cti->IsFunctionCall() || cti->IsConditionalJump()) {
    cti->InsertAfter(mir::Jump(this, pc));
  }
}

// Materialize an in-flight basic block by decoding native instructions,
// annotating those instructions, and updating the CFG with new successor
// basic blocks.
void LocalControlFlowGraph::MaterializeInFlight(InFlightBasicBlock *block,
                                           detail::BasicBlockList *block_list) {
  ControlFlowInstruction *cti(nullptr);
  DecodeInstructionList(block->FirstInstruction(), block->app_start_pc);

  // Attach the targeted future basic blocks into the control-flow graph.
  for (auto instr : block->Instructions()) {
    environment->AnnotateInstruction(instr);
    if (!(cti = DynamicCast<ControlFlowInstruction *>(instr))) {
      continue;
    }

    // Blocks targeted by MIR instructions are already in the list.
    auto next_block = cti->TargetBlock();
    if (!next_block->list) {
      InsertAfter(block_list, new detail::BasicBlockList(next_block));
    }
  }
}

// Insert a basic block list `new_list` after another existing list `list`.
detail::BasicBlockList *LocalControlFlowGraph::InsertAfter(
    detail::BasicBlockList *list, detail::BasicBlockList *new_list) {
  list->list.SetNext(list, new_list);
  if (last_block == list) {
    last_block = new_list;
  }
  return new_list;
}

// Figure out if we've already materialized this basic block. If so, return
// the already materialized basic block. Otherwise, return `nullptr`.
BasicBlock *LocalControlFlowGraph::FindMaterialized(
    AppProgramCounter target_pc,
    const GenericMetaData *meta,
    const BasicBlock * const ignore_block) const {

  InstrumentedBasicBlock *target_block(nullptr);
  for (auto block : Blocks()) {
    if (block == ignore_block ||
        block->app_start_pc != target_pc ||
        !(target_block = DynamicCast<InstrumentedBasicBlock *>(block))) {
      continue;
    }

    // It can happen that when we materialize one basic block, there are other
    // links to that block in the control-flow graph that we haven't considered.
    if (meta == target_block->meta && !IsA<FutureBasicBlock *>(block)) {
      return block;
    }

    if (meta->Equals(target_block->meta)) {
      return block;
    }
  }

  // TODO(pag): Look for the block in the code cache!
  return nullptr;
}

}  // namespace granary
