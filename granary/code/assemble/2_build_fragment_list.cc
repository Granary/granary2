/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/new.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/edge.h"
#include "granary/code/fragment.h"
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
// blocks. However, instrumentation tools might inject abitrary control-flow
// into basic blocks (e.g. via inline assembly). By the time we get around to
// wanting to convert instrumented blocks into machine code, we hit a wall
// where we can't assume that control flows linearly through the instructions
// of a `DecodedBasicBlock`, and this really complicates virtual register
// allocation (which is a pre-requisite to encoding).
//
// Therefore, it's necessary to "re-split up" `DecodedBasicBlocks` into actual
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
extern CodeFragment *CreateContextCallFragment(ContextInterface *context,
                                               FragmentList *frags,
                                               CodeFragment *pred,
                                               AppPC func_pc);

// Generates some code to target some client function. The generated code tries
// to minimize the amount of saved/restored machine state, and punts on the
// virtual register system for the rest.
extern void ExtendFragmentWithOutlineCall(ContextInterface *context,
                                          CodeFragment *frag,
                                          InlineFunctionCall *call);

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
    SHARED = false,
    ALIGNMENT = 1
  })
};

// Builder that manages the building and connecting process for fragments.
struct FragmentBuilder {
  FragmentInProgress *next;
  FragmentList *frags;
  LocalControlFlowGraph *cfg;
  ContextInterface *context;
};

// Enqueue a new fragment to be created to the work list. This fragment
// represents the tail of a basic block.
static void AddBlockTailToWorkList(
    FragmentBuilder *builder, CodeFragment *predecessor,
    LabelInstruction *label, Instruction *first_instr, StackUsageInfo stack,
    FragmentSuccessorSelector succ_sel=FRAG_SUCC_FALL_THROUGH) {
  Fragment *tail_frag(nullptr);

  // Already added to work list.
  if (label && label->fragment) {
    tail_frag = label->fragment;

  // Not already processed / part of the work list.
  } else {
    auto frag = new CodeFragment;
    frag->attr.block_meta = predecessor->attr.block_meta;
    frag->stack = stack;

    auto elm = new FragmentInProgress;
    elm->frag = frag;
    GRANARY_IF_DEBUG( elm->pred_frag = predecessor; )
    elm->instr = first_instr;
    elm->next = builder->next;

    builder->next = elm;  // To head of work list.
    builder->frags->InsertAfter(predecessor, frag);  // Depth-first order.

    if (label) label->fragment = frag;  // Cache for branches / fall-throughs.

    tail_frag = frag;
  }

  if (auto code_tail_frag = DynamicCast<CodeFragment *>(tail_frag)) {
    code_tail_frag->attr.num_predecessors += 1;

    // Propagate the "follows a CFI" condition. This is used later when
    // partitioning to make sure that code following a function call or system
    // call is not placed in the same partition as code that jumps around the
    // function or system call.
    if (FRAG_SUCC_FALL_THROUGH == succ_sel && predecessor->attr.follows_cfi) {
      code_tail_frag->attr.follows_cfi = true;
    }
  }

  // Add it to the fragment control-flow graph.
  GRANARY_ASSERT(!predecessor->successors[succ_sel]);
  predecessor->successors[succ_sel] = tail_frag;
}

