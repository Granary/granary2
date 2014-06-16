/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"

#include "granary/cache.h"  // For `CacheMetaData`.
#include "granary/module.h"  // For `ModuleMetaData`.
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (BasicBlock, 2),
      (NativeBasicBlock, 2 * 3),
      (InstrumentedBasicBlock, 2 * 5),
        (CachedBasicBlock, 2 * 5 * 7),
        (DecodedBasicBlock, 2 * 5 * 11),
        (DirectBasicBlock, 2 * 5 * 13),
        (IndirectBasicBlock, 2 * 5 * 17),
        (ReturnBasicBlock, 2 * 5 * 19))

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

}  // namespace detail

BasicBlock::BasicBlock(void)
    : UnownedCountedObject(),
      list(),
      id(-1) {}

detail::SuccessorBlockIterator BasicBlock::Successors(void) const {
  return detail::SuccessorBlockIterator();
}

// Returns the number of predecessors of this basic block within the LCFG.
int BasicBlock::NumLocalPredecessors(void) const {
  return this->NumReferences();
}

// Retunrs a unique ID for this basic block within the LCFG. This can be
// useful for client tools to implement data flow passes.
int BasicBlock::Id(void) const {
  return id;
}

// Get this basic block's meta-data.
BlockMetaData *InstrumentedBasicBlock::MetaData(void) {
  return meta;
}

// Get this basic block's meta-data.
BlockMetaData *InstrumentedBasicBlock::UnsafeMetaData(void) {
  return meta;
}

// Initialize an instrumented basic block.
InstrumentedBasicBlock::InstrumentedBasicBlock(BlockMetaData *meta_)
    : cfg(nullptr),
      meta(meta_),
      cached_meta_hash(0),
      native_pc(meta ? MetaDataCast<ModuleMetaData *>(meta)->start_pc
                     : nullptr) {}

InstrumentedBasicBlock::InstrumentedBasicBlock(LocalControlFlowGraph *cfg_,
                                               BlockMetaData *meta_)
    : cfg(cfg_),
      meta(meta_),
      cached_meta_hash(0),
      native_pc(meta ? MetaDataCast<ModuleMetaData *>(meta)->start_pc
                     : nullptr) {}

// Returns the starting PC of this basic block.
AppPC InstrumentedBasicBlock::StartAppPC(void) const {
  return native_pc;
}

// Returns the starting PC of this basic block in the code cache.
CachePC InstrumentedBasicBlock::StartCachePC(void) const {
  return MetaDataCast<CacheMetaData *>(meta)->cache_pc;
}


// Initialize a decoded basic block.
DecodedBasicBlock::DecodedBasicBlock(LocalControlFlowGraph *cfg_,
                                     BlockMetaData *meta_)
    : InstrumentedBasicBlock(cfg_, meta_),
      first(new AnnotationInstruction(IA_BEGIN_BASIC_BLOCK,
                                      reinterpret_cast<void *>(&first))),
      last(new AnnotationInstruction(IA_END_BASIC_BLOCK,
                                     reinterpret_cast<void *>(&last))) {
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

// Return an iterator of the successor blocks of this basic block.
detail::SuccessorBlockIterator DecodedBasicBlock::Successors(void) const {
  return detail::SuccessorBlockIterator(
      internal::FindNextSuccessorInstruction(first));
}

// Allocates a new temporary virtual register for use by instructions within
// this basic block.
VirtualRegister DecodedBasicBlock::AllocateVirtualRegister(int num_bytes) {
  return cfg->AllocateVirtualRegister(num_bytes);
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
ForwardInstructionIterator DecodedBasicBlock::Instructions(void) const {
  return ForwardInstructionIterator(first);
}

// Return a reverse iterator for the instructions of the block.
BackwardInstructionIterator
DecodedBasicBlock::ReversedInstructions(void) const {
  return BackwardInstructionIterator(last);
}

// Return an iterator for the application instructions of a basic block.
AppInstructionIterator DecodedBasicBlock::AppInstructions(void) const {
  return AppInstructionIterator(first);
}

// Return a reverse iterator for the application instructions of the block.
BackwardAppInstructionIterator
DecodedBasicBlock::ReversedAppInstructions(void) const {
  return BackwardAppInstructionIterator(last);
}


// Add a new instruction to the beginning of the instruction list.
void DecodedBasicBlock::PrependInstruction(std::unique_ptr<Instruction> instr) {
  FirstInstruction()->InsertAfter(std::move(instr));
}

// Add a new instruction to the end of the instruction list.
void DecodedBasicBlock::AppendInstruction(std::unique_ptr<Instruction> instr) {
  LastInstruction()->InsertBefore(std::move(instr));
}

// Add a new instruction to the beginning of the instruction list.
void DecodedBasicBlock::UnsafePrependInstruction(Instruction *instr) {
  PrependInstruction(std::move(std::unique_ptr<Instruction>(instr)));
}

// Add a new instruction to the end of the instruction list.
void DecodedBasicBlock::UnsafeAppendInstruction(Instruction *instr) {
  AppendInstruction(std::move(std::unique_ptr<Instruction>(instr)));
}

// Free all of the instructions in the basic block. This is invoked by
// `LocalControlFlowGraph::~LocalControlFlowGraph`, as the freeing of
// instructions interacts with the ownership model of basic blocks inside
// of basic block lists.
void DecodedBasicBlock::FreeInstructionList(void) {
  for (Instruction *instr(first), *next_instr(nullptr); instr;) {
    next_instr = instr->Next();
    delete instr;
    instr = next_instr;
  }
}

// Initialize a future basic block.
DirectBasicBlock::DirectBasicBlock(LocalControlFlowGraph *cfg_,
                                   BlockMetaData *meta_)
    : InstrumentedBasicBlock(cfg_, meta_),
      materialized_block(nullptr),
      materialize_strategy(REQUEST_LATER) {}

// Returns the starting PC of this basic block.
AppPC IndirectBasicBlock::StartAppPC(void) const {
  granary_break_on_fault();
  return nullptr;
}

// Returns the starting PC of this basic block in the code cache.
CachePC IndirectBasicBlock::StartCachePC(void) const {
  granary_break_on_fault();
  return nullptr;
}

// Initialize a return basic block.
ReturnBasicBlock::ReturnBasicBlock(LocalControlFlowGraph *cfg_,
                                   BlockMetaData *meta_)
    : InstrumentedBasicBlock(cfg_, nullptr),
      lazy_meta(meta_) {}

ReturnBasicBlock::~ReturnBasicBlock(void) {
  if (!meta && lazy_meta) {
    delete lazy_meta;
    lazy_meta = nullptr;
  }
}

// Return this basic block's meta-data. Accessing a return basic block's meta-
// data will "create" it for the block.
BlockMetaData *ReturnBasicBlock::MetaData(void) {
  if (GRANARY_UNLIKELY(!meta)) {
    std::swap(lazy_meta, meta);
  }
  return meta;
}

// Returns the starting PC of this basic block.
AppPC ReturnBasicBlock::StartAppPC(void) const {
  granary_break_on_fault();
  return nullptr;
}

// Returns the starting PC of this basic block in the code cache.
CachePC ReturnBasicBlock::StartCachePC(void) const {
  granary_break_on_fault();
  return nullptr;
}

// Returns the starting PC of this basic block.
AppPC NativeBasicBlock::StartAppPC(void) const {
  return native_pc;
}

// Returns the starting PC of this basic block in the code cache.
CachePC NativeBasicBlock::StartCachePC(void) const {
  granary_break_on_fault();
  return nullptr;
}

}  // namespace granary
