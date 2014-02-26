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

#include "granary/driver.h"
#include "granary/util.h"

namespace granary {

// Iterator over decoded basic blocks.
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
  auto instr = block->FirstInstruction();
  bool is_unreachable(false);
  for (auto next = instr; instr; instr = next) {
    next = instr->Next();
    if (InstructionIsReachable(instr, &is_unreachable)) {
      CheckNextInstructionReachable(instr, &is_unreachable);
    } else {
      Instruction::Unlink(instr);
    }
  }
}

// Relativizie instructions within a basic block. Instructions with relative
// components might not be safe because the code cache's placement might place
// them too far away. This is typical in user space on x86 with 32-bit relative
// addressing, where native code is placed at a low address, and `mmap`d cache
// code at a high address (>4GB away).
static void RelativizeInstructions(DecodedBasicBlock *block,
                                   CodeAllocator *cache) {
  auto instr = block->FirstInstruction();
  driver::InstructionRelativizer relativizer(cache->Allocate(1, 0));
  for (auto next = instr; instr; instr = next) {
    next = instr->Next();
    auto native_instr = DynamicCast<NativeInstruction *>(instr);
    if (native_instr) {
      relativizer.Relativize(native_instr);
    }
  }
}

// Preprocess the blocks. This involves removing unreachable instructions and
// making native instructions with relative components (e.g. RIP-relative
// addressing on x86-64) safe to execute in the code cache.
static void PreprocessBlocks(LocalControlFlowGraph *cfg,
                             CodeAllocator *cache) {
  for (auto block : cfg->Blocks()) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    if (decoded_block) {
      RemoveUnreachableInstructions(decoded_block);
      RelativizeInstructions(decoded_block, cache);
    }
  }
}

// Schedule a decoded basic block into the straight-line list of blocks to
// encode. Scheduling follows a depth-first ordering, where we follow direct
// jumps.
static DecodedBasicBlock **Schedule(DecodedBasicBlock *block,
                                    DecodedBasicBlock **next_ptr) {
  if (!block->next) {
    *next_ptr = block;  // Chain this block into the schedule.
    next_ptr = &(block->next);
    block->next = block;  // Sentinel to prevent loops in the list.
    for (auto instr : block->Instructions()) {
      auto cfi = DynamicCast<ControlFlowInstruction *>(instr);
      if (cfi && cfi->IsUnconditionalJump()) {
        auto target = DynamicCast<DecodedBasicBlock *>(cfi->TargetBlock());
        if (target) {
          next_ptr = Schedule(target, next_ptr);
        }
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
    if (decoded_block && !decoded_block->next) {
      next = Schedule(decoded_block, next);
    }
  }
  *next = nullptr;  // Clobber the remaining sentinel with a `nullptr`.
  return first;
}

// Pretend to encode a basic block at `cache_pc`.
static CachePC StageEncode(DecodedBasicBlock *block, CachePC cache_pc) {
  auto meta = GetMetaData<CacheMetaData>(block);
  meta->cache_pc = cache_pc;
  for (auto instr : block->Instructions()) {
    cache_pc = instr->StageEncode(cache_pc);
  }
  return cache_pc;
}

// Returns the target of a control-flow or branch instruction.
static PC TargetPC(const Instruction *instr) {
  if (IsA<const ControlFlowInstruction *>(instr)) {
    auto cfi = DynamicCast<const ControlFlowInstruction *>(instr);
    auto target = cfi->TargetBlock();
    if (IsA<InstrumentedBasicBlock *>(target) ||
        IsA<DirectBasicBlock *>(target)) {
      return target->StartCachePC();
    } else if (IsA<NativeBasicBlock *>(target)) {
      return target->StartAppPC();
    }
  } else if (IsA<const BranchInstruction *>(instr)) {
    auto branch = DynamicCast<const BranchInstruction *>(instr);
    auto target = branch->TargetInstruction();
    return target->StartCachePC();
  }
  return nullptr;
}

// Returns true if an instruction can be removed. The first test is jumping to
// the next instruction.
static bool CanRemoveInstruction(const Instruction *instr) {
  auto instr_pc = instr->StartCachePC();
  auto target_pc = TargetPC(instr);
  if (target_pc) {
    return (instr_pc + instr->Length()) == target_pc;
  }
  auto native_instr = DynamicCast<const NativeInstruction *>(instr);
  return native_instr && native_instr->IsNoOp();
}

// Returns true if the encoding of a particular instruction can be shrunk.
static bool CanShrinkInstruction(const Instruction *) {
  // TODO(pag): Implement this. See notes in `arch/x86-64/base.h`.
  // Note: Need to watch out that a stub that has a shorter relative
  //       displacement does not imply that the eventual resolved target will
  //       have the same short relative displacement.
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
  auto cache_pc = CachePC(nullptr);
  for (auto block : DecodedBlockIterator(blocks)) {
    cache_pc = StageEncode(block, cache_pc);
  }
  return static_cast<int>(cache_pc - CachePC(nullptr));
}

// Adjust the "stage" encoding of the blocks to pointer to their proper targets.
void AdjustEncoding(DecodedBasicBlock *block, uintptr_t adjust) {
  auto meta = GetMetaData<CacheMetaData>(block);
  meta->cache_pc += adjust;
  for (auto instr : block->Instructions()) {
    instr->SetStartCachePC(instr->StartCachePC() + adjust);
  }
}

// Encode all blocks.
static void Encode(DecodedBasicBlock *blocks, CachePC cache_pc) {
  for (auto block : DecodedBlockIterator(blocks)) {
    AdjustEncoding(block, reinterpret_cast<uintptr_t>(cache_pc));
  }
  driver::InstructionDecoder decoder;
  for (auto block : DecodedBlockIterator(blocks)) {
    for (auto instr : block->Instructions()) {
      instr->Encode(&decoder);
    }
  }
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

// Assemble the edges of a local CFG.
static void AssembleEdges(ContextInterface *env,
                          LocalControlFlowGraph *cfg) {
  for (auto block : cfg->Blocks()) {
    auto direct_block = DynamicCast<DirectBasicBlock *>(block);
    if (direct_block) {
      auto meta = direct_block->MetaData();
      auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
      cache_meta->cache_pc = AssembleEdge(env, meta);
    }
  }
}

}  // namespace

// Assemble the local control-flow graph into
void Assemble(ContextInterface *env, LocalControlFlowGraph *cfg,
              CodeAllocator *cache_code_allocator) {
  PreprocessBlocks(cfg, cache_code_allocator);
  AssembleEdges(env, cfg);
  auto blocks = Schedule(cfg);
  auto relaxed_size = Resize(blocks);
  auto code = cache_code_allocator->Allocate(
      GRANARY_ARCH_CACHE_LINE_SIZE, relaxed_size);
  Encode(blocks, code);
}

}  // namespace granary