// Process an annotation instruction. Returns `true` if iteration should
// continue, and `false` otherwise.
static bool ProcessAnnotation(FragmentBuilder *builder, CodeFragment *frag,
                              AnnotationInstruction *instr) {
  auto next_instr = instr->Next();
  switch (instr->annotation) {
    case IA_END_BASIC_BLOCK: granary_curiosity(); return false;

    // Should not have an `AnnotationInstruction` with `IA_LABEL` that is not
    // also a `LabelInstruction`.
    case IA_LABEL: GRANARY_ASSERT(false); return true;

    // An upcoming instruction makes this stack valid.
    case IA_VALID_STACK:
      if (STACK_INVALID == frag->stack.status) {
        AddBlockTailToWorkList(builder, frag, nullptr, next_instr,
                               StackUsageInfo(STACK_VALID));
        return false;
      } else {
        frag->stack.status = STACK_VALID;
        return true;
      }

    // The stack pointer is changed by an indeterminate amount, e.g. replaced
    // by the value stored in a register, or displaced by the value stored in
    // a register.
    case IA_INVALID_STACK:
      if (STACK_VALID == frag->stack.status || frag->attr.has_native_instrs) {
        frag->attr.can_add_succ_to_partition = false;
        AddBlockTailToWorkList(builder, frag, nullptr, next_instr,
                               StackUsageInfo(STACK_INVALID));
        return false;
      } else {
        frag->stack.status = STACK_INVALID;
        return true;
      }

    // This annotation is added by the block factory. It enables us to be a bit
    // more aggressive with fragment splitting, where if we have some code that
    // operates on an invalid stack, then we will assume it is localized, and
    // that execution will return to a valid stack soon. Therefore, we want to
    // arrange for the fragment following the current fragment (whose stack
    // should be invalid) to potentially have the opportunity to be marked as
    // valid. For example:
    //          <IA_INVALID_STACK> -----------.
    //          MOV RSP, [X]  <-- caused by --+
    //          <IA_UNKNOWN_STACK_ABOVE> -----'
    //          MOV Y, [Z]
    //          POP [Y]
    // Then we'll split that into two fragments:
    //      1:  MOV RSP, [X]
    //          ------------
    //      2:  MOV Y, [Z]
    //          POP [Y]
    // Where the `MOV Y, [Z]` is grouped with the `POP` and so isn't penalized
    // by the stack undefinedness of the `MOV RSP, [X]`.
    case IA_UNKNOWN_STACK_ABOVE:
      frag->attr.can_add_succ_to_partition = false;
      frag->stack.status = STACK_INVALID;
      AddBlockTailToWorkList(builder, frag, nullptr, next_instr,
                             StackUsageInfo(STACK_STATUS_INHERIT_SUCC));
      return false;

    // Here we've got something like:
    //          <IA_VALID_STACK> -------.
    //          PUSH RBP <--- cause by -'
    //          <IA_UNKNOWN_STACK_BELOW> ------.
    //          MOV RBP, RSP   <-- caused by --'
    //          MOV [RBP - 8], RDI   <-- accesses redzone (below RSP).
    case IA_UNKNOWN_STACK_BELOW:
      frag->stack.inherit_constraint = STACK_STATUS_INHERIT_PRED;
      AddBlockTailToWorkList(builder, frag, nullptr, next_instr,
                             StackUsageInfo(STACK_STATUS_INHERIT_SUCC));
      return false;

    // Function return address. Used when mangling indirect function calls.
    case IA_RETURN_ADDRESS:
      GRANARY_ASSERT(!frag->attr.has_native_instrs);
      frag->attr.is_return_target = true;
      frag->instrs.Append(instr->UnsafeUnlink().release());
      return true;

    // An annotation where, when encoded, will update a pointer to contain the
    // address at which this annotation is encoded.
    case IA_UPDATE_ENCODED_ADDRESS:
      frag->instrs.Append(instr->UnsafeUnlink().release());
      return true;

    // The upcoming instruction can potentially enabled/disable interrupts.
    //
    // Note: We'll assume that for such instructions, the stack is guaranteed
    //       to be valid.
    case IA_CHANGES_INTERRUPT_STATE:
      frag->attr.can_add_succ_to_partition = false;
      AddBlockTailToWorkList(builder, frag, nullptr, next_instr,
                             StackUsageInfo(GRANARY_IF_KERNEL(STACK_VALID)));
      return false;

    // Calls out to some client code. This creates a new fragment that cannot
    // be added to any existing partition.
    case IA_CONTEXT_CALL: {
      auto context_frag = arch::CreateContextCallFragment(
          builder->context, builder->frags, frag, instr->Data<AppPC>());
      AddBlockTailToWorkList(builder, context_frag, nullptr, next_instr,
                             StackUsageInfo());
      return false;
    }

    // Calls out to some client code, but the call has access to the existing
    // virtual register state.
    case IA_OUTLINE_CALL:
      arch::ExtendFragmentWithOutlineCall(builder->context, frag,
                                          instr->Data<InlineFunctionCall *>());
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
    if (CODE_TYPE_INST == frag->type) {
      auto frag_with_branch = new CodeFragment;
      frag_with_branch->attr.block_meta = frag->attr.block_meta;
      frag->successors[FRAG_SUCC_FALL_THROUGH] = frag_with_branch;
      builder->frags->InsertAfter(frag, frag_with_branch);
      frag = frag_with_branch;
    }
    frag->type = CODE_TYPE_APP;
  }

  frag->branch_instr = instr;
  frag->attr.branch_is_function_call = instr->IsFunctionCall();
  frag->attr.branch_is_indirect = instr->HasIndirectTarget();
  frag->attr.branch_is_jump = instr->IsJump();
  frag->attr.has_native_instrs = true;  // The branch.

  // Add the branch target.
  AddBlockTailToWorkList(builder, frag, target_label, target_label->Next(),
                         StackUsageInfo(), FRAG_SUCC_BRANCH);

  // Handle the fall-through.
  if (instr->IsFunctionCall() || instr->IsConditionalJump()) {
    auto next_instr = instr->Next();
    auto fall_through_label = DynamicCast<LabelInstruction *>(next_instr);
    if (fall_through_label) {
      fall_through_label->data += 1; // Hold a refcount.
      next_instr = fall_through_label->Next();
    }
    AddBlockTailToWorkList(builder, frag, fall_through_label,
                           next_instr, StackUsageInfo());
  }
  // Append the branch to the fragment.
  frag->instrs.Append(instr->UnsafeUnlink().release());
}

