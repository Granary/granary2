/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/encode.h"

#include "granary/base/option.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"

#include "granary/code/assemble.h"
#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/fragment.h"

#include "granary/cache.h"
#include "granary/context.h"
#include "granary/util.h"

GRANARY_DEFINE_bool(debug_trace_exec, false,
    "Trace the execution of the program. This records the register state on "
    "entry to every basic block. The default is `no`.\n"
    "\n"
    "The execution trace can be inspected from GDB by issuing the "
    "`print-exec-entry` command. For example, `print-exec-entry 0` will print "
    "the registers on entry to the most recently executed basic block. An "
    "optional second parameter can be passed to the command, which tells "
    "GDB how many instructions to decode from the block. For example, "
    "`print-exec-entry 1 20` will print the registers on entry to the 2nd most "
    "recently executed basic block, and decode and print the 20 instructions "
    "starting at the beginning of the basic block.\n"
    "\n"
    "A value representative of a \"thread id\" is printed along with each "
    "entry. In user space, this value uniquely identifies a thread, but has "
    "no correlation with a thread's ID (tid) from the perspective of the OS. "
    "In kernel space, this value is a shifted version of the stack pointer, "
    "and might make interrupt handlers appear to execute in the same or "
    "different threads than the interrupted tasks.");

namespace granary {
namespace arch {

// Instantiate an indirect out-edge template. The indirect out-edge will
// compare the target of a CFI with `app_pc`, and if the values match, then
// will jump to `cache_pc`, otherwise a fall-back is taken.
//
// Note: This function has an architecture-specific implementation.
//
// Note: This function must be called in the context of an
//       `IndirectEdge::out_edge_pc_lock`.
extern void InstantiateIndirectEdge(IndirectEdge *edge, FragmentList *frags,
                                    AppPC app_pc);

// Adds in some extra "tracing" instructions to the beginning of a basic block.
//
// Note: This function has an architecture-specific implementation.
extern void AddBlockTracer(Fragment *frag, BlockMetaData *meta,
                           CachePC estimated_encode_pc);
}  // namespace arch
namespace {

// Mark an estimated encode address on all labels/return address annotations.
// This is so that stage encoding is able to guage an accurate size for things.
static void StageEncodeLabels(Fragment *frag, CachePC estimated_encode_pc) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      if (IA_LABEL == annot->annotation ||
          IA_RETURN_ADDRESS == annot->annotation) {
        annot->data = reinterpret_cast<uintptr_t>(estimated_encode_pc);
      }
    }
  }
}

// Stage encode an individual fragment. Returns the number of bytes needed to
// encode all native instructions in this fragment.
static int StageEncodeNativeInstructions(Fragment *frag,
                                         const CachePC estimated_encode_pc) {
  auto encode_pc = estimated_encode_pc;
  arch::InstructionEncoder encoder(arch::InstructionEncodeKind::STAGED);
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (ninstr->IsNoOp()) ninstr->instruction.DontEncode();
      GRANARY_IF_DEBUG( bool encoded = ) encoder.EncodeNext(
          &(ninstr->instruction), &encode_pc);
      GRANARY_ASSERT(encoded);
    }
  }
  auto size = static_cast<int>(encode_pc - estimated_encode_pc);
  GRANARY_ASSERT(0 <= size);
  return size;
}

// Performs stage encoding of a fragment list. This determines the size of each
// fragment and returns the size (in bytes) of the block-specific and edge-
// specific instructions.
int StageEncode(FragmentList *frags, CachePC estimated_encode_pc) {
  auto first_frag = frags->First();
  auto num_bytes = 0;

  for (auto frag : EncodeOrderedFragmentIterator(first_frag)) {
    // Don't omit `ExitFragment`s in case they contain labels.
    StageEncodeLabels(frag, estimated_encode_pc);
  }

  for (auto frag : EncodeOrderedFragmentIterator(first_frag)) {
    if (frag->encoded_pc) continue;
    frag->encoded_size = StageEncodeNativeInstructions(frag,
                                                       estimated_encode_pc);
    num_bytes += frag->encoded_size;
  }
  return num_bytes;
}

