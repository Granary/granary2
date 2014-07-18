/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/driver.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"
#include "granary/cfg/factory.h"

#include "granary/code/metadata.h"
#include "granary/code/register.h"

#include "granary/cache.h"
#include "granary/context.h"
#include "granary/index.h"
#include "granary/module.h"
#include "granary/util.h"

namespace granary {

enum {
  MAX_NUM_MATERIALIZATION_REQUESTS = 1024
};

// Initialize the factory with an environment and a local control-flow graph.
// The environment is needed for lookups in the code cache index, and the LCFG
// is needed so that blocks can be added.
BlockFactory::BlockFactory(ContextInterface *context_,
                           LocalControlFlowGraph *cfg_)
    : context(context_),
      cfg(cfg_),
      has_pending_request(false),
      last_block(nullptr) {}

// Request that a block be materialized. This does nothing if the block is
// not a `DirectBasicBlock`.
void BlockFactory::RequestBlock(BasicBlock *block, BlockRequestKind strategy) {
  if (auto direct_block = DynamicCast<DirectBasicBlock *>(block)) {
    RequestBlock(direct_block, strategy);
  }
}

// Request that a `block` be materialized according to strategy `strategy`.
// If multiple requests are made, then the most fine-grained strategy is
// chosen.
//
// Note: We don't check that `block` is attached to the CFG's block list
//       because in the worst case, it will result in an extra instrumentation
//       loop, and it makes it easier to request blocks ahead of time.
void BlockFactory::RequestBlock(DirectBasicBlock *block,
                                BlockRequestKind strategy) {
  has_pending_request = true;
  block->materialize_strategy = GRANARY_MAX(block->materialize_strategy,
                                            strategy);
}

namespace {

// Create an intermediate basic block that adapts one version of a block to
// another version.
static CompensationBasicBlock *AdaptToBlock(LocalControlFlowGraph *cfg,
                                            BlockMetaData *meta,
                                            BasicBlock *existing_block) {
  auto adapt_block = new CompensationBasicBlock(cfg, meta);
  adapt_block->AppendInstruction(std::move(lir::Jump(existing_block)));
  return adapt_block;
}

}  // namespace

// Convert a decoded instruction into the internal Granary instruction IR.
Instruction *BlockFactory::MakeInstruction(arch::Instruction *instr) {
  if (instr->HasIndirectTarget()) {
    if (instr->IsFunctionCall() || instr->IsJump()) {  // Indirect jump/call.
      return new ControlFlowInstruction(
          instr,
          new IndirectBasicBlock(cfg, context->AllocateEmptyBlockMetaData()));

    // Return, with default empty meta-data.
    } else if (instr->IsFunctionReturn()) {
      return new ControlFlowInstruction(
          instr, new ReturnBasicBlock(
              cfg, context->AllocateEmptyBlockMetaData()));

    // System call/return, interrupt call/return.
    } else {
      return new ControlFlowInstruction(instr, new NativeBasicBlock(nullptr));
    }

  } else if (instr->IsJump() || instr->IsFunctionCall()) {
    auto meta = context->AllocateBlockMetaData(instr->BranchTargetPC());
    return new ControlFlowInstruction(instr, new DirectBasicBlock(cfg, meta));
  } else {
    return new NativeInstruction(instr);
  }
}

// Add the fall-through instruction for a block.
void BlockFactory::AddFallThroughInstruction(DecodedBasicBlock *block,
                                             Instruction *last_instr,
                                             AppPC pc) {

  auto cfi = DynamicCast<ControlFlowInstruction *>(last_instr);
  if (!cfi) return;

  BasicBlock *fall_through(nullptr);
  if (cfi->IsFunctionCall() || cfi->IsConditionalJump() ||
      cfi->IsSystemCall() || cfi->IsInterruptCall()) {
    fall_through = Materialize(pc).release();
    block->AppendInstruction(std::move(lir::Jump(fall_through)));
  } else if (cfi->IsUnconditionalJump() && !cfi->HasIndirectTarget()) {
    fall_through = cfi->TargetBlock();
  }

  // If we're doing a function call or a direct jump, then always
  // materialize the next block. In the case of function calls, this
  // letsus avoid issues related to `setjmp` and `longjmp`. In both
  // cases, this allows us to avoid unnecessary edge code when we know
  // ahead of time that we will reach the desired code.
  if (cfi->IsFunctionCall() || cfi->IsUnconditionalJump()) {
    RequestBlock(fall_through, REQUEST_CHECK_INDEX_AND_LCFG);
  }
}

// Annotate the instruction list based on the just-added instruction. This adds
// in the `IA_UNKNOWN_STACK` annotation when the decoded instruction resulted in
// the addition of an `IA_UNDEFINED_STACK` annotation. These two annotations
// are used during code assembly to split up blocks into fragments.
static void AnnotateInstruction(DecodedBasicBlock *block,
                                Instruction *begin) {
  bool in_undefined_state = false;
  for (auto instr : InstructionListIterator(begin)) {
    if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      if (IA_UNDEFINED_STACK == annot->annotation) {
        in_undefined_state = true;
      } else if (IA_VALID_STACK == annot->annotation) {
        in_undefined_state = false;
      }
    }
  }
  if (in_undefined_state) {
    block->UnsafeAppendInstruction(
        new AnnotationInstruction(IA_UNKNOWN_STACK_ABOVE));
  }
}