// Process a control-flow instruction.
static void ProcessCFI(FragmentBuilder *builder, CodeFragment *frag,
                       ControlFlowInstruction *instr) {

  auto target_block = instr->TargetBlock();
  auto target_frag = target_block->fragment;

  auto pred_frag = frag;
  frag = new CodeFragment;

  pred_frag->successors[FRAG_SUCC_FALL_THROUGH] = frag;
  pred_frag->attr.can_add_succ_to_partition = false;

  builder->frags->InsertAfter(pred_frag, frag);

  frag->type = CODE_TYPE_APP;  // Force it to application code.
  frag->branch_instr = instr;
  frag->attr.block_meta = pred_frag->attr.block_meta;
  frag->attr.has_native_instrs = true;
  frag->attr.branch_is_function_call = instr->IsFunctionCall();
  frag->attr.branch_is_indirect = instr->HasIndirectTarget();
  frag->attr.branch_is_jump = instr->IsJump();
  frag->attr.can_add_succ_to_partition = false;

  // Update stack validity.
  if (instr->IsFunctionCall() || instr->IsFunctionReturn() ||
      instr->IsInterruptReturn()
      GRANARY_IF_KERNEL( || instr->IsInterruptCall() )) {
    frag->stack.status = STACK_VALID;
  }

  // Specialized return, indirect call/jump.
  if (!target_frag) {
    GRANARY_ASSERT(frag->attr.branch_is_indirect);
    GRANARY_ASSERT(IsA<ReturnBasicBlock *>(target_block) ||
                   IsA<IndirectBasicBlock *>(target_block));
    auto inst_target = DynamicCast<InstrumentedBasicBlock *>(target_block);
    auto target_meta = inst_target->UnsafeMetaData();
    auto edge = builder->context->AllocateIndirectEdge(target_meta);

    target_frag = arch::GenerateIndirectEdgeCode(builder->frags, edge, instr,
                                                 frag, target_meta);
    target_block->fragment = target_frag;

    // Force the predecessor to be in the same partition, because the
    // predecessor likely defines the virtual register that contains the
    // target of this CFI.
    if (pred_frag) frag->partition.Union(frag, pred_frag);

    // We force the in-edge code to be in the same partition. At the same time,
    // we have `attr.can_add_succ_to_partition == false`, so that we don't add
    // fall-throughs into the same partition.
    frag->partition.Union(reinterpret_cast<Fragment *>(frag), target_frag);

  // Something going to native/cached/direct edge code.
  } else if (IsA<ExitFragment *>(target_frag)) {
    frag->attr.branches_to_code = true;

  // Going to a decoded basic block.
  } else {
    GRANARY_ASSERT(IsA<CodeFragment *>(target_frag));
    GRANARY_ASSERT(IsA<DecodedBasicBlock *>(target_block));
    frag->attr.can_add_succ_to_partition = false;
  }

  frag->successors[FRAG_SUCC_BRANCH] = target_frag;

  // Add in a fall-through successor.
  if (instr->IsFunctionCall() || instr->IsConditionalJump() ||
      instr->IsSystemCall() || instr->IsInterruptCall()) {
    AddBlockTailToWorkList(builder, frag, nullptr, instr->Next(), frag->stack);

    auto fall_through_frag = DynamicCast<CodeFragment *>(
        frag->successors[FRAG_SUCC_FALL_THROUGH]);

    fall_through_frag->attr.can_add_pred_to_partition = false;
    fall_through_frag->attr.follows_cfi = true;
  }

  // Add in the CFI.
  frag->instrs.Append(instr->UnsafeUnlink().release());
}

