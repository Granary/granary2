/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"

#include "granary/app.h"  // For `AppMetaData`.
#include "granary/cache.h"  // For `CacheMetaData`.
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (BasicBlock, 2),
      (NativeBasicBlock, 2 * 3),
      (InstrumentedBasicBlock, 2 * 5),
        (CachedBasicBlock, 2 * 5 * 7),
        (DecodedBasicBlock, 2 * 5 * 11),
          (CompensationBasicBlock, 2 * 5 * 11 * 13),
        (DirectBasicBlock, 2 * 5 * 17),
        (IndirectBasicBlock, 2 * 5 * 19),
        (ReturnBasicBlock, 2 * 5 * 23))

GRANARY_DEFINE_BASE_CLASS(BasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, NativeBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, InstrumentedBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, CachedBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, DecodedBasicBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(BasicBlock, CompensationBasicBlock)
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
    : list(),
      id(-1),
      generation(-1),
      is_reachable(false),
      fragment(nullptr) {}

detail::SuccessorBlockIterator BasicBlock::Successors(void) const {
  return detail::SuccessorBlockIterator();
}

// Returns a unique ID for this basic block within the LCFG. This can be
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

InstrumentedBasicBlock::InstrumentedBasicBlock(LocalControlFlowGraph *cfg_,
                                               BlockMetaData *meta_)
    : BasicBlock(),
      cfg(cfg_),
      meta(meta_),
      native_pc(meta ? MetaDataCast<AppMetaData *>(meta)->start_pc
                     : nullptr) {}

// Returns the starting PC of this basic block.
AppPC InstrumentedBasicBlock::StartAppPC(void) const {
  return native_pc;
}

// Returns the starting PC of this basic block in the code cache.
CachePC InstrumentedBasicBlock::StartCachePC(void) const {
  const auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  return cache_meta->start_pc;
}

DecodedBasicBlock::~DecodedBasicBlock(void) {
  for (Instruction *instr(first), *next_instr(nullptr); instr;) {
    next_instr = instr->Next();
    delete instr;
    instr = next_instr;
  }
  first = nullptr;
  last = nullptr;
}

// Initialize a decoded basic block.
DecodedBasicBlock::DecodedBasicBlock(LocalControlFlowGraph *cfg_,
                                     BlockMetaData *meta_)
    : InstrumentedBasicBlock(cfg_, meta_),
      first(new AnnotationInstruction(IA_BEGIN_BASIC_BLOCK,
                                      reinterpret_cast<void *>(&first))),
      last(new AnnotationInstruction(IA_END_BASIC_BLOCK,
                                     reinterpret_cast<void *>(&last))) {
  first->InsertAfter(last);
}

InstrumentedBasicBlock::~InstrumentedBasicBlock(void) {
  meta = nullptr;
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
InstructionIterator DecodedBasicBlock::Instructions(void) const {
  return InstructionIterator(first);
}

// Return a reverse iterator for the instructions of the block.
ReverseInstructionIterator
DecodedBasicBlock::ReversedInstructions(void) const {
  return ReverseInstructionIterator(last);
}

// Return an iterator for the application instructions of a basic block.
AppInstructionIterator DecodedBasicBlock::AppInstructions(void) const {
  return AppInstructionIterator(first);
}

// Return a reverse iterator for the application instructions of the block.
ReverseAppInstructionIterator
DecodedBasicBlock::ReversedAppInstructions(void) const {
  return ReverseAppInstructionIterator(last);
}


// Add a new instruction to the beginning of the instruction list.
void DecodedBasicBlock::PrependInstruction(std::unique_ptr<Instruction> instr) {
  FirstInstruction()->InsertAfter(instr.release());
}

// Add a new instruction to the end of the instruction list.
void DecodedBasicBlock::AppendInstruction(std::unique_ptr<Instruction> instr) {
  LastInstruction()->InsertBefore(instr.release());
}

// Add a new instruction to the beginning of the instruction list.
void DecodedBasicBlock::PrependInstruction(Instruction *instr) {
  FirstInstruction()->InsertAfter(instr);
}

// Add a new instruction to the end of the instruction list.
void DecodedBasicBlock::AppendInstruction(Instruction *instr) {
  LastInstruction()->InsertBefore(instr);
}

CompensationBasicBlock::CompensationBasicBlock(LocalControlFlowGraph *cfg_,
                                               BlockMetaData *meta_)
    : DecodedBasicBlock(cfg_, meta_),
      is_comparable(true) {}

DirectBasicBlock::~DirectBasicBlock(void) {
  materialized_block = nullptr;
}

// Initialize a future basic block.
DirectBasicBlock::DirectBasicBlock(LocalControlFlowGraph *cfg_,
                                   BlockMetaData *meta_,
                                   AppPC non_transparent_pc_)
    : InstrumentedBasicBlock(cfg_, meta_),
      materialized_block(nullptr),
      materialize_strategy(REQUEST_LATER),
      non_transparent_pc(non_transparent_pc_) {}

// Returns the starting PC of this basic block.
AppPC IndirectBasicBlock::StartAppPC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

// Returns the starting PC of this basic block in the code cache.
CachePC IndirectBasicBlock::StartCachePC(void) const {
  GRANARY_ASSERT(false);
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
  }
  lazy_meta = nullptr;
}

// Returns true if this return basic block has meta-data. If it has meta-data
// then the way that the branch is resolved is slightly more complicated.
bool ReturnBasicBlock::UsesMetaData(void) const {
  return nullptr != meta;
}

// Return this basic block's meta-data. Accessing a return basic block's meta-
// data will "create" it for the block.
BlockMetaData *ReturnBasicBlock::MetaData(void) {
  if (GRANARY_UNLIKELY(!UsesMetaData())) {
    std::swap(lazy_meta, meta);
  }
  return meta;
}

// Returns the starting PC of this basic block.
AppPC ReturnBasicBlock::StartAppPC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

// Returns the starting PC of this basic block in the code cache.
CachePC ReturnBasicBlock::StartCachePC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

NativeBasicBlock::NativeBasicBlock(AppPC native_pc_)
    : BasicBlock(),
      native_pc(native_pc_) {}

// Returns the starting PC of this basic block.
AppPC NativeBasicBlock::StartAppPC(void) const {
  return native_pc;
}

// Returns the starting PC of this basic block in the code cache.
CachePC NativeBasicBlock::StartCachePC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

}  // namespace granary
