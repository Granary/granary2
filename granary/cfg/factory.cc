/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/driver.h"

#include "granary/base/option.h"

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

GRANARY_DEFINE_bool(transparent_returns, GRANARY_IF_USER_ELSE(true, false),
    "Enable transparent return addresses? The default is `"
    GRANARY_IF_USER_ELSE("yes", "no") "`.\n"
    "\n"
    "Note: Enabling transparent returns will introduce significant\n"
    "      performance overheads due to the extra complications involved\n"
    "      specializing function return targets."
    GRANARY_IF_USER("\n"
    "\n"
    "Note: Granary needs to preserve return address transparency when\n"
    "      comprehensively instrumenting user space programs. However, if a\n"
    "      program isn't being comprehensively instrumented, then return\n"
    "      address transparency can likely be enabled."));

GRANARY_DEFINE_positive_int(max_decoded_instructions_per_block, 16,
    "The maximum number of instructions to decode per basic block. The default "
    "value is `16`.");

namespace granary {
extern "C" {

// Address range of Granary/client-specific code.
extern const uint8_t granary_begin_text;
extern const uint8_t granary_end_text;

// Address range of Granary/client-specific code that has been explicitly
// exported to instrumented code.
//
// Note: These symbols are defined by `linker.lds`.
extern const uint8_t granary_begin_inst_exports;
extern const uint8_t granary_end_inst_exports;

extern const AppPC granary_block_cache_begin;
extern const AppPC granary_block_cache_end;
extern const AppPC granary_edge_cache_begin;
extern const AppPC granary_edge_cache_end;

// User space-specific functions. If we find that `_fini` is being invoked,
// then we'll redirect execution to `exit_group`, which exit all
// threads in the process.
GRANARY_IF_USER( extern const uint8_t _fini; )
GRANARY_IF_USER( extern const uint8_t exit_group_ok; )

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

// Initialize the factory with an environment and a local control-flow graph.
// The environment is needed for lookups in the code cache index, and the LCFG
// is needed so that blocks can be added.
BlockFactory::BlockFactory(Context *context_,
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
//
// Note: We don't check that `block` is attached to the CFG's block list
//       because in the worst case, it will result in an extra instrumentation
//       loop, and it makes it easier to request blocks ahead of time.
void BlockFactory::RequestBlock(DirectBasicBlock *block,
                                BlockRequestKind strategy) {
  auto old_strategy = block->materialize_strategy;
  block->materialize_strategy = GRANARY_MAX(block->materialize_strategy,
                                            strategy);
  if (old_strategy != block->materialize_strategy) {
    has_pending_request = true;
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
      target_block = new IndirectBasicBlock(cfg, new BlockMetaData);

    // Return, with default empty meta-data.
    } else if (instr->IsFunctionReturn()) {
      target_block = new ReturnBasicBlock(cfg, new BlockMetaData);

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
    RequestBlock(block, kRequestBlockInFuture);
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
  BasicBlock *fall_through_block(nullptr);

  // If the last instruction isn't a CFI, then we need to add a fall-through.
  auto add_fall_through_block = true;

  // Should we auto-submit a request to look up the fall-through block?
  auto request_fall_through_block = false;

  auto cfi = DynamicCast<ControlFlowInstruction *>(last_instr);
  if (cfi) {
    GRANARY_ASSERT(!cfi->IsInterruptCall());

    // Force us to request the fall through if we have an exceptional
    // control-flow instruction (kernel space faultable instruction) that is
    // otherwise not explicitly a control-flow instruction).
    add_fall_through_block = IsA<ExceptionalControlFlowInstruction *>(cfi);
    request_fall_through_block = add_fall_through_block;

    if (cfi->IsFunctionCall() && !FLAG_transparent_returns) {
      add_fall_through_block = false;
    }
  }

  if (add_fall_through_block || cfi->IsConditionalJump() ||
      cfi->IsSystemCall()) {
    auto meta = context->AllocateBlockMetaData(pc);
    fall_through_block = new DirectBasicBlock(cfg, meta);
    block->AppendInstruction(AsApp(lir::Jump(fall_through_block), pc));
  }

  if (request_fall_through_block) {
    RequestBlock(fall_through_block, kRequestBlockFromIndexOrCFG);
  }
}

namespace {

// Annotate the instruction list based on the just-added instruction. This adds
// in the `kAnnotUnknownStackAbove` annotation when the decoded instruction
// resulted in the addition of an `kAnnotInvalidStack` annotation. These two
// annotations are used during code assembly to split up blocks into fragments.
//
// The idea here is that one instruction might switch stacks, but the next
// instruction, operating on the new stack, might be operating on a valid stack.
// Ideally, we want to be able to take advantage of this, and
// `kAnnotUnknownStackAbove` exists to allow us to limit the scope of the
// stack undefinedness to only a single instruction.
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
      if (kAnnotInvalidStack == annot->annotation) {
        in_undefined_state = true;
      } else if (kAnnotValidStack == annot->annotation) {
        in_undefined_state = false;
      } else if (kAnnotInterruptDeliveryStateChange == annot->annotation) {
        changes_interrupt_state = true;
      }
    }
  }
  if (in_undefined_state) {
    block->AppendInstruction(
        new AnnotationInstruction(kAnnotUnknownStackAbove));
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
  auto num_instrs = FLAG_max_decoded_instructions_per_block;
  do {
    auto decoded_pc = decode_pc;
    auto before_instr = new AnnotationInstruction(
        kAnnotLogicalInstructionBoundary, decode_pc);

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
  } while (!IsA<ControlFlowInstruction *>(instr) && 0 < --num_instrs);
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
          GRANARY_ASSERT(materialized_block->list.IsLinked());
          succ.cfi->ChangeTarget(materialized_block);
        }
      }
    }
  }
}

