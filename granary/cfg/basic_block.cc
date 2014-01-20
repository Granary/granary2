/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/meta_data.h"

namespace granary {
namespace detail {

SuccessorBlockIterator::~SuccessorBlockIterator(void) {
  block = nullptr;
  next_block = nullptr;
  data = nullptr;
}

bool SuccessorBlockIterator::operator!=(
    const SuccessorBlockIterator &that) const {
  return next_block != that.next_block;
}

BasicBlock *SuccessorBlockIterator::operator*(void) {
  return next_block;
}

const SuccessorBlockIterator &SuccessorBlockIterator::operator++(void) {
  next_block = block->FindNextSuccessor(&data);
  return *this;
}

SuccessorBlockIterator::SuccessorBlockIterator(BasicBlock *block_, void *data_)
    : block(block_),
      next_block(nullptr),
      data(data_) {
  next_block = block->FindNextSuccessor(&data);
}

}  // namespace detail

// Initialize a basic block.
BasicBlock::BasicBlock(AppProgramCounter app_start_pc_)
    : app_start_pc(app_start_pc_) {}

// By default, basic blocks don't know about any of their successors. This
// default applies to `FutureBasicBlock` as well as `UnknownBasicBlock`
// instances.
detail::SuccessorBlockFinder BasicBlock::Successors(void) {
  return detail::SuccessorBlockFinder(nullptr, nullptr);
}

// By default, basic blocks don't know about any of their successors. This
// default applies to `FutureBasicBlock` as well as `UnknownBasicBlock`
// instances.
BasicBlock *BasicBlock::FindNextSuccessor(void **) {
  return nullptr;
}

// Initialize an instrumented basic block.
InstrumentedBasicBlock::InstrumentedBasicBlock(
    AppProgramCounter app_start_pc_, const BasicBlockMetaData *entry_meta_,
    BasicBlockMetaData *meta_)
    : BasicBlock(app_start_pc_),
      entry_meta(entry_meta_),
      meta(meta_) {}

// Initialize a cached basic block.
CachedBasicBlock::CachedBasicBlock(AppProgramCounter app_start_pc_,
                                   CacheProgramCounter cache_start_pc_,
                                   const BasicBlockMetaData *entry_meta_,
                                   BasicBlockMetaData *meta_,
                                   const BasicBlock **successors_)
    : InstrumentedBasicBlock(app_start_pc_, entry_meta_, meta_),
      cache_start_pc(cache_start_pc_),
      successors(successors_) {}

// Return a finder for the successors of a cached basic block.
detail::SuccessorBlockFinder CachedBasicBlock::Successors(void) {
  return detail::SuccessorBlockFinder(this, UnsafeCast<void *>(successors));
}

// Return the current successor in the successor array and move the pointer to
// the next successor in the array.
BasicBlock *CachedBasicBlock::FindNextSuccessor(void **data) {
  auto successor(reinterpret_cast<BasicBlock ***>(data));
  return *((*successor)++);
}

InFlightBasicBlock::InFlightBasicBlock(AppProgramCounter app_start_pc_,
                                       BasicBlockMetaData *entry_meta_,
                                       BasicBlockMetaData *meta_,
                                       Instruction *instructions_)
    : InstrumentedBasicBlock(app_start_pc_, entry_meta_, meta_),
      instructions(instructions_) {}

// Return a finder for the successors of a cached basic block.
detail::SuccessorBlockFinder InFlightBasicBlock::Successors(void) {
  return detail::SuccessorBlockFinder(this, instructions);
}

// Return the next successor by iterating through the instructions in the
// basic block.
BasicBlock *InFlightBasicBlock::FindNextSuccessor(void **data) {
  auto successor(reinterpret_cast<Instruction **>(data));
  auto curr(*successor);
  NonLocalControlFlowInstruction *instr(nullptr);
  BasicBlock *block(nullptr);
  for (; curr && !block; curr = curr->Next()) {
    if ((instr = DynamicCast<NonLocalControlFlowInstruction *>(curr)) &&
        !instr->IsFunctionCall()) {
      block = instr->target;
    }
  }
  *successor = curr;
  return block;
}

}  // namespace granary
