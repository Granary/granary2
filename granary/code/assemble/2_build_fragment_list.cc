/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/new.h"

#include "granary/cfg/block.h"
#include "granary/cfg/trace.h"
#include "granary/cfg/instruction.h"

#include "granary/code/edge.h"
#include "granary/code/fragment.h"
#include "granary/code/inline_assembly.h"
#include "granary/code/assemble/2_build_fragment_list.h"

#include "granary/cache.h"
#include "granary/context.h"
#include "granary/util.h"

namespace granary {
namespace arch {

// The high-level goal of this stage of assembly is to take input basic blocks
// from a local-control-flow graph and turn them into "true" basic blocks (with
// some added restrictions one when these true blocks end) and form a control-
// flow graph.
//
// At decode time, the local control-flow graph is formed of "true" basic
// blocks. However, instrumentation tools might inject arbitrary control-flow
// into basic blocks (e.g. via inline assembly). By the time we get around to
// wanting to convert instrumented blocks into machine code, we hit a wall
// where we can't assume that control flows linearly through the instructions
// of a `DecodedBlock`, and this really complicates virtual register
// allocation (which is a prerequisite to encoding).
//
// Therefore, it's necessary to "re-split up" `DecodedBlocks` into actual
// basic blocks. However, we go further than the typical definition of a basic
// block, hence the name `Fragment`.
//
// A `Fragment` is a maximal sequence of instructions ending in an instruction
// that:
//      1)  Branches somewhere else (a control-flow instructions).
//      2)  Alters the stack pointer. This extra condition is used during
//          stage 4, to partition / color fragments. The key idea here is that
//          in kernel space, we can use the stack for allocating virtual
//          registers if the stack is "safe" (i.e. behaves like a C-style
//          call stack). An example of an "unsafe" stack is a user space stack.
//      3)  Is or is not an application / native instruction. That is, fragments
//          contain either all application or all instrumentation instructions.
//          This makes flags usage analysis, saving, and restoring easier
//          because then we can reason about the problem at the granularity of
//          fragments, and employ data flow frameworks to tackle the problem.
//      4)  Is a label instruction. Label instructions are assumed to be
//          targeted by local branch instructions, and so we eagerly split
//          fragments at label instructions based on this assumption.


// Generates the direct edge code for a given `DirectEdge` structure.
//
// Note: This function has an architecture-specific implementation.
Fragment *GenerateDirectEdgeCode(DirectEdge *edge);

// Generates some indirect edge code that is used to look up the target of an
// indirect jump.
//
// Note: This function has an architecture-specific implementation.
extern CodeFragment *GenerateIndirectEdgeCode(FragmentList *frags,
                                              IndirectEdge *edge,
                                              ControlFlowInstruction *cfi,
                                              CodeFragment *predecessor_frag,
                                              BlockMetaData *dest_block_meta);

// Generates some code to target some client function. The generated code saves
// the machine context and passes it directly to the client function for direct
// manipulation.
//
// Note: This function has an architecture-specific implementation.
extern CodeFragment *CreateContextCallFragment(Context *context,
                                               FragmentList *frags,
                                               CodeFragment *pred,
                                               AppPC func_pc);

// Generates some code to target some client function. The generated code tries
// to minimize the amount of saved/restored machine state, and punts on the
// virtual register system for the rest.
//
// Note: This function has an architecture-specific implementation.
extern void ExtendFragmentWithInlineCall(Context *context,
                                         CodeFragment *frag,
                                         InlineFunctionCall *call);

// Process an exceptional control-flow instruction.
//
// Note: `instr` already belongs to `frag`.
//
// Note: This function has an architecture-specific implementation.
extern CodeFragment *ProcessExceptionalCFI(
    FragmentList *frags, CodeFragment *frag,
    ExceptionalControlFlowInstruction *instr);

}  // namespace arch
namespace {

// Worklist item for building a fragment.
struct FragmentInProgress {
  // Next thing to process in the work list.
  FragmentInProgress *next;

