/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

#include "granary/cfg/block.h"
#include "granary/cfg/trace.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"

#include "granary/app.h"  // For `AppMetaData`.
#include "granary/cache.h"  // For `CacheMetaData`.
#include "granary/util.h"  // For `GetMetaData`.

GRANARY_DECLARE_bool(transparent_returns);

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Block, 2),
      (NativeBlock, 2 * 3),
      (InstrumentedBlock, 2 * 5),
        (CachedBlock, 2 * 5 * 7),
        (DecodedBlock, 2 * 5 * 11),
          (CompensationBlock, 2 * 5 * 11 * 13),
        (DirectBlock, 2 * 5 * 17),
        (IndirectBlock, 2 * 5 * 19),
        (ReturnBlock, 2 * 5 * 23))

GRANARY_DEFINE_BASE_CLASS(Block)
GRANARY_DEFINE_DERIVED_CLASS_OF(Block, NativeBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(Block, InstrumentedBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(Block, CachedBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(Block, DecodedBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(Block, CompensationBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(Block, DirectBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(Block, IndirectBlock)
GRANARY_DEFINE_DERIVED_CLASS_OF(Block, ReturnBlock)

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

SuccessorBlockIterator::SuccessorBlockIterator(Instruction *instr_)
    : cursor(instr_),
      next_cursor(nullptr) {
  if (cursor) {
    next_cursor = cursor->Next();
  }
}

BlockSuccessor SuccessorBlockIterator::operator*(void) const {
  auto cti(DynamicCast<ControlFlowInstruction *>(cursor));
  return BlockSuccessor(cti, cti->TargetBlock());
}

void SuccessorBlockIterator::operator++(void) {
  if (next_cursor && next_cursor->Previous() != cursor) {
    cursor = next_cursor;
  }
  next_cursor = nullptr;
  if ((cursor = internal::FindNextSuccessorInstruction(cursor))) {
    next_cursor = cursor->Next();
  }
}

}  // namespace detail

Block::Block(void)
    : list(),
      id(-1),
      generation(0),
      is_reachable(false),
      fragment(nullptr) {}

detail::SuccessorBlockIterator Block::Successors(void) const {
  return detail::SuccessorBlockIterator();
}

// Returns a unique ID for this basic block within the trace. This can be
// useful for client tools to implement data flow passes.
int Block::Id(void) const {
  return id;
}

// Get this basic block's meta-data.
BlockMetaData *InstrumentedBlock::MetaData(void) {
  return meta;
}

// Get this basic block's meta-data.
BlockMetaData *InstrumentedBlock::UnsafeMetaData(void) {
  return meta;
}

InstrumentedBlock::InstrumentedBlock(Trace *cfg_,
                                               BlockMetaData *meta_)
    : Block(),
      cfg(cfg_),
      meta(meta_),
      native_pc(meta ? MetaDataCast<AppMetaData *>(meta)->start_pc
                     : nullptr) {}

// Returns the starting PC of this basic block.
AppPC InstrumentedBlock::StartAppPC(void) const {
  return native_pc;
}

// Returns the starting PC of this basic block in the code cache.
CachePC InstrumentedBlock::StartCachePC(void) const {
  const auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  return cache_meta->start_pc;
}

DecodedBlock::~DecodedBlock(void) {
  for (Instruction *instr(first), *next_instr(nullptr); instr;) {
    next_instr = instr->Next();
    delete instr;
    instr = next_instr;
  }
  first = nullptr;
  last = nullptr;
}

// Initialize a decoded basic block.
DecodedBlock::DecodedBlock(Trace *cfg_, BlockMetaData *meta_)
    : InstrumentedBlock(cfg_, meta_),
      first(new AnnotationInstruction(kAnnotBeginBlock,
                                      reinterpret_cast<void *>(&first))),
      last(new AnnotationInstruction(kAnnotEndBlock,
                                     reinterpret_cast<void *>(&last))),
      is_cold_code(false) {
  first->InsertAfter(last);
  for (auto &reg : arg_regs) {
    reg = cfg->AllocateVirtualRegister();
  }
}

InstrumentedBlock::~InstrumentedBlock(void) {
  meta = nullptr;
}

// Return an iterator of the successor blocks of this basic block.
detail::SuccessorBlockIterator DecodedBlock::Successors(void) const {
  return detail::SuccessorBlockIterator(
      internal::FindNextSuccessorInstruction(first));
}

// Allocates a new temporary virtual register for use by instructions within
// this basic block.
VirtualRegister DecodedBlock::AllocateVirtualRegister(size_t num_bytes) {
  return cfg->AllocateVirtualRegister(num_bytes);
}

// Return the first instruction in the basic block.
Instruction *DecodedBlock::FirstInstruction(void) const {
  return first;
}

// Return the last instruction in the basic block.
Instruction *DecodedBlock::LastInstruction(void) const {
  return last;
}

// Return an iterator for the instructions of the block.
InstructionIterator DecodedBlock::Instructions(void) const {
  return InstructionIterator(first);
}

// Return a reverse iterator for the instructions of the block.
ReverseInstructionIterator
DecodedBlock::ReversedInstructions(void) const {
  return ReverseInstructionIterator(last);
}

// Return an iterator for the application instructions of a basic block.
AppInstructionIterator DecodedBlock::AppInstructions(void) const {
  return AppInstructionIterator(first);
}

// Return a reverse iterator for the application instructions of the block.
ReverseAppInstructionIterator
DecodedBlock::ReversedAppInstructions(void) const {
  return ReverseAppInstructionIterator(last);
}


// Add a new instruction to the beginning of the instruction list.
void DecodedBlock::PrependInstruction(std::unique_ptr<Instruction> instr) {
  FirstInstruction()->InsertAfter(instr.release());
}

// Add a new instruction to the end of the instruction list.
void DecodedBlock::AppendInstruction(std::unique_ptr<Instruction> instr) {
  LastInstruction()->InsertBefore(instr.release());
}

// Add a new instruction to the beginning of the instruction list.
void DecodedBlock::PrependInstruction(Instruction *instr) {
  FirstInstruction()->InsertAfter(instr);
}

// Add a new instruction to the end of the instruction list.
void DecodedBlock::AppendInstruction(Instruction *instr) {
  LastInstruction()->InsertBefore(instr);
}

// Mark the code of this block as being cold.
void DecodedBlock::MarkAsColdCode(void) {
  is_cold_code = true;
}

// Is this cold code?
bool DecodedBlock::IsColdCode(void) const {
  return is_cold_code;
}

// Remove and return single instruction. Some special kinds of instructions
// can't be removed.
std::unique_ptr<Instruction> DecodedBlock::Unlink(Instruction *instr) {
  if (auto annot_instr = DynamicCast<AnnotationInstruction *>(instr)) {

    // Don't allow removal of these instructions.
    switch (annot_instr->annotation) {
      case kAnnotBeginBlock:
      case kAnnotEndBlock:
      case kAnnotationLabel:
      case kAnnotInvalidStack:
        return std::unique_ptr<Instruction>(nullptr);
      default:
        break;
    }

  // If we're unlinking a branch then make sure that the target itself does
  // not continue to reference the branch.
  } else if (auto branch = DynamicCast<BranchInstruction *>(instr)) {
    GRANARY_ASSERT(1 <= branch->TargetLabel()->Data<uintptr_t>());
    branch->TargetLabel()->DataRef<uintptr_t>() -= 1;
  }
  return Instruction::Unlink(instr);
}

// Truncate a decoded basic block. This removes `instr` up until the end of
// the instruction list. In some cases, certain special instructions are not
// allowed to be truncated. This will not remove such special cases.
void DecodedBlock::Truncate(Instruction *instr) {
  for (Instruction *next_instr(nullptr); instr; instr = next_instr) {
    next_instr = instr->Next();
    Unlink(instr);
  }
}

// Returns the Nth argument register for use by a lir function call.
VirtualRegister DecodedBlock::NthArgumentRegister(size_t arg_num) const {
  return arg_regs[arg_num];
}

CompensationBlock::CompensationBlock(Trace *cfg_,
                                               BlockMetaData *meta_)
    : DecodedBlock(cfg_, meta_),
      is_comparable(true) {}

DirectBlock::~DirectBlock(void) {
  materialized_block = nullptr;
}

// Initialize a future basic block.
DirectBlock::DirectBlock(Trace *cfg_,
                                   BlockMetaData *meta_)
    : InstrumentedBlock(cfg_, meta_),
      materialized_block(nullptr),
      materialize_strategy(kRequestBlockLater) {}

// Returns the starting PC of this basic block.
AppPC IndirectBlock::StartAppPC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

// Returns the starting PC of this basic block in the code cache.
CachePC IndirectBlock::StartCachePC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

// Initialize a return basic block.
ReturnBlock::ReturnBlock(Trace *cfg_,
                                   BlockMetaData *meta_)
    : InstrumentedBlock(cfg_, FLAG_transparent_returns ? meta_ : nullptr),
      lazy_meta(FLAG_transparent_returns ? nullptr : meta_) {}

ReturnBlock::~ReturnBlock(void) {
  if (!meta && lazy_meta) {
    delete lazy_meta;
  }
  lazy_meta = nullptr;
}

// Returns true if this return basic block has meta-data. If it has meta-data
// then the way that the branch is resolved is slightly more complicated.
bool ReturnBlock::UsesMetaData(void) const {
  return nullptr != meta;
}

// Return this basic block's meta-data. Accessing a return basic block's meta-
// data will "create" it for the block.
BlockMetaData *ReturnBlock::MetaData(void) {
  if (GRANARY_UNLIKELY(!UsesMetaData())) {
    std::swap(lazy_meta, meta);
  }
  return meta;
}

// Returns the starting PC of this basic block.
AppPC ReturnBlock::StartAppPC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

// Returns the starting PC of this basic block in the code cache.
CachePC ReturnBlock::StartCachePC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

NativeBlock::NativeBlock(AppPC native_pc_)
    : Block(),
      native_pc(native_pc_) {}

// Returns the starting PC of this basic block.
AppPC NativeBlock::StartAppPC(void) const {
  return native_pc;
}

// Returns the starting PC of this basic block in the code cache.
CachePC NativeBlock::StartCachePC(void) const {
  GRANARY_ASSERT(false);
  return nullptr;
}

}  // namespace granary
