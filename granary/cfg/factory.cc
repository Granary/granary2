/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "dependencies/xxhash/hash.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/factory.h"

#include "granary/arch/driver.h"

#include "granary/ir/lir.h"

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
  granary_break_on_fault_if(!block || !block->list.IsAttached());
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
    if (!decoder->Decode(block, &dinstr, pc)) {
      block->AppendInstruction(lir::Jump(new NativeBasicBlock(pc)));
    } else if (dinstr.IsUnconditionalJump()) {
      block->UnsafeAppendInstruction(MakeInstruction(&dinstr));
    } else {
      block->AppendInstruction(lir::Jump(this, pc));
    }
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
    if (!decoder.DecodeNext(block, &dinstr, &pc)) {
      block->AppendInstruction(lir::Jump(new NativeBasicBlock(decoded_pc)));
      return;
    }
    instr = MakeInstruction(&dinstr);
    block->UnsafeAppendInstruction(instr);
    context->AnnotateInstruction(instr);
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
  }
}

// Materialize the initial basic block.
void BlockFactory::MaterializeInitialBlock(BlockMetaData *meta) {
  GRANARY_IF_DEBUG( granary_break_on_fault_if(!meta); )
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
