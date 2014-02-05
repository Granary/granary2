/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/factory.h"
#include "granary/util.h"

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (BasicBlock, 2),
    (NativeBasicBlock, 2 * 3),
    (InstrumentedBasicBlock, 2 * 5),
    (CachedBasicBlock, 2 * 5 * 7),
    (DecodedBasicBlock, 2 * 5 * 11),
    (DirectBasicBlock, 2 * 5 * 13),
    (IndirectBasicBlock, 2 * 5 * 17),
    (ReturnBasicBlock, 2 * 19))

GRANARY_DEFINE_BASE_CLASS(BasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, NativeBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, InstrumentedBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, CachedBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, DecodedBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, DirectBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, IndirectBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, ReturnBasicBlock)

namespace internal {

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
}  // namespace internal
namespace detail {

BasicBlockSuccessor SuccessorBlockIterator::operator*(void) const {
  auto cti(DynamicCast<ControlFlowInstruction *>(cursor));
  return BasicBlockSuccessor(cti, cti->TargetBlock());
}

void SuccessorBlockIterator::operator++(void) {
  cursor = internal::FindNextSuccessorInstruction(cursor);
}

void ForwardInstructionIterator::operator++(void) {
  instr = instr->Next();
}

void BackwardInstructionIterator::operator++(void) {
  instr = instr->Previous();
}

}  // namespace detail

BasicBlock::BasicBlock(void)
    : UnownedCountedObject(),
      list() {}

detail::SuccessorBlockIterator BasicBlock::Successors(void) const {
  return detail::SuccessorBlockIterator();
}

// Returns the number of predecessors of this basic block within the LCFG.
int BasicBlock::NumLocalPredecessors(void) const {
  return this->NumReferences();
}

// Get this basic block's meta-data.
GenericMetaData *InstrumentedBasicBlock::MetaData(void) {
  return meta;
}

// Initialize an instrumented basic block.
InstrumentedBasicBlock::InstrumentedBasicBlock(GenericMetaData *meta_)
    : meta(meta_),
      cached_meta_hash(0),
      native_pc(MetaDataCast<TranslationMetaData *>(meta)->native_pc) {}

// Returns the starting PC of this basic block.
AppProgramCounter InstrumentedBasicBlock::StartPC(void) const {
  return native_pc;
}

// Initialize a decoded basic block.
DecodedBasicBlock::DecodedBasicBlock(GenericMetaData *meta_)
    : InstrumentedBasicBlock(meta_),
      first(new AnnotationInstruction(BEGIN_BASIC_BLOCK)),
      last(new AnnotationInstruction(END_BASIC_BLOCK)) {
  first->InsertAfter(std::unique_ptr<Instruction>(last));
}

// Clean up an instrumented basic block. If the meta-data hasn't already been
// unlinked by now then we assume this basic block is unreachable, as is its
// meta-data.
InstrumentedBasicBlock::~InstrumentedBasicBlock(void) {
  if (meta) {
    delete meta;
    meta = nullptr;
  }
}

// Return an iterator of the successors of a basic block.
detail::SuccessorBlockIterator DecodedBasicBlock::Successors(void) const {
  return detail::SuccessorBlockIterator(
      internal::FindNextSuccessorInstruction(first));
}

// Return the first instruction in the basic block.
Instruction *DecodedBasicBlock::FirstInstruction(void) const {
  return first;
}

// Return the last instruction in the basic block.
Instruction *DecodedBasicBlock::LastInstruction(void) const {
  return last;
}

// Return an iterator for the instructions of the block.
detail::ForwardInstructionIterator
DecodedBasicBlock::Instructions(void) const {
  return detail::ForwardInstructionIterator(first);
}

// Return a reverse iterator for the instructions of the block.
detail::BackwardInstructionIterator
DecodedBasicBlock::ReversedInstructions(void) const {
  return detail::BackwardInstructionIterator(last);
}

// Free all of the instructions in the basic block. This is invoked by
// LocalControlFlowGraph::~LocalControlFlowGraph, as the freeing of instructions
// interacts with the ownership model of basic blocks inside of basic block
// lists.
void DecodedBasicBlock::FreeInstructionList(void) {
  for (Instruction *instr(first), *next(nullptr); instr; instr = next) {
    next = instr->Next();
    delete instr;
  }
}

// Initialize a future basic block.
DirectBasicBlock::DirectBasicBlock(GenericMetaData *meta_)
    : InstrumentedBasicBlock(meta_),
      materialized_block(nullptr),
      materialize_strategy(REQUEST_LATER) {}

// Returns the starting PC of this basic block.
AppProgramCounter IndirectBasicBlock::StartPC(void) const {
  granary_break_on_fault();
  return nullptr;
}

// Initialize a return basic block.
ReturnBasicBlock::ReturnBasicBlock(void)
    : BasicBlock() {}

// Returns the starting PC of this basic block.
AppProgramCounter ReturnBasicBlock::StartPC(void) const {
  granary_break_on_fault();
  return nullptr;
}

// Returns the starting PC of this basic block.
AppProgramCounter NativeBasicBlock::StartPC(void) const {
  return native_pc;
}

}  // namespace granary
