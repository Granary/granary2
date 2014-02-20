/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "dependencies/xxhash/hash.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/factory.h"

#include "granary/ir/lir.h"

#include "granary/driver.h"
#include "granary/environment.h"
#include "granary/metadata.h"
#include "granary/module.h"

namespace granary {

// Initialize the factory with an environment and a local control-flow graph.
// The environment is needed for lookups in the code cache index, and the LCFG
// is needed so that blocks can be added.
BlockFactory::BlockFactory(LocalControlFlowGraph *cfg_)
    : meta_data_filter(),
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

// Return a control-flow instruction that targets a future basic block.
static Instruction *MakeDirectCTI(driver::Instruction *instr, AppPC target_pc) {
  return new ControlFlowInstruction(
      instr, new DirectBasicBlock(new GenericMetaData(target_pc)));
}

// Return a control-flow instruction that indirectly targets an unknown future
// basic block.
static Instruction *MakeIndirectCTI(driver::Instruction *instr) {
  return new ControlFlowInstruction(
      instr, new IndirectBasicBlock(new GenericMetaData(nullptr)));
}

// Return a control-flow instruction that returns to some existing basic block.
static Instruction *MakeReturnCTI(driver::Instruction *instr) {
  return new ControlFlowInstruction(instr, new ReturnBasicBlock);
}

// Convert a decoded instruction into the internal Granary instruction IR.
static std::unique_ptr<Instruction> MakeInstruction(
    driver::Instruction *instr) {
  Instruction *ret_instr(nullptr);
  if (instr->HasIndirectTarget()) {
    if (instr->IsFunctionReturn() ||
        instr->IsInterruptReturn() ||
        instr->IsSystemReturn()) {
      ret_instr = MakeReturnCTI(instr);
    } else {
      ret_instr = MakeIndirectCTI(instr);
    }
  } else if (instr->IsJump() || instr->IsFunctionCall()) {
    ret_instr = MakeDirectCTI(instr, instr->BranchTarget());
  } else {
    ret_instr = new NativeInstruction(instr);
  }
  return std::unique_ptr<Instruction>(ret_instr);
}

// Add the fall-through instruction for a block.
static void AddFallThroughInstruction(BlockFactory *factory,
                                      driver::InstructionDecoder *decoder,
                                      Instruction *last_instr, AppPC pc) {
  auto cti = DynamicCast<ControlFlowInstruction *>(last_instr);
  if (cti && (cti->IsFunctionCall() || cti->IsConditionalJump())) {
    // Unconditionally decode the next instruction. If it's a jump then we'll
    // use the jump as the fall-through. If we can't decode it then we'll add
    // a fall-through to native, and if it's neither then just add in a LIR
    // instruction for the fall-through.
    std::unique_ptr<driver::Instruction> dinstr(new driver::Instruction);
    if (!decoder->Decode(dinstr.get(), pc)) {
      last_instr->InsertAfter(lir::Jump(new NativeBasicBlock(pc)));
    } else if (dinstr->IsUnconditionalJump()) {
      last_instr->InsertAfter(std::move(MakeInstruction(dinstr.release())));
    } else {
      last_instr->InsertAfter(lir::Jump(factory, pc));
    }
  }
}

// Decode the list of instructions and appends them to the first instruction in
// a basic block.
static void DecodeInstructionList(BlockFactory *factory, Instruction *instr,
                                  AppPC pc) {
  driver::InstructionDecoder decoder;
  for (; !IsA<ControlFlowInstruction *>(instr); ) {
    auto dinstr = new driver::Instruction;
    if (!decoder.DecodeNext(dinstr, &pc)) {
      instr->InsertAfter(lir::Jump(new NativeBasicBlock(pc)));
      delete dinstr;
      return;
    }
    instr = instr->InsertAfter(std::move(MakeInstruction(dinstr)));
  }
  AddFallThroughInstruction(factory, &decoder, instr, pc);
}

// Hash some basic block meta-data.
static uint32_t HashMetaData(HashFunction *hasher,
                             InstrumentedBasicBlock *block) {
  hasher->Reset();
  block->MetaData()->Hash(hasher);
  hasher->Finalize();
  return hasher->Extract32();
}

}  // namespace

// Hash the meta data of all basic blocks. This resets the `materialized_block`
// of any `DirectBasicBlock` from prior materialization runs.
void BlockFactory::HashBlockMetaDatas(HashFunction *hasher) {
  for (auto block : cfg->Blocks()) {
    auto meta_block = DynamicCast<InstrumentedBasicBlock *>(block);
    if (meta_block) {
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
  for (auto block = cfg->first_block, last_block = cfg->last_block;
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
  for (auto block = cfg->first_block;
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
  for (auto block = cfg->first_block;
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

// Materialize a basic block if there is a pending request.
bool BlockFactory::MaterializeBlock(DirectBasicBlock *block) {
  if (!block->materialized_block) {
    switch (block->materialize_strategy) {
      case REQUEST_LATER:
        return false;

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
        auto decoded_block = new DecodedBasicBlock(block->meta);
        block->meta = nullptr;  // Steal.
        DecodeInstructionList(this, decoded_block->FirstInstruction(),
                              block->StartAppPC());
        cfg->AddBlock(decoded_block);
        block->materialized_block = decoded_block;
        return true;
      }

      case REQUEST_NATIVE:
        block->materialized_block = new NativeBasicBlock(block->StartAppPC());
        return true;
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
void BlockFactory::MaterializeInitialBlock(GenericMetaData *meta) {
  GRANARY_IF_DEBUG( granary_break_on_fault_if(!meta); )
  auto decoded_block = new DecodedBasicBlock(meta);
  DecodeInstructionList(this, decoded_block->FirstInstruction(),
                        decoded_block->StartAppPC());
  cfg->AddBlock(decoded_block);
}

// Create a new (future) basic block. This block is left as un-owned and
// will not appear in any iterators until some instruction takes ownership
// of it. This can be achieved by targeting this newly created basic block
// with a CTI.
std::unique_ptr<DirectBasicBlock> BlockFactory::Materialize(
    AppPC start_pc) {
  auto block = new DirectBasicBlock(new GenericMetaData(start_pc));
  return std::unique_ptr<DirectBasicBlock>(block);
}

}  // namespace granary
