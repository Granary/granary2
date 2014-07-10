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
#include "granary/module.h"
#include "granary/util.h"

namespace granary {

// Initialize the factory with an environment and a local control-flow graph.
// The environment is needed for lookups in the code cache index, and the LCFG
// is needed so that blocks can be added.
BlockFactory::BlockFactory(ContextInterface *context_,
                           LocalControlFlowGraph *cfg_)
    : context(context_),
      cfg(cfg_),
      has_pending_request(false) {}

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
void BlockFactory::RequestBlock(DirectBasicBlock *block,
                                BlockRequestKind strategy) {
  GRANARY_ASSERT(block && block->list.IsAttached());
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
void BlockFactory::AddFallThroughInstruction(
    arch::InstructionDecoder *decoder, DecodedBasicBlock *block,
    Instruction *last_instr, AppPC pc) {

  auto cti = DynamicCast<ControlFlowInstruction *>(last_instr);
  if (cti && (cti->IsFunctionCall() || cti->IsConditionalJump() ||
              cti->IsSystemCall() || cti->IsInterruptCall())) {
    // Unconditionally decode the next instruction. If it's a jump then we'll
    // use the jump as the fall-through. If we can't decode it then we'll add
    // a fall-through to native, and if it's neither then just add in a LIR
    // instruction for the fall-through.
    arch::Instruction dinstr;
    if (!decoder->Decode(&dinstr, pc)) {
      block->AppendInstruction(std::move(lir::Jump(new NativeBasicBlock(pc))));
    } else if (dinstr.IsUnconditionalJump()) {
      decoder->Mangle(block, &dinstr);
      block->UnsafeAppendInstruction(MakeInstruction(&dinstr));
    } else {
      block->AppendInstruction(std::move(lir::Jump(this, pc)));
    }
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
    block->UnsafeAppendInstruction(new AnnotationInstruction(IA_UNKNOWN_STACK));
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
    if (!decoder.DecodeNext(&dinstr, &pc)) {
      block->AppendInstruction(std::move(lir::Jump(
          new NativeBasicBlock(decoded_pc))));
      return;
    }
    decoder.Mangle(block, &dinstr);
    instr = MakeInstruction(&dinstr);
    block->UnsafeAppendInstruction(instr);
    AnnotateInstruction(block, before_instr);
  } while (!IsA<ControlFlowInstruction *>(instr));
  AddFallThroughInstruction(&decoder, block, instr, pc);
}

// Iterates through the blocks and tries to materialize `DirectBasicBlock`s.
// Returns `true` if any changes were made to the LCFG.
bool BlockFactory::MaterializeDirectBlocks(void) {
  auto materialized_a_block = false;
  for (auto block : BasicBlockIterator(cfg->first_block)) {
    auto direct_block = DynamicCast<DirectBasicBlock *>(block);
    if (direct_block && MaterializeBlock(direct_block)) {
      materialized_a_block = true;
    }
  }
  return materialized_a_block;
}

// Unlink old blocks from the control-flow graph by changing the targets of
// CTIs going to now-materialized `DirectBasicBlock`s.
void BlockFactory::RelinkCFIs(void) {
  for (auto block : BasicBlockIterator(cfg->first_block)) {
    if (block == cfg->first_new_block) break;

    for (auto succ : block->Successors()) {
      auto direct_block = DynamicCast<DirectBasicBlock *>(succ.block);
      if (direct_block && direct_block->materialized_block) {
        auto materialized_block = direct_block->materialized_block;
        cfg->AddBlock(materialized_block);
        succ.cti->ChangeTarget(materialized_block);
      }
    }
  }
}

// Remove blocks that are now unnecessary.
void BlockFactory::RemoveOldBlocks(void) {
  BasicBlock *prev(cfg->first_block);
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
    block = next_block;
  }
}

// Runs some simple analysis (for the purposes of internal meta-data) of the
// just-materialized basic blocks. This is often necessary because the results
// of these analyses might become incomplete at later stages due to
// interference by instrumentation tools.
void BlockFactory::AnalyzeNewBlocks(void) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto block : cfg->NewBlocks()) {
      if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
        auto meta = GetMetaData<LiveRegisterMetaData>(decoded_block);
        changed = meta->AnalyzeBlock(decoded_block) || changed;
      }
    }
  }
}

