/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/metadata.h"

namespace granary {
namespace detail {

// Return the next successor by iterating through the instructions in the
// basic block.
namespace {
static Instruction *FindNextSuccessorInstruction(Instruction *instr) {
  for (Instruction *curr(instr->Next()); curr; curr = curr->Next()) {
    if (IsA<ControlFlowInstruction *>(curr)) {
      return curr;
    }
  }
  return nullptr;
}
}  // namespace

SuccessorBlockIterator::SuccessorBlockIterator(Instruction *instr_)
    : cursor(FindNextSuccessorInstruction(instr_)) {}

BasicBlockSuccessor SuccessorBlockIterator::operator*(void) const {
  auto cti(DynamicCast<ControlFlowInstruction *>(cursor));
  return BasicBlockSuccessor(cti, cti->target);
}

void SuccessorBlockIterator::operator++(void) {
  cursor = FindNextSuccessorInstruction(cursor);
}

void ForwardInstructionIterator::operator++(void) {
  instr = instr->Next();
}

void BackwardInstructionIterator::operator++(void) {
  instr = instr->Previous();
}

}  // namespace detail

// Initialize an instrumented basic block.
InstrumentedBasicBlock::InstrumentedBasicBlock(
    AppProgramCounter app_start_pc_, const BasicBlockMetaData *entry_meta_)
    : BasicBlock(app_start_pc_),
      entry_meta(entry_meta_) {
  GRANARY_UNUSED(entry_meta);  // TODO(pag): Use the entry metadata.
}

// Initialize a cached basic block.
CachedBasicBlock::CachedBasicBlock(AppProgramCounter app_start_pc_,
                                   CacheProgramCounter cache_start_pc_,
                                   const BasicBlockMetaData *entry_meta_)
    : InstrumentedBasicBlock(app_start_pc_, entry_meta_),
      cache_start_pc(cache_start_pc_) {}

// Clean up the memory of an in-flight basic block.
InFlightBasicBlock::~InFlightBasicBlock(void) {
  for (Instruction *instr(first), *next(nullptr); instr; instr = next) {
    next = instr->Next();
    delete instr;
  }
}

// Initialize an in-flight basic block.
InFlightBasicBlock::InFlightBasicBlock(AppProgramCounter app_start_pc_,
                                       BasicBlockMetaData *entry_meta_)
    : InstrumentedBasicBlock(app_start_pc_, entry_meta_),
      meta(entry_meta_->Copy()),
      first(new AnnotationInstruction(BEGIN_BASIC_BLOCK)),
      last(new AnnotationInstruction(END_BASIC_BLOCK)) {
  first->InsertAfter(std::unique_ptr<Instruction>(last));
}

}  // namespace granary