// Relativize the instructions of a fragment.
static void RelativizeInstructions(Fragment *frag, CachePC curr_pc,
                                   bool *update_encode_addresses) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      ninstr->instruction.SetEncodedPC(curr_pc);
      curr_pc += ninstr->instruction.EncodedLength();

    } else if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      // Make labels and return addresses aware of their encoded addresses.
      if (IA_LABEL == annot->annotation ||
          IA_RETURN_ADDRESS == annot->annotation) {
        annot->data = reinterpret_cast<uintptr_t>(curr_pc);

      // Record the `curr_pc` for later updating by `UpdateEncodeAddresses`.
      } else if (IA_UPDATE_ENCODED_ADDRESS == annot->annotation) {
        SetMetaData(annot, curr_pc);
        *update_encode_addresses = true;
      }
    }
  }
}

// Update the pointers associated with all `IA_UPDATE_ENCODED_ADDRESS`
// annotation instructions. This needs to be done *after* encoded to avoid
// a nasty race where one thread does an indirect jump based on the updated
// pointer and jumps into some incomplete code sequence.
static void UpdateEncodeAddresses(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
        // Update some pointer somewhere with the encoded address of this
        // instruction.
        if (IA_UPDATE_ENCODED_ADDRESS == annot->annotation) {
          auto cache_pc_ptr = reinterpret_cast<CachePC *>(annot->data);
          *cache_pc_ptr = GetMetaData<CachePC>(annot);
        }
      }
    }
  }
}

// Assign program counters to every fragment and instruction.
static void RelativizeCode(FragmentList *frags, CachePC cache_code,
                           bool *update_addresses) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    if (!frag->encoded_pc) {
      frag->encoded_pc = cache_code;
      cache_code += frag->encoded_size;
    }
    RelativizeInstructions(frag, frag->encoded_pc, update_addresses);
  }
}

// Relativize a control-flow instruction.
static void RelativizeCFI(Fragment *frag, ControlFlowInstruction *cfi) {
  if (cfi->IsNoOp() || !cfi->instruction.WillBeEncoded()) return;  // Elided.

  // Note: We use the `arch::Instruction::HasIndirectTarget` instead of
  //       `ControlFlowInstruction::HasIndirectTarget` because the latter
  //       sometimes "lies" in order to hide us from the details of mangling
  //       far-away targets.
  if (cfi->instruction.HasIndirectTarget()) return;

  GRANARY_ASSERT(frag->branch_instr == cfi);
  auto target_frag = frag->successors[FRAG_SUCC_BRANCH];
  GRANARY_ASSERT(nullptr != target_frag);
  auto target_pc = target_frag->encoded_pc;
  GRANARY_ASSERT(nullptr != target_pc);
  cfi->instruction.SetBranchTarget(target_pc);
}

// Relativize a branch instruction.
//
// TODO(pag): This is a bit ugly. `2_build_fragment_list.cc` leaves labels
//            behind (in their respective basic block instruction lists), so
//            that all fragments are correctly connected. However, some
//            branch instructions are introduced at a later point in time,
//            e.g. `10_add_connecting_jumps.cc`, to make sure there are
//            fall-throughs for everything.
//
//            Perhaps one solution would be to move the labels into the
//            correct fragments at some point.
static void RelativizeBranch(Fragment *frag, BranchInstruction *branch) {
  if (frag->branch_instr == branch) {
    branch->instruction.SetBranchTarget(
        frag->successors[FRAG_SUCC_BRANCH]->encoded_pc);

  } else {
    auto target = branch->TargetLabel();
    auto target_pc = target->Data<CachePC>();
    GRANARY_ASSERT(nullptr != target_pc);
    GRANARY_ASSERT(4096UL < target->data);  // Doesn't look like a refcount.
    branch->instruction.SetBranchTarget(target_pc);
  }
}

// Relativize all control-flow instructions.
static void RelativizeControlFlow(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
        RelativizeCFI(frag, cfi);
      } else if (auto branch = DynamicCast<BranchInstruction *>(instr)) {
        RelativizeBranch(frag, branch);
      }
    }
  }
}