// Search an LCFG for a block whose meta-data matches the meta-data of
// `exclude`. The returned block, if any, is guaranteed not to be `exclude`,
// as well as not being another `DirectBasicBlock` instance.
InstrumentedBasicBlock *BlockFactory::MaterializeFromLCFG(
    DirectBasicBlock *exclude) {
  InstrumentedBasicBlock *adapt_block(nullptr);
  auto exclude_meta = exclude->meta;
  for (auto block : BasicBlockIterator(cfg->first_block)) {
    if (block == exclude) continue;
    auto inst_block = DynamicCast<InstrumentedBasicBlock *>(block);
    if (!inst_block || IsA<DirectBasicBlock *>(inst_block)) {
      continue;
    }
    if (auto comp_block = DynamicCast<CompensationBasicBlock *>(block)) {
      // This block is the compensation block created when we try to translate
      // the target block of an indirect jump.
      if (!comp_block->is_comparable) continue;
    }
    auto block_meta = inst_block->meta;
    if (!block_meta || !exclude_meta->Equals(block_meta)) {
      continue;  // Indexable meta-data doesn't match.
    }
    switch (exclude_meta->CanUnifyWith(block_meta)) {
      case UnificationStatus::ACCEPT:
        delete exclude->meta;  // No longer needed.
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

static void JoinMetaData(ContextInterface *context, BlockMetaData *meta,
                          AppPC cache_pc) {
  // Set up `CacheMetaData::start_pc` as that is what the shadow index uses for
  // lookup.
  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  cache_meta->start_pc = UnsafeCast<CachePC>(cache_pc);

  auto shadow_index = context->ShadowCodeCacheIndex();
  auto response = shadow_index->Request(meta);

  // TODO(pag): This case might be possible if we request that the block
  //            immediately following a function call go native. Then we won't
  //            necessarily have meta-data for the targeted block.
  //
  // TODO(pag): Another possibility is that a computation based on a return
  //            address is performed. This case is explicitly not handled,
  //            although an approach like DynamoRIO takes for signal-delaying
  //            might be appropriate. It would require:
  //                1)  Find the nearest known cache return address to the one
  //                    being requested. Nearest being a heuristic. In practice
  //                    this might be worth experimenting with.
  //                2)  Apply the same displacement between the requested and
  //                    found return address to the `ModuleMetaData::start_pc`
  //                    associated with the found return address.
  //                3)  Combine and translate with this block.
  GRANARY_ASSERT(UnificationStatus::REJECT != response.status);

  // Clear `CacheMetaData::start_pc` as we don't want it to look like this
  // block has been encoded when it hasn't been.
  cache_meta->start_pc = nullptr;

  // Now combine the "fixed" meta-data with the found meta-data. This will,
  // among other things, overwrite the `ModuleMetaData` of meta (which points
  // into the code cache) with one that points to native code.
  meta->JoinWith(response.meta);
}

}  // namespace

// Request a block that is the target of an indirect control-flow instruction.
// To provide maximum flexibility (e.g. allow selective going native of
// targets), we generate a dummy compensation fragment that jumps to a direct
// basic block with a default non-`REQUEST_LATER` materialization strategy.
InstrumentedBasicBlock *BlockFactory::MaterializeInitialIndirectBlock(
    BlockMetaData *meta) {
  AppPC non_transparent_pc(nullptr);
  auto module_meta = MetaDataCast<ModuleMetaData *>(meta);
  auto module = module_meta->source.module;

  if (ModuleKind::GRANARY_CODE_CACHE == module->Kind()) {
    non_transparent_pc = module_meta->start_pc;
    JoinMetaData(context, meta, non_transparent_pc);
    module_meta = MetaDataCast<ModuleMetaData *>(meta);  // Compiler hint ;-)
    module = nullptr;
  }

  auto dest_meta = context->AllocateBlockMetaData(module_meta->start_pc);
  auto direct_block = new DirectBasicBlock(cfg, dest_meta, non_transparent_pc);

  // Default to having a materialization strategy, and make it so that no one
  // can materialize against this block.
  direct_block->materialize_strategy = REQUEST_CHECK_INDEX_AND_LCFG;
  auto adapt_block = AdaptToBlock(cfg, meta, direct_block);
  adapt_block->is_comparable = false;
  cfg->AddBlock(adapt_block);
  return adapt_block;
}

// Materialize a basic block if there is a pending request.
bool BlockFactory::MaterializeBlock(DirectBasicBlock *block) {
  if (!CanMaterializeBlock(block)) return false;
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
  has_pending_request = false;
  if (MaterializeDirectBlocks()) {
    RelinkCFIs();
    RemoveOldBlocks();
    AnalyzeNewBlocks();
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
