/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/driver.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"
#include "granary/cfg/factory.h"

#include "granary/code/register.h"

#include "granary/app.h"
#include "granary/cache.h"
#include "granary/context.h"
#include "granary/index.h"
#include "granary/util.h"

#include "os/exception.h"
#include "os/module.h"

namespace granary {
extern "C" {

// Address range of Granary/client-specific code that has been explicitly
// exported to instrumented code.
//
// Note: These symbols are defined by `linker.lds`.
extern uint8_t granary_begin_inst_exports;
extern uint8_t granary_end_inst_exports;

// User space-specific functions. If we find that `_fini` is being invoked,
// then we'll redirect execution to `exit_group`, which exit all
// threads in the process.
GRANARY_IF_USER( extern uint8_t _fini; )
GRANARY_IF_USER( extern uint8_t exit_group_ok; )

}  // extern C
namespace arch {

// Save some architectural state before `instr` executes, so that if a
// recoverable exception occurs while executing `instr`, we can handle it.
//
// Note: This has an architecture-specific implementation.
void SaveStateForExceptionCFI(DecodedBasicBlock *block,
                              ExceptionalControlFlowInstruction *instr,
                              granary::Instruction *before_instr);

}  // namespace arch
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
      generation(0) {}

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
  if (REQUEST_LATER != strategy) {
    if (-1 == block->generation) {
      block->generation = generation + 1;
    }
    auto old_strategy = block->materialize_strategy;
    block->materialize_strategy = GRANARY_MAX(block->materialize_strategy,
                                              strategy);
    if (old_strategy != block->materialize_strategy) {
      has_pending_request = true;
    }
  }
}

namespace {

// Converts a LIR instruction into an application instruction, where the
// PC associated with the instruction is `pc`.
static Instruction *AsApp(std::unique_ptr<Instruction> instr, AppPC pc) {
  auto ni = DynamicCast<NativeInstruction *>(instr.release());
  ni->MakeAppInstruction(pc);
  return ni;
}

// Converts a LIR instruction into an application instruction, where the
// PC associated with the instruction is the application `start_pc` stored
// in `meta`.
static Instruction *AsApp(std::unique_ptr<Instruction> instr,
                          BlockMetaData *meta) {
  return AsApp(std::move(instr), MetaDataCast<AppMetaData *>(meta)->start_pc);
}

// Create an intermediate basic block that adapts one version of a block to
// another version.
static CompensationBasicBlock *AdaptToBlock(LocalControlFlowGraph *cfg,
                                            BlockMetaData *meta,
                                            BasicBlock *existing_block) {
  auto adapt_block = new CompensationBasicBlock(cfg, meta);
  adapt_block->AppendInstruction(AsApp(lir::Jump(existing_block), meta));
  return adapt_block;
}

}  // namespace

// Convert a decoded instruction into the internal Granary instruction IR.
NativeInstruction *BlockFactory::MakeInstruction(
    arch::Instruction *instr, const arch::Instruction *orig_instr) {
  BasicBlock *target_block(nullptr);
  AppPC recovery_pc(nullptr);
  AppPC emulation_pc(nullptr);
  if (instr->HasIndirectTarget()) {
    if (instr->IsFunctionCall() || instr->IsJump()) {  // Indirect jump/call.
      target_block = new IndirectBasicBlock(
          cfg, context->AllocateEmptyBlockMetaData());

    // Return, with default empty meta-data.
    } else if (instr->IsFunctionReturn()) {
      target_block = new ReturnBasicBlock(
          cfg, context->AllocateEmptyBlockMetaData());

    // System call/return, interrupt call/return.
    } else {
      target_block = new NativeBasicBlock(nullptr);
    }

  // Direct jump or call.
  } else if (instr->IsJump() || instr->IsFunctionCall()) {
    auto meta = context->AllocateBlockMetaData(instr->BranchTargetPC());
    target_block = new DirectBasicBlock(cfg, meta);

  // Instruction that can trigger a recoverable exception.
  } else if (os::GetExceptionInfo(orig_instr, &recovery_pc, &emulation_pc)) {
    auto meta = context->AllocateBlockMetaData(recovery_pc);
    auto block = new DirectBasicBlock(cfg, meta);
    RequestBlock(block, REQUEST_DENIED);
    return new ExceptionalControlFlowInstruction(instr, orig_instr, block,
                                                 emulation_pc);
  // Normal instruction.
  } else {
    return new NativeInstruction(instr);
  }
  return new ControlFlowInstruction(instr, target_block);
}

