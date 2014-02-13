/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/list.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/allocate.h"
#include "granary/code/assemble.h"
#include "granary/code/edge.h"

#include "granary/util.h"

namespace granary {

typedef LinkedListIterator<DecodedBasicBlock> DecodedBlockIterator;

namespace {

// Test whether or not we should treat the current instruction as reachable.
// All annotation instructions are treated as reachable. If `is_unreachable`
// tells us that the current instruction might not be reachable (independent
// of if it's an annotation), then we can only switch back to seeing non-
// annotations as reachable if we see a label instruction that is the target
// of some branch.
//
// Note: We don't do more aggressive checking on the reachability of the
//       jump to the label itself.
static bool InstructionIsReachable(Instruction *instr, bool *is_unreachable) {
  if (*is_unreachable) {
    auto annotation = DynamicCast<const AnnotationInstruction *>(instr);
    if (!annotation) {
      return false;
    } else if (!annotation->IsBranchTarget()) {
      *is_unreachable = false;
    }
  }
  return true;
}

// Update the unreachable status when control passes over specific types of
// control-flow instructions.
void CheckNextInstructionReachable(Instruction *instr, bool *is_unreachable) {
  auto cfi = DynamicCast<ControlFlowInstruction *>(instr);
  if (cfi) {
    *is_unreachable = *is_unreachable || cfi->IsUnconditionalJump() ||
                      cfi->IsFunctionReturn() || cfi->IsSystemReturn() ||
                      cfi->IsInterruptReturn();
  }
}

// Removes unreachable instructions.
static void RemoveUnreachableInstructions(DecodedBasicBlock *block) {
  auto curr = block->FirstInstruction();
  auto next = curr;
  bool is_unreachable(false);
  for (; curr; curr = next) {
    next = curr->Next();
    if (InstructionIsReachable(curr, &is_unreachable)) {
      CheckNextInstructionReachable(curr, &is_unreachable);
    } else {
      Instruction::Unlink(curr);
    }
  }
}

// Preprocess the blocks.
static void PreprocessBlocks(LocalControlFlowGraph *cfg) {
  for (auto block : cfg->Blocks()) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    if (decoded_block) {
      RemoveUnreachableInstructions(decoded_block);
    }
  }
}

// Schedule a decoded basic block into the straight-line list of blocks to
// encode. Scheduling follows a depth-first ordering, where we follow direct
// jumps.
static DecodedBasicBlock **Schedule(DecodedBasicBlock *block,
                                    DecodedBasicBlock **next_ptr) {
  if (block->next) {
    return next_ptr;
  }
  *next_ptr = block; // Chain this block into the schedule.
  next_ptr = &(block->next);

  for (auto instr : block->Instructions()) {
    auto cfi = DynamicCast<ControlFlowInstruction *>(instr);
    if (cfi && cfi->IsUnconditionalJump()) {
      auto target = DynamicCast<DecodedBasicBlock *>(cfi->TargetBlock());
      if (target) {
        next_ptr = Schedule(target, next_ptr);
      }
    }
  }
  return next_ptr;
}

// Schedule the decoded basic blocks into a list of blocks for encoding.
static DecodedBasicBlock *Schedule(LocalControlFlowGraph *cfg) {
  DecodedBasicBlock *first(nullptr);
  DecodedBasicBlock **next(&first);
  for (auto block : cfg->Blocks()) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    if (!decoded_block || decoded_block->next) {
      continue;  // Wrong kind of block, or already scheduled.
    }
    next = Schedule(decoded_block, next);
  }
  return first;
}

// Pretend to encode a basic block at `cache_pc`.
static CacheProgramCounter StageEncode(DecodedBasicBlock *block,
                                       CacheProgramCounter cache_pc) {
  auto meta = GetMetaData<CacheMetaData>(block);
  meta->cache_pc = cache_pc;
  for (auto instr : block->Instructions()) {
    cache_pc = instr->StageEncode(cache_pc);
  }
  return cache_pc;
}

// Returns the target of a control-flow or branch instruction.
static ProgramCounter TargetPC(const Instruction *instr) {
  if (IsA<const ControlFlowInstruction *>(instr)) {
    auto cfi = DynamicCast<const ControlFlowInstruction *>(instr);
    auto target = cfi->TargetBlock();
    if (IsA<InstrumentedBasicBlock *>(target)) {
      return target->CacheStartPC();
    } else if (IsA<NativeBasicBlock *>(target)) {
      return target->AppStartPC();
    }
  } else if (IsA<const BranchInstruction *>(instr)) {
    auto branch = DynamicCast<const BranchInstruction *>(instr);
    auto target = branch->TargetInstruction();
    return target->CacheStartPC();
  }
  return nullptr;
}

// Returns true if an instruction can be removed.
static bool CanRemoveInstruction(const Instruction *instr) {
  auto instr_pc = instr->CacheStartPC();
  auto target_pc = TargetPC(instr);

  if (target_pc) {
    return (instr_pc + instr->Length()) == target_pc;
  }

  auto native_instr = DynamicCast<const NativeInstruction *>(instr);
  if (native_instr) {
    return native_instr->IsNoOp();
  }

  return false;
}

// Returns true if the encoding of a particular instruction can be shrunk.
static bool CanShrinkInstruction(const Instruction *) {
  // TODO(pag): Implement this. See notes in `arch/x86-64/base.h`.
  return false;
}

// Optimize the encoding of the instructions within the basic blocks.
static bool OptimizeEncoding(DecodedBasicBlock *blocks) {
  bool optimized(false);
  for (auto block : DecodedBlockIterator(blocks)) {
    auto instr = block->FirstInstruction();
    auto next = instr;
    for (; instr; instr = next) {
      next = instr->Next();
      if (CanRemoveInstruction(instr)) {
        Instruction::Unlink(instr);
        optimized = true;
      } else if (CanShrinkInstruction(instr)) {
        // TODO(pag): Implement this. See notes in `arch/x86-64/base.h`.
      }
    }
  }
  return optimized;
}

// Stage encode all blocks and return the encoded size of the blocks.
static int StageEncode(DecodedBasicBlock *blocks) {
  CacheProgramCounter cache_pc(nullptr);
  auto start_pc = cache_pc;
  for (auto block : DecodedBlockIterator(blocks)) {
    cache_pc = StageEncode(block, cache_pc);
  }
  return static_cast<int>(cache_pc - start_pc);
}

// Try to find the "relaxed" size of the scheduled blocks by repeatedly
// optimizing the blocks until no further size reduction can be made.
static int Resize(DecodedBasicBlock *blocks) {
  int len(INT_MAX);
  int old_len(0);
  do {
    old_len = len;
    len = StageEncode(blocks);
  } while (len < old_len && OptimizeEncoding(blocks));
  return static_cast<int>(len);
}

}  // namespace

// Assemble the local control-flow graph into
void Assemble(LocalControlFlowGraph *cfg, CodeAllocator *cache,
              CodeAllocator *edge) {
  PreprocessBlocks(cfg);
  auto blocks = Schedule(cfg);
  auto relaxed_size = Resize(blocks);
  // TODO(pag): stubs.
  // TODO(pag): Final allocation size check, should match `relaxed_size`.
  GRANARY_UNUSED(relaxed_size);
  GRANARY_UNUSED(cache);
  GRANARY_UNUSED(edge);
}

}  // namespace granary