// Decode an instruction list starting at `pc` and link the decoded
// instructions into the instruction list beginning with `instr`.
void BlockFactory::DecodeInstructionList(DecodedBasicBlock *block) {
  auto pc = block->StartAppPC();
  arch::InstructionDecoder decoder;
  Instruction *instr(nullptr);
  do {
    auto decoded_pc = pc;
    arch::Instruction dinstr;
    auto before_instr = block->LastInstruction()->Previous();
    if (!decoder.DecodeNext(&dinstr, &pc) || dinstr.IsInterruptCall()) {
      block->AppendInstruction(std::move(lir::Jump(
          new NativeBasicBlock(decoded_pc))));
      return;
    }
    decoder.Mangle(block, &dinstr);
    instr = MakeInstruction(&dinstr);
    block->UnsafeAppendInstruction(instr);
    AnnotateInstruction(block, before_instr);
  } while (!IsA<ControlFlowInstruction *>(instr));
  AddFallThroughInstruction(block, instr, pc);
}

// Iterates through the blocks and tries to materialize `DirectBasicBlock`s.
// Returns `true` if any changes were made to the LCFG.
bool BlockFactory::MaterializeDirectBlocks(void) {
  auto materialized_a_block = false;
  for (auto block : cfg->Blocks()) {
    if (block == cfg->first_new_block) break;
    auto direct_block = DynamicCast<DirectBasicBlock *>(block);
    if (direct_block && MaterializeBlock(direct_block)) {
      materialized_a_block = true;
    }
    if (block == last_block) break;
  }
  return materialized_a_block;
}

// Unlink old blocks from the control-flow graph by changing the targets of
// CTIs going to now-materialized `DirectBasicBlock`s.
void BlockFactory::RelinkCFIs(void) {
  for (auto block : cfg->Blocks()) {
    if (block == cfg->first_new_block) break;
    for (auto succ : block->Successors()) {
      auto direct_block = DynamicCast<DirectBasicBlock *>(succ.block);
      if (direct_block && direct_block->materialized_block) {
        auto materialized_block = direct_block->materialized_block;
        cfg->AddBlock(materialized_block);
        succ.cfi->ChangeTarget(materialized_block);
      }
    }
    if (block == last_block) break;
  }
}

// Remove blocks that are now unnecessary.
void BlockFactory::RemoveOldBlocks(void) {
  BasicBlock *prev(nullptr);
  for (auto block = cfg->first_block; block; ) {
    if (block == cfg->first_new_block) break;
    auto next_block = block->list.GetNext(block);
    if (block->CanDestroy()) {
      block->list.Unlink();
      if (cfg->last_block == block) {
        cfg->last_block = prev;
      }
      delete block;
    } else {
      prev = block;
    }
    if (block == last_block) break;
    block = next_block;
  }
}