// Encode all fragments associated with basic block code and not with direct
// edge or out-edge code.
static void Encode(FragmentList *frags) {
  arch::InstructionEncoder encoder(arch::InstructionEncodeKind::COMMIT);
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
        GRANARY_IF_DEBUG( auto expected_length =
            ninstr->instruction.EncodedLength(); )
        GRANARY_IF_DEBUG( auto encoded = ) encoder.Encode(
            &(ninstr->instruction), ninstr->instruction.EncodedPC());
        GRANARY_ASSERT(encoded);
        GRANARY_ASSERT(expected_length == ninstr->instruction.EncodedLength());
      }
    }
  }
}

// Adds in additional "tracing" instructions to the entrypoints of basic
// blocks.
static void AddBlockTracers(FragmentList *frags, CachePC estimated_encode_pc) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!cfrag->attr.is_block_head) continue;

      auto partition = cfrag->partition.Value();
      auto block_meta = cfrag->attr.block_meta;
      auto block_frag = partition->entry_frag;

      arch::AddBlockTracer(block_frag, block_meta, estimated_encode_pc);
    }
  }
}

// Assign `CacheMetaData::cache_pc` for each basic block.
static void AssignBlockCacheLocations(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!cfrag->attr.is_block_head) continue;
      auto cache_meta = MetaDataCast<CacheMetaData *>(cfrag->attr.block_meta);
      auto partition = cfrag->partition.Value();
      auto entry_frag = partition->entry_frag;

      GRANARY_ASSERT(nullptr == cache_meta->start_pc);
      cache_meta->start_pc = entry_frag->encoded_pc;
    }
  }
}

// Update all direct/indirect edge data structures to know about where their
// data is encoded.
static void ConnectEdgesToInstructions(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto cfrag = DynamicCast<CodeFragment *>(frag);
    if (!cfrag) continue;
    if (!cfrag->branch_instr) continue;

    // Try to get the direct edge (if any) that is targeted by `branch_instr`.
    auto edge_frag = DynamicCast<ExitFragment *>(
        cfrag->successors[FRAG_SUCC_BRANCH]);
    if (!edge_frag) continue;
    if (EDGE_KIND_DIRECT != edge_frag->edge.kind) continue;

    GRANARY_ASSERT(IsA<ControlFlowInstruction *>(cfrag->branch_instr));

    // Tell the edge data structure what instruction will eventually need to
    // be patched (after that instruction's target is eventually resolved)
    auto edge = edge_frag->edge.direct;
    edge->patch_instruction_pc = cfrag->branch_instr->instruction.EncodedPC();
  }
}

// Encodes the fragments into the specified code caches.
static void Encode(FragmentList *frags, CodeCache *block_cache) {
  auto estimated_addr = EstimatedCachePC();
  if (GRANARY_UNLIKELY(FLAG_debug_trace_exec)) {
    AddBlockTracers(frags, estimated_addr);
  }
  if (auto num_bytes = StageEncode(frags, estimated_addr)) {
    auto cache_code = block_cache->AllocateBlock(num_bytes);
    auto cache_code_end = cache_code + num_bytes;
    auto update_addresses = false;
    RelativizeCode(frags, cache_code, &update_addresses);
    RelativizeControlFlow(frags);

    do {
      CodeCacheTransaction transaction(block_cache, cache_code, cache_code_end);
      Encode(frags);
    } while (0);

    if (GRANARY_UNLIKELY(update_addresses)) {
      UpdateEncodeAddresses(frags);
    }
  }
  AssignBlockCacheLocations(frags);
  ConnectEdgesToInstructions(frags);
}

}  // namespace

// Compile some instrumented code.
void Compile(ContextInterface *context, LocalControlFlowGraph *cfg) {
  auto frags = Assemble(context, cfg);
  Encode(&frags, context->BlockCodeCache());
  FreeFragments(&frags);
}

// Compile some instrumented code for an indirect edge.
void Compile(ContextInterface *context, LocalControlFlowGraph *cfg,
             IndirectEdge *edge, AppPC target_app_pc) {
  auto frags = Assemble(context, cfg);
  do {
    SpinLockedRegion locker(&(edge->out_edge_pc_lock));
    arch::InstantiateIndirectEdge(edge, &frags, target_app_pc);
    Encode(&frags, context->BlockCodeCache());
  } while (false);
  FreeFragments(&frags);
}

}  // namespace granary