// Add the fall-through instruction for a block.
void BlockFactory::AddFallThroughInstruction(DecodedBasicBlock *block,
                                             Instruction *last_instr,
                                             AppPC pc) {
  auto cfi = DynamicCast<ControlFlowInstruction *>(last_instr);
  if (!cfi) return;

  BasicBlock *fall_through(nullptr);

  GRANARY_ASSERT(!cfi->IsInterruptCall());

  // If we're doing a function call or a direct jump, then always
  // materialize the next block. In the case of function calls, this
  // lets us avoid issues related to `setjmp` and `longjmp`. In both
  // cases, this allows us to avoid unnecessary edge code when we know
  // ahead of time that we will reach the desired code.
  auto request_fall_through = cfi->IsFunctionCall() ||
                              IsA<ExceptionalControlFlowInstruction *>(cfi);

  if (request_fall_through || cfi->IsConditionalJump() || cfi->IsSystemCall()) {
    fall_through = new DirectBasicBlock(cfg,
                                        context->AllocateBlockMetaData(pc));
    block->AppendInstruction(AsApp(lir::Jump(fall_through), pc));

  // Inherit the fall-through from the target.
  } else if (cfi->IsUnconditionalJump() && !cfi->HasIndirectTarget()) {
    fall_through = cfi->TargetBlock();
    request_fall_through = true;
  }

  if (request_fall_through) {
    RequestBlock(fall_through, REQUEST_CHECK_INDEX_AND_LCFG);
  }
}

namespace {

// Annotate the instruction list based on the just-added instruction. This adds
// in the `IA_UNKNOWN_STACK` annotation when the decoded instruction resulted in
// the addition of an `IA_UNDEFINED_STACK` annotation. These two annotations
// are used during code assembly to split up blocks into fragments.
static void AnnotateInstruction(BlockFactory *factory, DecodedBasicBlock *block,
                                Instruction *begin, AppPC next_pc) {
  auto in_undefined_state = false;
  auto changes_interrupt_state = false;
  for (auto instr : InstructionListIterator(begin)) {

    // If we generated an exceptional control-flow instruction, then go and
    // save a bunch of machine state before the instruction.
    if (auto exc = DynamicCast<ExceptionalControlFlowInstruction *>(instr)) {
      arch::SaveStateForExceptionCFI(block, exc, begin);

    // Use the existing annotations added by the early mangler to generate new
    // annotations.
    } else if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      if (IA_INVALID_STACK == annot->annotation) {
        in_undefined_state = true;
      } else if (IA_VALID_STACK == annot->annotation) {
        in_undefined_state = false;
      } else if (IA_CHANGES_INTERRUPT_STATE == annot->annotation) {
        changes_interrupt_state = true;
      }
    }
  }
  if (in_undefined_state) {
    block->AppendInstruction(
        new AnnotationInstruction(IA_UNKNOWN_STACK_ABOVE));
  }
  if (changes_interrupt_state) {
    block->AppendInstruction(AsApp(lir::Jump(factory, next_pc), next_pc));
  }
}

}  // namespace