// Process a native instruction. Returns `true` if the instruction is added
// to the fragment, and false if the instruction splits the fragment.
static bool ProcessNativeInstr(FragmentBuilder *builder, CodeFragment *frag,
                               NativeInstruction *instr) {
  auto is_app = instr->IsAppInstruction();
  auto reads_flags = instr->ReadsConditionCodes();
  auto writes_flags = instr->WritesConditionCodes();
  auto writes_stack_ptr = instr->instruction.WritesToStackPointer();

  if (CODE_TYPE_UNKNOWN == frag->type) {
    frag->type = is_app ? CODE_TYPE_APP : CODE_TYPE_INST;

  // Instrumentation instructions in an application fragment are allowed to
  // read but not write the flags.
  } else if (CODE_TYPE_APP == frag->type && !is_app && writes_flags) {
    AddBlockTailToWorkList(builder, frag, nullptr, instr, frag->stack);
    return false;

  // Application instructions in an instrumentation fragment are not allowed
  // to read or write the flags, or to change the stack pointer.
  } else if (CODE_TYPE_INST == frag->type && is_app &&
             (reads_flags || writes_flags || writes_stack_ptr)) {
    AddBlockTailToWorkList(builder, frag, nullptr, instr, frag->stack);
    return false;
  }

  // We're appending the instruction.
  if (reads_flags) frag->attr.reads_flags = true;
  if (writes_flags) frag->attr.modifies_flags = true;
  frag->attr.has_native_instrs = true;
  frag->instrs.Append(instr->UnsafeUnlink().release());
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

  AddBlockTailToWorkList(builder, frag, label, next_instr, StackUsageInfo());
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
    GRANARY_ASSERT(!frag->successors[FRAG_SUCC_FALL_THROUGH]);
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

// Adds a decoded basic block to the fragment work list as an empty
// `CodeFragment`.
static void AddDecodedBlockToWorkList(FragmentBuilder *builder,
                                      DecodedBasicBlock *block) {
  auto frag = new CodeFragment;
  frag->attr.block_meta = block->MetaData();
  frag->attr.is_block_head = true;

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
                                     DirectBasicBlock *block) {
  auto meta = block->MetaData();
  auto frag = new ExitFragment(FRAG_EXIT_FUTURE_BLOCK_DIRECT);
  auto edge = builder->context->AllocateDirectEdge(meta);

  frag->encoded_pc = edge->edge_code;
  frag->block_meta = meta;
  frag->edge.kind = EDGE_KIND_DIRECT;
  frag->edge.direct = edge;

  GRANARY_ASSERT(nullptr != frag->encoded_pc);

  block->fragment = frag;
  builder->frags->Append(frag);  // To tail of fragment list.
}

// Adds a cached basic block to the end of the fragment list as an
// `ExitFragment`.
static void AddCachedBlockToFragList(FragmentBuilder *builder,
                                     CachedBasicBlock *block) {
  auto frag = new ExitFragment(FRAG_EXIT_EXISTING_BLOCK);
  frag->encoded_pc = block->StartCachePC();
  frag->encoded_size = 0;
  frag->block_meta = block->MetaData();

  GRANARY_ASSERT(nullptr != frag->encoded_pc);

  block->fragment = frag;
  builder->frags->Append(frag);  // To tail of fragment list.
}

// Adds a native basic block to the end of the fragment list as an
// `ExitFragment`.
static void AddNativeBlockToFragList(FragmentBuilder *builder,
                                     BasicBlock *block,
                                     AppPC start_pc) {
  auto frag = new ExitFragment(FRAG_EXIT_NATIVE);
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
static void InitBlockFragment(FragmentBuilder *builder, BasicBlock *block) {
  if (auto direct_block = DynamicCast<DirectBasicBlock *>(block)) {
    AddDirectBlockToFragList(builder, direct_block);
  } else if (auto cached_block = DynamicCast<CachedBasicBlock *>(block)) {
    AddCachedBlockToFragList(builder, cached_block);
  } else if (auto native_block = DynamicCast<NativeBasicBlock *>(block)) {
    AddNativeBlockToFragList(builder, native_block, native_block->StartAppPC());
  } else if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
    AddDecodedBlockToWorkList(builder, decoded_block);
  } else if (auto return_block = DynamicCast<ReturnBasicBlock *>(block)) {
    if (!return_block->UsesMetaData()) {
      AddNativeBlockToFragList(builder, return_block, nullptr);
    }
  }
}

// Initialize the work list for each basic block.
static void InitializeFragAndWorklist(FragmentBuilder *builder) {
  for (auto block : builder->cfg->ReverseBlocks()) {
    InitBlockFragment(builder, block);
  }
}

}  // namespace

// Build a fragment list out of a set of basic blocks.
void BuildFragmentList(ContextInterface *context, LocalControlFlowGraph *cfg,
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
  }
}

}  // namespace granary