// Remove blocks that are now unnecessary.
void BlockFactory::RemoveUnreachableBlocks(void) {

  // First, make sure all blocks are added to the LCFG.
  for (auto block : cfg->Blocks()) {
    for (auto succ : block->Successors()) {
      cfg->AddBlock(succ.block);
    }
  }

  // Then, mark all blocks as unreachable.
  for (auto block : cfg->Blocks()) {
    block->is_reachable = false;
  }

  // Make sure the entry block remains reachable.
  cfg->entry_block->is_reachable = true;

  ListOfListHead<BasicBlock> old_blocks;
  ListOfListHead<BasicBlock> new_blocks;
  ListOfListHead<BasicBlock> work_list;

  cfg->blocks.Remove(cfg->entry_block);
  work_list.Append(cfg->entry_block);
  auto new_gen = cfg->generation;

  while (auto block = work_list.First()) {
    work_list.Remove(block);

    // Process blocks off the work list as either being old or new blocks.
    if (block->generation < new_gen) {
      old_blocks.Append(block);
    } else {
      new_blocks.Append(block);
    }

    // Add successors to the work list.
    for (auto succ : block->Successors()) {
      if (succ.block->is_reachable) continue;
      succ.block->is_reachable = true;
      cfg->blocks.Remove(succ.block);
      work_list.Append(succ.block);
    }
  }

  // Any remaining blocks are unreachable.
  while (auto block = cfg->blocks.First()) {
    cfg->blocks.Remove(block);
    if (auto inst_block = DynamicCast<InstrumentedBasicBlock *>(block)) {
      auto meta = inst_block->UnsafeMetaData();
      if (meta && !IsA<CachedBasicBlock *>(block)) delete meta;
    }
    delete block;
  }
  cfg->blocks = old_blocks;
  cfg->first_new_block = new_blocks.First();
  cfg->blocks.Extend(new_blocks);
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
  return !(block->materialized_block ||
           kRequestBlockLater == block->materialize_strategy ||
           kRequestBlockInFuture == block->materialize_strategy);
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

// Returns the block request kind for a given target PC. This does some sanity
// and bounds checking.
static BlockRequestKind RequestKindForTargetPC(AppPC &target_pc,
                                               BlockRequestKind default_kind) {
  auto request_kind = kRequestBlockExecuteNatively;

  // Aagh! Indirect jump to some already cached code. For the time being,
  // give up and just go to the target and ignore the meta-data.
  if (granary_block_cache_begin <= target_pc &&
      target_pc < granary_block_cache_end) {
    granary_curiosity();  // TODO(pag): Issue #42.

  // Execution should never go to the code cache.
  } else if (granary_edge_cache_begin <= target_pc &&
             target_pc < granary_edge_cache_begin) {
    granary_unreachable("Fatal error: Trying to jump into edge cache.");

  // Target is an instrumentation-exported Granary function. These run
  // natively.
  } else if (&granary_begin_inst_exports <= target_pc &&
             target_pc < &granary_end_inst_exports) {

#ifdef GRANARY_WHERE_user
  // If we try to go to `_fini`, then redirect execution to `exit_group`.
  } else if (&_fini == target_pc) {
    target_pc = &exit_group_ok;
#endif  // GRANARY_WHERE_user

  // Execution should never target Granary itself.
  } else if (&granary_begin_text <= target_pc &&
             target_pc < &granary_end_text) {
    granary_unreachable("Fatal error: Trying to jump into "
                        "non-exported Granary function.");

  // All remaining targets should always be associated with valid module code.
  } else if (auto module = os::ModuleContainingPC(target_pc)) {
     if (os::ModuleKind::GRANARY == module->Kind()) {
       GRANARY_IF_NOT_TEST( granary_unreachable(
           "Fatal error: Trying to jump into non-exported Granary function."); )

     // Everything looks good! Take the input materialization kind.
     } else {
       request_kind = default_kind;
     }

  // Trying to translate non-executable code.
  } else {
    granary_unreachable("Fatal error: Trying to instrument non-"
                        "executable code.");
  }
  return request_kind;
}

}  // namespace