// Decode an instruction list starting at `pc` and link the decoded
// instructions into the instruction list beginning with `instr`.
void BlockFactory::DecodeInstructionList(DecodedBasicBlock *block) {
  auto decode_pc = block->StartAppPC();
  arch::InstructionDecoder decoder;
  arch::Instruction dinstr;
  arch::Instruction ainstr;
  Instruction *instr(nullptr);
  do {
    auto decoded_pc = decode_pc;
    auto before_instr = new AnnotationInstruction(
        IA_BEGIN_LOGICAL_INSTRUCTION, decode_pc);

    // Exist mostly to document instruction boundaries to client code.
    block->AppendInstruction(before_instr);

    // If we can't decode the instruction then just jump directly to it. Also,
    // if the instruction raises an interrupt, e.g. the debug trap, then assume
    // that is because of GDB debugging (or something similar) and go native
    // there as well.
    if (!decoder.DecodeNext(&dinstr, &decode_pc) || dinstr.IsInterruptCall()) {
      auto native_block = new NativeBasicBlock(decoded_pc);
      block->AppendInstruction(AsApp(lir::Jump(native_block), decoded_pc));
      return;
    }

    // Apply early mangling to the instruction, then add it in and annotate
    // it accordingly.
    memcpy(&ainstr, &dinstr, sizeof ainstr);
    decoder.Mangle(block, &dinstr);

    block->AppendInstruction(MakeInstruction(&dinstr, &ainstr));
    AnnotateInstruction(this, block, before_instr, decode_pc);

    instr = block->LastInstruction()->Previous();
  } while (!IsA<ControlFlowInstruction *>(instr));
  AddFallThroughInstruction(block , instr, decode_pc);
}

// Iterates through the blocks and tries to materialize `DirectBasicBlock`s.
// Returns `true` if any changes were made to the LCFG.
bool BlockFactory::MaterializeDirectBlocks(void) {
  auto materialized_a_block = false;
  for (auto block : cfg->Blocks()) {
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
  for (auto block : cfg->Blocks()) {
    for (auto succ : block->Successors()) {
      if (auto direct_block = DynamicCast<DirectBasicBlock *>(succ.block)) {
        if (auto materialized_block = direct_block->materialized_block) {
          cfg->AddBlock(materialized_block);
          succ.cfi->ChangeTarget(materialized_block);
        }
      }
    }
  }
}

// Remove blocks that are now unnecessary.
//
// TODO(pag): This might be a bit heavyweight. That is, it's not clear if
//            it's worth it to actually do this kind of mark & sweep garbage
//            collection of the blocks or not.
void BlockFactory::RemoveOldBlocks(void) {

  // First, make sure all blocks are added to the LCFG.
  for (auto block : cfg->Blocks()) {
    for (auto succ : block->Successors()) {
      cfg->AddBlock(succ.block);
    }
  }

  // Now mark all blocks, except the first block, as unreachable.
  auto first_block = cfg->first_block;
  auto second_block = first_block->list.GetNext(first_block);
  for (auto block = second_block; block; block = block->list.GetNext(block)) {
    block->is_reachable = false;  // Mark all blocks as unreachable.
  }

  first_block->is_reachable = true;

  // Transitively propagate reachability from the entry block. This is like
  // the "mark" phase of a mark & sweep GC.
  for (auto changed = true, can_make_progess = true;
       changed && can_make_progess; ) {
    changed = false;
    can_make_progess = false;
    for (auto block = first_block; block; block = block->list.GetNext(block)) {
      if (!block->is_reachable) {
        can_make_progess = true;
        continue;
      }
      for (auto succ : block->Successors()) {
        auto succ_block = succ.block;
        if (succ_block->is_reachable) continue;
        changed = true;
        succ_block->is_reachable = true;
      }
    }
  }

  // Garbage collect the unreachable blocks. This is like the "sweep" phase of
  // a mark & sweep GC.
  auto new_last_block = first_block;
  for (auto block = second_block, prev_block = first_block; block; ) {
    auto next_block = block->list.GetNext(block);
    if (!block->is_reachable) {
      if (cfg->first_new_block == block) {
        cfg->first_new_block = next_block;
      }
      block->list.Unlink();
      delete block;
    } else {
      new_last_block = block;
      prev_block = block;
    }
    block = next_block;
  }
  cfg->last_block = new_last_block;
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
  return -1 != block->generation && block->generation <= generation;
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
  auto block = AdaptToBlock(cfg, meta,
                            new NativeBasicBlock(non_transparent_pc));
  cfg->AddBlock(block);
  return block;
}

}  // namespace