// Search an LCFG for a block whose meta-data matches the meta-data of
// `exclude`. The returned block, if any, is guaranteed not to be `exclude`,
// as well as not being another `DirectBasicBlock` instance.
InstrumentedBasicBlock *BlockFactory::MaterializeFromLCFG(
    DirectBasicBlock *exclude) {
  InstrumentedBasicBlock *adapt_block(nullptr);
  const auto exclude_meta = exclude->meta;
  for (auto block : cfg->ReverseBlocks()) {
    if (block == exclude) continue;

    // Allow us to materialize with a block that hasn't been added to the LCFG
    // yet but is part of this materialization step.
    if (auto direct_block = DynamicCast<DirectBasicBlock *>(block)) {
      block = direct_block->materialized_block;
    }

    // Only materialize with blocks that should have meta-data.
    auto inst_block = DynamicCast<InstrumentedBasicBlock *>(block);
    if (!inst_block) {
      continue;
    }

    auto block_meta = inst_block->meta;
    if (GRANARY_UNLIKELY(!block_meta)) continue;  // Unspecialized return block.

    // This block is the compensation block created when we translated
    // the target block of an indirect jump.
    if (auto comp_block = DynamicCast<CompensationBasicBlock *>(block)) {
      if (!comp_block->is_comparable) continue;
    }

    // Indexable meta-data doesn't match.
    if (!exclude_meta->Equals(block_meta)) continue;

    switch (exclude_meta->CanUnifyWith(block_meta)) {
      case UnificationStatus::ACCEPT:
        delete exclude_meta;  // No longer needed.
        exclude->meta = nullptr;
        return inst_block;  // Perfect match.
      case UnificationStatus::ADAPT:
        adapt_block = inst_block;  // Need compensation code.
        break;
      case UnificationStatus::REJECT:
        break;
    }
  }
  if (adapt_block) {  // Need to create some compensation code.
    exclude->meta = nullptr;  // Steal.
    return AdaptToBlock(cfg, exclude_meta, adapt_block);
  }
  return nullptr;
}

// Returns true if we can try to materialize this block. Requires that the
// block has not already been materialized.
bool BlockFactory::CanMaterializeBlock(DirectBasicBlock *block) {
  if (block->materialized_block ||
      REQUEST_LATER == block->materialize_strategy ||
      REQUEST_DENIED == block->materialize_strategy) {
    return false;
  }
  return true;
}

// Request a block from the code cache index. If an existing block can be
// adapted, then we will use that.
InstrumentedBasicBlock *BlockFactory::RequestIndexedBlock(
    BlockMetaData **meta_ptr) {
  auto meta = *meta_ptr;
  auto index = context->CodeCacheIndex();
  const auto response = index->Request(meta);
  switch (response.status) {
    case UnificationStatus::ACCEPT: {
      auto new_block = new CachedBasicBlock(cfg, response.meta);
      if (response.meta != meta) delete meta;  // No longer needed.
      *meta_ptr = nullptr;
      return new_block;
    }
    case UnificationStatus::ADAPT: {
      auto cached_block = new CachedBasicBlock(cfg, response.meta);
      auto adapt_block = AdaptToBlock(cfg, meta, cached_block);
      *meta_ptr = nullptr;  // Steal.
      return adapt_block;
    }
    case UnificationStatus::REJECT:
      return nullptr;
  }
}

namespace {

// Materialize a request to a code cache location to that exact code cache
// location.
//
// Note: This is a temporary hack, while I punt on the issue of function return
//       specialization.
//
// TODO(pag): This does not fit with the model of return specialization,
//            especially in the context of something like `longjmp`.
CompensationBasicBlock *MaterializeToExistingBlock(LocalControlFlowGraph *cfg,
                                                   BlockMetaData *meta,
                                                   AppPC non_transparent_pc) {
  auto bb = AdaptToBlock(cfg, meta, new NativeBasicBlock(non_transparent_pc));
  cfg->AddBlock(bb);
  return bb;
}

}  // namespace

