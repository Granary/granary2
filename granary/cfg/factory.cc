/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "dependencies/xxhash/hash.h"

#include "granary/arch/driver.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"
#include "granary/cfg/factory.h"

#include "granary/code/metadata.h"
#include "granary/code/register.h"

#include "granary/context.h"
#include "granary/module.h"
#include "granary/util.h"

namespace granary {

// Initialize the factory with an environment and a local control-flow graph.
// The environment is needed for lookups in the code cache index, and the LCFG
// is needed so that blocks can be added.
BlockFactory::BlockFactory(ContextInterface *context_,
                           LocalControlFlowGraph *cfg_)
    : meta_data_filter(),
      context(context_),
      cfg(cfg_),
      has_pending_request(false) {}

// Request that a block be materialized. This does nothing if the block is
// not a `DirectBasicBlock`.
void BlockFactory::RequestBlock(BasicBlock *block, BlockRequestKind strategy) {
  auto direct_block = DynamicCast<DirectBasicBlock *>(block);
  if (direct_block) {
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

// Hash some basic block meta-data.
static uint32_t HashMetaData(HashFunction *hasher,
                             InstrumentedBasicBlock *block) {
  hasher->Reset();
  if (auto meta = block->UnsafeMetaData()) {
    meta->Hash(hasher);
  }
  hasher->Finalize();
  return hasher->Extract32();
}
}  // namespace

// Convert a decoded instruction into the internal Granary instruction IR.
Instruction *BlockFactory::MakeInstruction(arch::Instruction *instr) {
  if (instr->HasIndirectTarget()) {
    if (instr->IsFunctionCall() || instr->IsJump()) {  // Indirect jump/call.
      return new ControlFlowInstruction(
          instr,
          new IndirectBasicBlock(context->AllocateEmptyBlockMetaData()));

    // Return, with default empty meta-data.
    } else if (instr->IsFunctionReturn()) {
      return new ControlFlowInstruction(
          instr, new ReturnBasicBlock(context->AllocateEmptyBlockMetaData()));

    // System call/return, interrupt call/return.
    } else {
      return new ControlFlowInstruction(instr, new NativeBasicBlock(nullptr));
    }

  } else if (instr->IsJump() || instr->IsFunctionCall()) {
    auto meta = context->AllocateBlockMetaData(instr->BranchTargetPC());
    return new ControlFlowInstruction(instr, new DirectBasicBlock(meta));
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
      block->AppendInstruction(lir::Jump(new NativeBasicBlock(pc)));
    } else if (dinstr.IsUnconditionalJump()) {
      decoder->Mangle(block, &dinstr);
      block->UnsafeAppendInstruction(MakeInstruction(&dinstr));
    } else {
      block->AppendInstruction(lir::Jump(this, pc));
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
      block->AppendInstruction(lir::Jump(new NativeBasicBlock(decoded_pc)));
      return;
    }
    decoder.Mangle(block, &dinstr);
    instr = MakeInstruction(&dinstr);
    block->UnsafeAppendInstruction(instr);
    AnnotateInstruction(block, before_instr);
  } while (!IsA<ControlFlowInstruction *>(instr));
  AddFallThroughInstruction(&decoder, block, instr, pc);
}

// Hash the meta data of all basic blocks. This resets the `materialized_block`
// of any `DirectBasicBlock` from prior materialization runs.
void BlockFactory::HashBlockMetaDatas(HashFunction *hasher) {
  for (auto block : cfg->Blocks()) {
    if (auto meta_block = DynamicCast<InstrumentedBasicBlock *>(block)) {
      meta_block->cached_meta_hash = HashMetaData(hasher, meta_block);
      auto direct_block = DynamicCast<DirectBasicBlock *>(block);
      if (!direct_block) {
        meta_data_filter.Add({meta_block->cached_meta_hash});
      } else {
        direct_block->materialized_block = nullptr;
      }
    }
  }
}

// Iterates through the blocks and tries to materialize `DirectBasicBlock`s.
// Returns `true` if any changes were made to the LCFG.
bool BlockFactory::MaterializeDirectBlocks(void) {
  // Note: Can't use block iterator as it might GC some of the blocks that are
  //       as-of-yet unreferenced!
  bool materialized_a_block(false);
  for (BasicBlock *block = cfg->first_block, *last_block = cfg->last_block;
       nullptr != block;
       block = block->list.GetNext(block)) {

    auto direct_block = DynamicCast<DirectBasicBlock *>(block);
    if (direct_block && MaterializeBlock(direct_block)) {
      materialized_a_block = true;
    }
    if (block == last_block) {
      break;  // Can't materialize newly added blocks.
    }
  }
  return materialized_a_block;
}

// Unlink old blocks from the control-flow graph by changing the targets of
// CTIs going to now-materialized `DirectBasicBlock`s.
void BlockFactory::RelinkCFIs(void) {
  // Note: Can't use block iterator as it might GC some of the blocks that are
  //       as-of-yet unreferenced!
  for (BasicBlock *block(cfg->first_block);
       block != cfg->first_new_block;
       block = block->list.GetNext(block)) {

    for (auto succ : block->Successors()) {
      auto direct_block = DynamicCast<DirectBasicBlock *>(succ.block);
      if (direct_block && direct_block->materialized_block) {
        succ.cti->ChangeTarget(direct_block->materialized_block);
      }
    }
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
BasicBlock *BlockFactory::MaterializeFromLCFG(DirectBasicBlock *exclude) {
  // Note: Can't use block iterator as it might GC some of the blocks that are
  //       as-of-yet unreferenced!
  for (BasicBlock *block(cfg->first_block);
       block != cfg->first_new_block;
       block = block->list.GetNext(block)) {
    auto meta_block = DynamicCast<InstrumentedBasicBlock *>(block);
    if (meta_block &&
        meta_block != exclude &&
        meta_block->cached_meta_hash == exclude->cached_meta_hash &&
        !IsA<DirectBasicBlock *>(meta_block) &&
        meta_block->meta->Equals(exclude->meta)) {
      return meta_block;
    }
  }
  return nullptr;
}

// Returns true if we can try to materialize this block. If it looks like
// there's a pending request then we double check that the module of the
// requested block matches the module of the LCFG's first block. If they don't
// match then we permanently deny materialization within this session. The
// reason for this is that we want all code cache allocations to be specific
// to an individual module.
bool BlockFactory::CanMaterializeBlock(DirectBasicBlock *block) {
  if (block->materialized_block ||
      REQUEST_LATER == block->materialize_strategy ||
      REQUEST_DENIED == block->materialize_strategy) {
    return false;
  } else {
    auto first_meta = GetMetaData<ModuleMetaData>(cfg->first_block);
    if (!first_meta->CanMaterializeWith(GetMetaData<ModuleMetaData>(block))) {
      block->materialize_strategy = REQUEST_DENIED;
      return false;  // Modules of requested and first blocks don't match.
    } else {
      return true;
    }
  }
}

// Materialize a basic block if there is a pending request.
bool BlockFactory::MaterializeBlock(DirectBasicBlock *block) {
  if (CanMaterializeBlock(block)) {
    switch (block->materialize_strategy) {
      case REQUEST_CHECK_INDEX_AND_LCFG:
        // TODO(pag): Implement me.
        // Fall-through.

      case REQUEST_CHECK_LCFG:
        if (meta_data_filter.MightContain({block->cached_meta_hash})) {
          if (BasicBlock *found_block = MaterializeFromLCFG(block)) {
            block->materialized_block = found_block;
            return true;
          }
        }
        // Fall-through.

      case REQUEST_NOW: {
        auto decoded_block = new DecodedBasicBlock(cfg, block->meta);
        block->meta = nullptr;  // Steal.
        DecodeInstructionList(decoded_block);
        cfg->AddBlock(decoded_block);
        block->materialized_block = decoded_block;
        return true;
      }

      case REQUEST_NATIVE:
        block->materialized_block = new NativeBasicBlock(block->StartAppPC());
        return true;

      default: {}  // REQUEST_LATER, REQUEST_DENIED.
    }
  }
  return false;
}

// Satisfy all materialization requests.
void BlockFactory::MaterializeRequestedBlocks(void) {
  xxhash::HashFunction hasher(0xDEADBEEFUL);
  meta_data_filter.Clear();
  HashBlockMetaDatas(&hasher);
  cfg->first_new_block = nullptr;
  has_pending_request = false;
  if (MaterializeDirectBlocks() || cfg->first_new_block) {
    RelinkCFIs();
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
      new DirectBasicBlock(context->AllocateBlockMetaData(start_pc)));
}

}  // namespace granary