  // Fragment to build.
  CodeFragment *frag;

  // Predecessor of `frag`. Helpful for debugging when there's an assertion
  // failure somewhere. In this case, we can go down to `BuildFragmentList` and
  // see what fragment led to `frag`s creation.
  GRANARY_IF_DEBUG( CodeFragment *pred_frag; )

  // First instruction to process for addition to `frag`.
  Instruction *instr;

  GRANARY_DEFINE_NEW_ALLOCATOR(FragmentInProgress, {
    kAlignment = 1
  })
};

// Builder that manages the building and connecting process for fragments.
struct FragmentBuilder {
  FragmentInProgress *next;
  FragmentList *frags;
  Trace *cfg;
  Context *context;
};

// Enqueue a new fragment to be created to the work list. This fragment
// represents the tail of a basic block.
static void AddBlockTailToWorkList(
    FragmentBuilder *builder, CodeFragment *predecessor,
    LabelInstruction *label, Instruction *first_instr,
    FragmentSuccessorSelector succ_sel=kFragSuccFallThrough) {
  Fragment *tail_frag(nullptr);

  // Already added to work list.
  if (label && label->fragment) {
    tail_frag = label->fragment;

  // Not already processed / part of the work list.
  } else {
    auto frag = new CodeFragment;
    frag->block_meta = predecessor->block_meta;

    auto elm = new FragmentInProgress;
    elm->frag = frag;
    GRANARY_IF_DEBUG( elm->pred_frag = predecessor; )
    elm->instr = first_instr;
    elm->next = builder->next;

    builder->next = elm;  // To head of work list.
    builder->frags->InsertAfter(predecessor, frag);  // Depth-first order.

    if (label) {
      frag->entry_label = label;
      label->fragment = frag;  // Cache for branches / fall-throughs.
    }

    tail_frag = frag;
  }

  // Add it to the fragment control-flow graph.
  GRANARY_ASSERT(!predecessor->successors[succ_sel]);
  GRANARY_ASSERT(tail_frag->list.IsLinked());
  predecessor->successors[succ_sel] = tail_frag;
}

// Unreachable but referenced label. Most likely we have another mechanism of
// reaching this label that isn't communicated by means of the normal control-
// flow instructions. For example, the function wrapper tool will sometimes
// want to pass a pointer to an instrumented version of the function being
// wrapped.
static void AddBlockStragglerToWorkList(FragmentBuilder *builder,
                                        BlockMetaData *source_block_meta,
                                        LabelInstruction *label) {
  // We have a distinguished non-local entry fragment here because we don't
  // want to allow our labels to get lost inside partition entry/flag entry
  // fragments and allow control to jump into weird places.
  auto frag = new NonLocalEntryFragment;
  frag->cache = kCodeCacheKindFrozen;
  frag->entry_label = label;
  label->fragment = frag;

  auto cfrag = new CodeFragment;
  cfrag->block_meta = source_block_meta;

  frag->successors[kFragSuccFallThrough] = cfrag;

  auto elm = new FragmentInProgress;
  elm->frag = cfrag;
  elm->instr = label->Next();
  elm->next = builder->next;

  builder->next = elm;  // To head of work list.
  builder->frags->Append(frag);  // Add to the end so it's not in-line.
  builder->frags->Append(cfrag);  // Add to the end so it's not in-line.
}

// Process an annotation instruction. Returns `true` if iteration should
// continue, and `false` otherwise.
static bool ProcessAnnotation(FragmentBuilder *builder, CodeFragment *frag,
                              AnnotationInstruction *instr) {
  auto next_instr = instr->Next();
  switch (instr->annotation) {
    case kAnnotEndBlock: granary_curiosity(); return false;

    case kAnnotationCodeCacheKind:
      frag->cache = instr->Data<CodeCacheKind>();
      return true;

    // Should not have an `AnnotationInstruction` with `kAnnotLabel` that is
    // not also a `LabelInstruction`.
    case kAnnotationLabel: GRANARY_ASSERT(false); return true;

    // The stack pointer is changed by an indeterminate amount, e.g. replaced
    // by the value stored in a register, or displaced by the value stored in
    // a register.
    case kAnnotInvalidStack:
      frag->stack_status = kStackStatusInvalid;
      return true;

    // An annotation where, when encoded, will update a pointer to contain the
    // address at which this annotation is encoded.
    case kAnnotUpdateAddressWhenEncoded:
      frag->instrs.Append(Instruction::Unlink(instr).release());
      return true;

    // The upcoming instruction can potentially enabled/disable interrupts.
    //
    // Note: We'll assume that for such instructions, the stack is guaranteed
    //       to be valid.
    case kAnnotInterruptDeliveryStateChange:
      frag->attr.can_add_succ_to_partition = false;
      AddBlockTailToWorkList(builder, frag, nullptr, next_instr);
      return false;

    // Calls out to some client code. This creates a new fragment that cannot
    // be added to any existing partition.
    case kAnnotContextFunctionCall: {
      auto context_frag = arch::CreateContextCallFragment(
          builder->context, builder->frags, frag, instr->Data<AppPC>());
      AddBlockTailToWorkList(builder, context_frag, nullptr, next_instr);
      return false;
    }

    // Calls out to some client code, but the call has access to the existing
    // virtual register state.
    case kAnnotInlineFunctionCall: {
      auto call = instr->Data<InlineFunctionCall *>();
      arch::ExtendFragmentWithInlineCall(builder->context, frag, call);
      delete call;
      instr->SetData(0UL);
      return true;
    }

    // Used to hint at late stack switching.
    case kAnnotCondLeaveNativeStack:
    case kAnnotCondEnterNativeStack:
      frag->instrs.Append(Instruction::Unlink(instr).release());
      return true;

    default: return true;
  }
}

// Process a branch instruction.
static void ProcessBranch(FragmentBuilder *builder, CodeFragment *frag,
                          BranchInstruction *instr) {
  auto target_label = instr->TargetLabel();

  // Makes the fragment into an application fragment; if the current fragment
  // is an instrumentation fragment then we need to split the fragment for
  // the branch.
  if (instr->IsAppInstruction() &&
      (instr->IsConditionalJump() ||
       instr->instruction.WritesToStackPointer())) {
    if (kFragmentKindInst == frag->kind) {
      auto frag_with_branch = new CodeFragment;
      frag_with_branch->block_meta = frag->block_meta;
      frag->successors[kFragSuccFallThrough] = frag_with_branch;
      builder->frags->InsertAfter(frag, frag_with_branch);
      frag = frag_with_branch;
    }
    frag->kind = kFragmentKindApp;
  }

  frag->branch_instr = instr;
  frag->attr.branch_is_function_call = instr->IsFunctionCall();
  frag->attr.has_native_instrs = true;  // The branch.

  // Add the branch target.
  AddBlockTailToWorkList(builder, frag, target_label, target_label->Next(),
                         kFragSuccBranch);

  // Handle the fall-through.
  if (instr->IsFunctionCall() || instr->IsConditionalJump()) {
    auto next_instr = instr->Next();
    auto fall_through_label = DynamicCast<LabelInstruction *>(next_instr);
    if (fall_through_label) {
      fall_through_label->DataRef<uintptr_t>() += 1; // Hold a refcount.
      next_instr = fall_through_label->Next();
    }
    AddBlockTailToWorkList(builder, frag, fall_through_label, next_instr);
  }
  // Append the branch to the fragment.
  frag->instrs.Append(Instruction::Unlink(instr).release());
}

// Process a native instruction. Returns `true` if the instruction is added
// to the fragment, and false if the instruction splits the fragment.
static bool ProcessNativeInstr(FragmentBuilder *builder, CodeFragment *frag,
                               NativeInstruction *instr);

// Process an exceptional control-flow instruction.
static void ProcessExceptionalCFI(FragmentBuilder *builder, CodeFragment *frag,
                                  ExceptionalControlFlowInstruction *instr) {
  auto next_instr = instr->Next();
  if (!ProcessNativeInstr(builder, frag, instr)) {
    auto elm = builder->next;
    builder->next = elm->next;
    frag = elm->frag;
    GRANARY_ASSERT(instr == elm->instr);
    delete elm;
    GRANARY_IF_DEBUG(auto ret = ) ProcessNativeInstr(builder, frag, instr);
    GRANARY_ASSERT(ret);
  }
  frag = arch::ProcessExceptionalCFI(builder->frags, frag, instr);
  AddBlockTailToWorkList(builder, frag, nullptr, next_instr);
}

// Add an indirect edge. This is used for specialized returns (which by now
// should have been converted into indirect jumps, indirect jumps, and indirect
// function calls. This is *not* used for syscalls/sysreturns/HTM aborts.
static CodeFragment *AddIndirectEdge(FragmentBuilder *builder,
                                     CodeFragment *pred_frag,
                                     ControlFlowInstruction *cfi) {
  auto target_block = cfi->TargetBlock();
  auto inst_target = DynamicCast<InstrumentedBlock *>(target_block);
  auto target_meta = inst_target->UnsafeMetaData();
  auto edge = builder->context->AllocateIndirectEdge(
      pred_frag->block_meta, target_meta);
  auto frag = arch::GenerateIndirectEdgeCode(builder->frags, edge, cfi,
                                             pred_frag, target_meta);
  return frag;
}

static void ProcessJump(FragmentBuilder *builder, CodeFragment *pred_frag,
                        ControlFlowInstruction *cfi) {
  auto target_block = cfi->TargetBlock();
  auto target_frag = target_block->fragment;

  // Indirect jump.
  if (cfi->HasIndirectTarget()) {
    GRANARY_ASSERT(!target_frag);
    target_frag = AddIndirectEdge(builder, pred_frag, cfi);

  // Direct jump.
  } else {
    GRANARY_ASSERT(!cfi->HasIndirectTarget());
    GRANARY_ASSERT(nullptr != target_frag);
    pred_frag->attr.can_add_succ_to_partition =
        pred_frag->block_meta == target_frag->block_meta;
  }
  pred_frag->successors[kFragSuccFallThrough] = target_frag;
}

static CodeFragment *MakeCFIFrag(FragmentBuilder *builder,
                                 CodeFragment *pred_frag,
                                 ControlFlowInstruction *cfi) {
  auto frag = new CodeFragment;
  frag->kind = kFragmentKindApp;  // Force it to application code.
  frag->branch_instr = cfi;
  frag->block_meta = pred_frag->block_meta;

  frag->attr.has_native_instrs = true;
  frag->attr.branch_is_function_call = cfi->IsFunctionCall();
  frag->attr.can_add_succ_to_partition = false;
  frag->attr.can_add_pred_to_partition = false;
  pred_frag->attr.can_add_succ_to_partition = false;

  // Chain it in.
  pred_frag->successors[kFragSuccFallThrough] = frag;
  builder->frags->InsertAfter(pred_frag, frag);

  // Add in the CFI.
  frag->instrs.Append(Instruction::Unlink(cfi).release());
  return frag;
}

static void ProcessTwoWayCFITail(FragmentBuilder *builder,
                                 CodeFragment *pred_frag,
                                 Instruction *next_instr) {
  auto cfi = DynamicCast<ControlFlowInstruction *>(next_instr);
  if (cfi && cfi->IsJump() && cfi->IsUnconditionalJump()) {
    ProcessJump(builder, pred_frag, cfi);
  } else {
    AddBlockTailToWorkList(builder, pred_frag, nullptr, next_instr);
  }
}

// Process a control-flow instruction.
static void ProcessCFI(FragmentBuilder *builder, CodeFragment *pred_frag,
                       ControlFlowInstruction *cfi) {
  auto target_block = cfi->TargetBlock();
  auto target_frag = target_block->fragment;
  auto next_instr = cfi->Next();

  if (cfi->IsJump() && cfi->IsUnconditionalJump()) {
    ProcessJump(builder, pred_frag, cfi);

  } else if (cfi->IsFunctionCall()) {
    auto frag = MakeCFIFrag(builder, pred_frag, cfi);
    if (cfi->HasIndirectTarget()) {
      GRANARY_ASSERT(!target_frag);
      frag->attr.can_add_pred_to_partition = true;  // For target reg/mem!
      pred_frag->partition.Union(frag->partition);
      target_frag = AddIndirectEdge(builder, frag, cfi);
    }
    GRANARY_ASSERT(nullptr != target_frag);
    frag->successors[kFragSuccBranch] = target_frag;
    ProcessTwoWayCFITail(builder, frag, next_instr);

  } else if (cfi->IsConditionalJump()) {
    GRANARY_ASSERT(nullptr != target_frag);
    auto frag = MakeCFIFrag(builder, pred_frag, cfi);
    frag->successors[kFragSuccBranch] = target_frag;
    ProcessTwoWayCFITail(builder, frag, next_instr);

  } else if (cfi->IsSystemCall() || cfi->IsInterruptCall()) {
    GRANARY_ASSERT(!target_frag);
    auto frag = new CodeFragment;
    frag->attr.can_add_succ_to_partition = false;
    frag->attr.can_add_pred_to_partition = false;
    frag->attr.has_native_instrs = true;
    frag->instrs.Append(Instruction::Unlink(cfi).release());

    pred_frag->successors[kFragSuccFallThrough] = frag;
    builder->frags->InsertAfter(pred_frag, frag);
    ProcessTwoWayCFITail(builder, frag, next_instr);

  } else if (cfi->IsSystemReturn() || cfi->IsInterruptReturn() ||
             cfi->IsFunctionReturn()) {
    GRANARY_ASSERT(!target_frag);
    auto frag = new ExitFragment;
    frag->instrs.Append(Instruction::Unlink(cfi).release());
    pred_frag->successors[kFragSuccFallThrough] = frag;

  } else {
    GRANARY_ASSERT(false);
  }
}

// Process a native instruction. Returns `true` if the instruction is added
// to the fragment, and false if the instruction splits the fragment.
static bool ProcessNativeInstr(FragmentBuilder *builder, CodeFragment *frag,
                               NativeInstruction *instr) {
  auto is_app = instr->IsAppInstruction();
  auto reads_flags = instr->ReadsConditionCodes();
  auto writes_flags = instr->WritesConditionCodes();
  auto writes_stack_ptr = instr->instruction.WritesToStackPointer();

  if (kFragmentKindInvalid == frag->kind) {
    frag->kind = is_app ? kFragmentKindApp : kFragmentKindInst;

  // Instrumentation instructions in an application fragment are allowed to
  // read but not write the flags.
  } else if (kFragmentKindApp == frag->kind && !is_app && writes_flags) {
    AddBlockTailToWorkList(builder, frag, nullptr, instr);
    return false;

  // Application instructions in an instrumentation fragment are not allowed
  // to read or write the flags, or to change the stack pointer.
  } else if (kFragmentKindInst == frag->kind && is_app &&
             (reads_flags || writes_flags || writes_stack_ptr)) {
    AddBlockTailToWorkList(builder, frag, nullptr, instr);
    return false;
  }

  // We're appending the instruction.
  if (reads_flags) frag->attr.reads_flags = true;
  if (writes_flags) frag->attr.modifies_flags = true;
  frag->attr.has_native_instrs = true;
  frag->instrs.Append(Instruction::Unlink(instr).release());
  return true;
}

// Process a label instruction. Returns `true` if the label is skipped, and
// false if the label splits the fragment.
static bool ProcessLabel(FragmentBuilder *builder, CodeFragment *frag,
                         LabelInstruction *label, Instruction *next_instr) {
  if (!label->data) return true;  // Skip it.

  // TODO(pag): Temporary stop-gap to handle the problem of two partition
  //            entrypoints being added when trying to jump around a syscall.
  if (!frag->attr.has_native_instrs &&
      !frag->attr.can_add_pred_to_partition) {
    frag->attr.can_add_succ_to_partition = false;
  }
  AddBlockTailToWorkList(builder, frag, label, next_instr);
  return false;
}

// Process a fragment that just came from the head of the work list. This
// involves iteration through the instruction list beginning at `instr` and
// deciding which instructions to put into `frag`, and when to stop building
// `frag` and enqueue new items to the work list.
static void ProcessFragment(FragmentBuilder *builder, CodeFragment *frag,
                            Instruction *instr) {
  Instruction *next_instr(nullptr);
  for (; instr; instr = next_instr) {
    GRANARY_ASSERT(!frag->successors[kFragSuccFallThrough]);
    next_instr = instr->Next();

    // Blocks are split up by labels, but only if labels are targeted by
    // branches. The `data` field of the label counts the number of incoming
    // branches.
    if (auto label_instr = DynamicCast<LabelInstruction *>(instr)) {
      if (!ProcessLabel(builder, frag, label_instr, next_instr)) return;

    // Annotation instructions either introduce fragment splits, modify fragment
    // attributes, or are ignored.
    } else if (auto annot_instr = DynamicCast<AnnotationInstruction *>(instr)) {
      if (!ProcessAnnotation(builder, frag, annot_instr)) return;

    // Found a local branch; add in the fall-through and/or the branch
    // target.
    } else if (auto branch_instr = DynamicCast<BranchInstruction *>(instr)) {
      ProcessBranch(builder, frag, branch_instr);
      return;

    // Exceptional control-flow instruction.
    } else if (auto exc = DynamicCast<ExceptionalControlFlowInstruction *>(
                   instr)) {
      ProcessExceptionalCFI(builder, frag, exc);
      return;

    // Found a control-flow instruction.
    } else if (auto flow_instr = DynamicCast<ControlFlowInstruction *>(instr)) {
      ProcessCFI(builder, frag, flow_instr);
      return;

    } else if (auto native_instr = DynamicCast<NativeInstruction *>(instr)) {
      if (!ProcessNativeInstr(builder, frag, native_instr)) return;

    } else {
      granary_curiosity();
    }
  }
}

// Run ahead to see if there's anything that might be a useful instruction to
// add to the the fragment graph.
static bool HasUsefulInstruction(Instruction *instr_) {
  for (auto instr : InstructionListIterator(instr_)) {
    if (IsA<NativeInstruction *>(instr)) return true;
  }
  return false;
}

// Look for remaining, potentially reachable code in the trace, and add it in.
static void AddStragglerFragments(FragmentBuilder *builder) {
  for (auto block : builder->cfg->ReverseBlocks()) {
    auto decoded_block = DynamicCast<DecodedBlock *>(block);
    if (!decoded_block) continue;

    for (auto instr : decoded_block->Instructions()) {
      auto label = DynamicCast<LabelInstruction *>(instr);
      if (!label) continue;
      if (!label->data) continue;
      if (label->fragment) continue;  // Already seen.
      if (!HasUsefulInstruction(label->Next())) continue;
      AddBlockStragglerToWorkList(builder, decoded_block->MetaData(), label);
      break;
    }
  }
}

// Adds a decoded basic block to the fragment work list as an empty
// `CodeFragment`.
static void AddDecodedBlockToWorkList(FragmentBuilder *builder,
                                      DecodedBlock *block) {
  auto frag = new CodeFragment;
  frag->block_meta = block->MetaData();
  frag->attr.is_block_head = true;

  if (block->IsColdCode()) frag->cache = kCodeCacheKindCold;

  auto elm = new FragmentInProgress;
  elm->frag = frag;
  GRANARY_IF_DEBUG( elm->pred_frag = nullptr; )
  elm->instr = block->FirstInstruction()->Next();
  elm->next = builder->next;

  builder->next = elm;  // To head of work list.
  block->fragment = frag;
  builder->frags->Prepend(frag);  // To head of fragment list.
}

// Adds a direct edge to the end of the fragment list as an `ExitFragment`.
static void AddDirectBlockToFragList(FragmentBuilder *builder,
                                     DirectBlock *block) {
  auto meta = block->MetaData();
  auto edge = builder->context->AllocateDirectEdge(meta);
  auto frag = arch::GenerateDirectEdgeCode(edge);
  block->fragment = frag;
  builder->frags->Append(frag);  // To tail of fragment list.
}

// Adds a cached basic block to the end of the fragment list as an
// `ExitFragment`.
static void AddCachedBlockToFragList(FragmentBuilder *builder,
                                     CachedBlock *block) {
  auto frag = new ExitFragment;
  frag->encoded_pc = block->StartCachePC();
  frag->cache = kCodeCacheKindEdge;
  frag->encoded_size = 0;
  frag->block_meta = block->MetaData();

  GRANARY_ASSERT(nullptr != frag->encoded_pc);

  block->fragment = frag;
  builder->frags->Append(frag);  // To tail of fragment list.
}

// Adds a native basic block to the end of the fragment list as an
// `ExitFragment`.
static void AddNativeBlockToFragList(FragmentBuilder *builder,
                                     Block *block,
                                     AppPC start_pc) {
  if (!start_pc) return;  // Syscalls, interrupt calls, returns, etc.

  auto frag = new ExitFragment;
  frag->cache = kCodeCacheKindEdge;
  frag->encoded_pc = UnsafeCast<CachePC>(start_pc);
  frag->encoded_size = 0;
  frag->block_meta = nullptr;

  block->fragment = frag;
  builder->frags->Append(frag);  // To tail of fragment list.
}

// Adds a block to the builder's work list.
//
// Note: Blocks are added in reverse order so that the first block ends up
//       being the first one processed by the builder.
//
// Note: This arranges for all `ExitFragment`s to be located at the end of
//       the fragment list.
static void InitBlockFragment(FragmentBuilder *builder, Block *block) {
  if (auto direct_block = DynamicCast<DirectBlock *>(block)) {
    AddDirectBlockToFragList(builder, direct_block);
  } else if (auto cached_block = DynamicCast<CachedBlock *>(block)) {
    AddCachedBlockToFragList(builder, cached_block);
  } else if (auto native_block = DynamicCast<NativeBlock *>(block)) {
    AddNativeBlockToFragList(builder, native_block, native_block->StartAppPC());
  } else if (auto decoded_block = DynamicCast<DecodedBlock *>(block)) {
    AddDecodedBlockToWorkList(builder, decoded_block);
  }

  // Ignore `ReturnBlock`s and `IndirectBlock`s.
}

// Initialize the work list for each basic block.
static void InitializeFragAndWorklist(FragmentBuilder *builder) {
  for (auto block : builder->cfg->ReverseBlocks()) {
    InitBlockFragment(builder, block);
  }
}

}  // namespace

// Build a fragment list out of a set of basic blocks.
void BuildFragmentList(Context *context, Trace *cfg,
                       FragmentList *frags) {
  FragmentBuilder builder = {
    nullptr,
    frags,
    cfg,
    context
  };
  InitializeFragAndWorklist(&builder);
  while (auto item = builder.next) {
    builder.next = item->next;
    auto frag = item->frag;
    GRANARY_IF_DEBUG( auto pred = item->pred_frag; )
    auto instr = item->instr;
    delete item;
    ProcessFragment(&builder, frag, instr);
    GRANARY_IF_DEBUG(GRANARY_USED(pred));

    if (!builder.next) {
      AddStragglerFragments(&builder);
    }
  }
}

}  // namespace granary