// Request a block that is the target of an indirect control-flow instruction.
// To provide maximum flexibility (e.g. allow selective going native of
// targets), we generate a dummy compensation fragment that jumps to a direct
// basic block with a default non-`REQUEST_LATER` materialization strategy.
InstrumentedBasicBlock *BlockFactory::MaterializeInitialIndirectBlock(
    BlockMetaData *meta) {
  AppPC non_transparent_pc(nullptr);
  auto app_meta = MetaDataCast<AppMetaData *>(meta);
  auto module = context->FindModuleContainingPC(app_meta->start_pc);

  // Aagh! Indirect jump to some already cached code. For the time being,
  // give up and just go to the target and ignore the meta-data.
  if (ModuleKind::GRANARY_CODE_CACHE == module->Kind()) {
    non_transparent_pc = app_meta->start_pc;
    return MaterializeToExistingBlock(cfg, meta, non_transparent_pc);
  }

  GRANARY_ASSERT(ModuleKind::GRANARY != module->Kind());
  GRANARY_ASSERT(ModuleKind::GRANARY_CLIENT != module->Kind());

  auto dest_meta = context->AllocateBlockMetaData(app_meta->start_pc);
  auto direct_block = new DirectBasicBlock(cfg, dest_meta, non_transparent_pc);

  // Default to having a materialization strategy, and make it so that no one
  // can materialize against this block.
  auto adapt_block = AdaptToBlock(cfg, meta, direct_block);
  adapt_block->is_comparable = false;
  cfg->AddBlock(adapt_block);
  RequestBlock(direct_block, REQUEST_CHECK_INDEX_AND_LCFG);
  return adapt_block;
}

// Materialize a basic block if there is a pending request.
bool BlockFactory::MaterializeBlock(DirectBasicBlock *block) {
  if (!CanMaterializeBlock(block)) return false;
  if (MAX_NUM_MATERIALIZATION_REQUESTS < cfg->num_basic_blocks &&
      REQUEST_NOW > block->materialize_strategy) {
    block->materialize_strategy = REQUEST_DENIED;
    return false;
  }
  switch (block->materialize_strategy) {
    case REQUEST_CHECK_INDEX_AND_LCFG:
    case REQUEST_CHECK_INDEX_AND_LCFG_ONLY:
      if ((block->materialized_block = RequestIndexedBlock(&(block->meta)))) {
        break;
      }
    [[clang::fallthrough]];
    case REQUEST_CHECK_LCFG:
      if ((block->materialized_block = MaterializeFromLCFG(block)) ||
          REQUEST_CHECK_INDEX_AND_LCFG_ONLY == block->materialize_strategy) {
        break;
      }
    [[clang::fallthrough]];
    case REQUEST_NOW: {
      auto decoded_block = new DecodedBasicBlock(cfg, block->meta);
      block->meta = nullptr;  // Steal.
      block->materialized_block = decoded_block;
      DecodeInstructionList(decoded_block);
      break;
    }
    case REQUEST_NATIVE: {
      auto dest_pc = block->non_transparent_pc;
      if (GRANARY_LIKELY(!dest_pc)) dest_pc = block->StartAppPC();
      auto native_block = new NativeBasicBlock(dest_pc);
      delete block->meta;
      block->materialized_block = native_block;
      block->meta = nullptr;  // No longer needed.
      break;
    }
    case REQUEST_LATER: break;
    case REQUEST_DENIED: break;
  }
  return nullptr != block->materialized_block;
}

// Satisfy all materialization requests.
void BlockFactory::MaterializeRequestedBlocks(void) {
  cfg->first_new_block = nullptr;
  last_block = cfg->last_block;
  has_pending_request = false;
  if (MaterializeDirectBlocks()) {
    RelinkCFIs();
    RemoveOldBlocks();
  }
}

// Materialize the initial basic block.
void BlockFactory::MaterializeInitialBlock(BlockMetaData *meta) {
  GRANARY_ASSERT(nullptr != meta);
  auto decoded_block = new DecodedBasicBlock(cfg, meta);
  DecodeInstructionList(decoded_block);
  cfg->AddBlock(decoded_block);
}

// Create a new (future) basic block. This block is left as un-owned and
// will not appear in any iterators until some instruction takes ownership
// of it. This can be achieved by targeting this newly created basic block
// with a CTI.
std::unique_ptr<DirectBasicBlock> BlockFactory::Materialize(
    AppPC start_pc) {
  return std::unique_ptr<DirectBasicBlock>(
      new DirectBasicBlock(cfg, context->AllocateBlockMetaData(start_pc)));
}

}  // namespace granary