// Request a block that is the target of an indirect control-flow instruction.
// To provide maximum flexibility (e.g. allow selective going native of
// targets), we generate a dummy compensation fragment that jumps to a direct
// basic block with a default non-`kRequestBlockInFuture` materialization
// strategy.
InstrumentedBasicBlock *BlockFactory::MaterializeIndirectEntryBlock(
    BlockMetaData *meta) {
  auto start_pc = MetaDataCast<AppMetaData *>(meta)->start_pc;
  auto request_kind = RequestKindForTargetPC(
      start_pc, kRequestBlockFromIndexOrCFG);

  BasicBlock *target_block(nullptr);
  if (kRequestBlockExecuteNatively == request_kind) {
    target_block = new NativeBasicBlock(start_pc);
  } else {
    auto dest_meta = context->AllocateBlockMetaData(start_pc);
    target_block = new DirectBasicBlock(cfg, dest_meta);
    RequestBlock(target_block, request_kind);
    GRANARY_ASSERT(has_pending_request);
  }

  // Default to having a materialization strategy, and make it so that no one
  // can materialize against this block.
  auto adapt_block = AdaptToBlock(cfg, meta, target_block);
  adapt_block->is_comparable = false;
  cfg->AddEntryBlock(adapt_block);
  return adapt_block;
}

// Materialize a basic block if there is a pending request.
bool BlockFactory::MaterializeBlock(DirectBasicBlock *block) {
  if (!CanMaterializeBlock(block)) return false;

  // Make sure that code exported to instrumented application code is never
  // actually instrumented.
  auto start_pc = block->StartAppPC();
  auto request_kind = RequestKindForTargetPC(start_pc,
                                             block->materialize_strategy);

  // Don't allow us to re-materialize.
  block->materialize_strategy = kRequestBlockInFuture;

  switch (request_kind) {
    case kRequestBlockFromIndexOrCFG:
    case kRequestBlockFromIndexOrCFGOnly:
      if ((block->materialized_block = RequestIndexedBlock(&(block->meta)))) {
        break;
      }
    [[clang::fallthrough]];
    case kRequestBlockFromCFG:
      if ((block->materialized_block = MaterializeFromLCFG(block)) ||
          kRequestBlockFromIndexOrCFGOnly == request_kind) {
        break;
      }
    [[clang::fallthrough]];
    case kRequestBlockDecodeNow: {
      auto decoded_block = new DecodedBasicBlock(cfg, block->meta);
      block->meta = nullptr;  // Steal.
      block->materialized_block = decoded_block;
      DecodeInstructionList(decoded_block);
      break;
    }
    case kRequestBlockExecuteNatively: {
      auto native_block = new NativeBasicBlock(start_pc);
      delete block->meta;
      block->materialized_block = native_block;
      block->meta = nullptr;  // No longer needed.
      break;
    }
    case kRequestBlockLater:
    case kRequestBlockInFuture: break;
  }
  if (auto materialized_block = block->materialized_block) {

    // Inherit the block id.
    if (-1 == materialized_block->id) materialized_block->id = block->id;

    cfg->AddBlock(materialized_block);
    return true;
  } else {
    return false;
  }
}

// Satisfy all materialization requests.
void BlockFactory::MaterializeRequestedBlocks(void) {
  has_pending_request = false;
  cfg->first_new_block = nullptr;
  if (MaterializeDirectBlocks()) {
    RelinkCFIs();
    RemoveUnreachableBlocks();
  }
  ++cfg->generation;
}

// Returns true if there are any pending materialization requests.
bool BlockFactory::HasPendingMaterializationRequest(void) const {
  return has_pending_request;
}

// Materialize the initial basic block.
DecodedBasicBlock *BlockFactory::MaterializeDirectEntryBlock(
    BlockMetaData *meta) {
  GRANARY_ASSERT(nullptr != meta);
  auto decoded_block = new DecodedBasicBlock(cfg, meta);
  DecodeInstructionList(decoded_block);
  cfg->AddEntryBlock(decoded_block);
  has_pending_request = false;
  return decoded_block;
}

// Try to request the initial entry block from the code cache index.
InstrumentedBasicBlock *BlockFactory::RequestDirectEntryBlock(
    BlockMetaData **meta) {
  auto entry_block = RequestIndexedBlock(meta);
  if (entry_block) cfg->AddEntryBlock(entry_block);
  return entry_block;
}

// Create a new (future) basic block.
DirectBasicBlock *BlockFactory::Materialize(AppPC start_pc) {
  auto meta = context->AllocateBlockMetaData(start_pc);
  auto block = new DirectBasicBlock(cfg, meta);
  cfg->AddBlock(block);
  return block;
}

// Request that an empty basic block be created and added to the LCFG.
CompensationBasicBlock *BlockFactory::MaterializeEmptyBlock(AppPC start_pc) {
  auto meta = context->AllocateBlockMetaData(start_pc);
  auto block = new CompensationBasicBlock(cfg, meta);
  has_pending_request = true;
  cfg->AddBlock(block);
  return block;
}

}  // namespace granary