// Request a block that is the target of an indirect control-flow instruction.
// To provide maximum flexibility (e.g. allow selective going native of
// targets), we generate a dummy compensation fragment that jumps to a direct
// basic block with a default non-`REQUEST_LATER` materialization strategy.
InstrumentedBasicBlock *BlockFactory::MaterializeIndirectEntryBlock(
    BlockMetaData *meta) {
  auto app_meta = MetaDataCast<AppMetaData *>(meta);
  auto target_pc = app_meta->start_pc;

  if (auto module = os::ModuleContainingPC(app_meta->start_pc)) {
    // Aagh! Indirect jump to some already cached code. For the time being,
    // give up and just go to the target and ignore the meta-data.
    if (os::ModuleKind::GRANARY_CODE_CACHE == module->Kind()) {
      granary_curiosity();  // TODO(pag): Issue #42.
      return MaterializeToExistingBlock(cfg, meta, target_pc);

    } else if (os::ModuleKind::GRANARY == module->Kind()) {
#ifdef GRANARY_WHERE_user
      // If we try to go to `_fini`, then redirect execution to `exit_group`.
      if (&_fini == target_pc) {
        target_pc = &exit_group_ok;
        app_meta->start_pc = &exit_group_ok;
      }
#endif
      GRANARY_ASSERT(&granary_begin_inst_exports <= target_pc &&
                     target_pc < &granary_end_inst_exports);
    }

  // TODO(pag): In release builds, module tracking is basically ignored.
  } else {
    GRANARY_ASSERT(false);
  }

  auto dest_meta = context->AllocateBlockMetaData(app_meta->start_pc);
  auto direct_block = new DirectBasicBlock(cfg, dest_meta, target_pc);

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

  // Make sure that code exported to instrumented application code is never
  // actually instrumented.
  auto start_pc = block->StartAppPC();
  if (&granary_begin_inst_exports <= start_pc &&
      start_pc < &granary_end_inst_exports) {
    block->materialize_strategy = REQUEST_NATIVE;
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
      cfg->AddBlock(decoded_block);
      DecodeInstructionList(decoded_block);
      for (auto succ : decoded_block->Successors()) cfg->AddBlock(succ.block);
      break;
    }
    case REQUEST_NATIVE: {
      auto dest_pc = block->non_transparent_pc;
      if (GRANARY_LIKELY(!dest_pc)) dest_pc = start_pc;
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
  ++generation;
  if (MaterializeDirectBlocks()) {
    RelinkCFIs();
    RemoveOldBlocks();
  }
}

// Materialize the initial basic block.
DecodedBasicBlock *BlockFactory::MaterializeDirectEntryBlock(
    BlockMetaData *meta) {
  GRANARY_ASSERT(nullptr != meta);
  auto decoded_block = new DecodedBasicBlock(cfg, meta);
  cfg->AddBlock(decoded_block);
  DecodeInstructionList(decoded_block);
  for (auto succ : decoded_block->Successors()) cfg->AddBlock(succ.block);
  decoded_block->generation = generation;
  return decoded_block;
}

// Try to request the initial entry block from the code cache index.
InstrumentedBasicBlock *BlockFactory::RequestDirectEntryBlock(
    BlockMetaData **meta) {
  if (auto entry_block = RequestIndexedBlock(meta)) {
    cfg->AddBlock(entry_block);
    return entry_block;
  }
  return nullptr;
}

// Create a new (future) basic block.
DirectBasicBlock *BlockFactory::Materialize(AppPC start_pc) {
  auto meta = context->AllocateBlockMetaData(start_pc);
  auto block = new DirectBasicBlock(cfg, meta);
  cfg->AddBlock(block);
  return block;
}

}  // namespace granary
