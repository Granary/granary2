/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/encode.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"

#include "granary/code/assemble.h"
#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/fragment.h"

#include "granary/cache.h"
#include "granary/context.h"
#include "granary/module.h"
#include "granary/util.h"

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
                                         CachePC estimated_encode_pc) {
  auto encode_pc = estimated_encode_pc;
  arch::InstructionEncoder encoder(arch::InstructionEncodeKind::STAGED);
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (ninstr->instruction.IsNoOp()) continue;
      GRANARY_IF_DEBUG( bool encoded = ) encoder.EncodeNext(
          &(ninstr->instruction), &encode_pc);
      GRANARY_ASSERT(encoded);
    }
  }
  return static_cast<int>(encode_pc - estimated_encode_pc);
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
static void RelativizeInstructions(Fragment *frag, CachePC curr_pc) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (!ninstr->instruction.IsNoOp()) {
        ninstr->instruction.SetEncodedPC(curr_pc);
        curr_pc += ninstr->instruction.EncodedLength();
      }
    } else if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      // Make labels and return addresses aware of their encoded addresses.
      if (IA_LABEL == annot->annotation ||
          IA_RETURN_ADDRESS == annot->annotation) {
        annot->data = reinterpret_cast<uintptr_t>(curr_pc);

      // Update some pointer somewhere with the encoded address of this
      // instruction.
      } else if (IA_UPDATE_ENCODED_ADDRESS == annot->annotation) {
        *reinterpret_cast<CachePC *>(annot->data) = curr_pc;
      }
    }
  }
}

// Assign program counters to every fragment and instruction.
static void RelativizeCode(FragmentList *frags, CachePC cache_code) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    if (frag->encoded_pc) continue;

    frag->encoded_pc = cache_code;
    cache_code += frag->encoded_size;
    RelativizeInstructions(frag, frag->encoded_pc);
  }
}

// Relativize all control-flow instructions.
static void RelativizeCFIs(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
        if (cfi->HasIndirectTarget()) continue;  // No target PC.
        if (IsA<NativeBasicBlock *>(cfi->TargetBlock())) continue;

        GRANARY_ASSERT(frag->branch_instr == cfi);
        auto target_frag = frag->successors[FRAG_SUCC_BRANCH];
        GRANARY_ASSERT(nullptr != target_frag);
        auto target_pc = target_frag->encoded_pc;
        GRANARY_ASSERT(nullptr != target_pc);

        // Set the target PC if this wasn't a fall-through that was elided.
        if (!cfi->IsNoOp()) cfi->instruction.SetBranchTarget(target_pc);

      } else if (auto branch = DynamicCast<BranchInstruction *>(instr)) {
        auto target = branch->TargetInstruction();
        auto target_pc = target->Data<CachePC>();
        GRANARY_ASSERT(nullptr != target_pc);
        branch->instruction.SetBranchTarget(target_pc);
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
        if (ninstr->IsNoOp()) continue;
        GRANARY_IF_DEBUG( auto encoded = ) encoder.Encode(
            &(ninstr->instruction), ninstr->instruction.EncodedPC());
        GRANARY_ASSERT(encoded);
      }
    }
  }
}

// Assign `CacheMetaData::cache_pc` for each basic block.
static void AssignBlockCacheLocations(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!cfrag->attr.is_block_head) continue;
      auto partition = cfrag->partition.Value();
      partition->entry_frag = frag;
    }
  }
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<PartitionEntryFragment *>(frag)) {
      auto partition = frag->partition.Value();
      if (partition->entry_frag) {
        partition->entry_frag = frag;
      }
    }
  }
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
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!cfrag->attr.branches_to_edge_code) continue;
      if (cfrag->branch_instr->HasIndirectTarget()) continue;

      auto direct_edge_frag = DynamicCast<ExitFragment *>(
          cfrag->successors[FRAG_SUCC_BRANCH]);

      if (direct_edge_frag) {
        auto edge = direct_edge_frag->edge.direct;
        edge->patch_instruction = cfrag->branch_instr->instruction.EncodedPC();
      }
    }
  }
}

// Encodes the fragments into the specified code caches.
static void Encode(FragmentList *frags, CodeCache *block_cache) {
  auto estimated_addr = block_cache->AllocateBlock(0);
  auto num_bytes = StageEncode(frags, estimated_addr);
  auto cache_code = block_cache->AllocateBlock(num_bytes);

  RelativizeCode(frags, cache_code);
  RelativizeCFIs(frags);
  if (auto cache_code_end = cache_code + num_bytes) {
    CodeCacheTransaction transaction(block_cache, cache_code, cache_code_end);
    Encode(frags);
  }
  AssignBlockCacheLocations(frags);
  ConnectEdgesToInstructions(frags);
}

}  // namespace

// Compile some instrumented code.
void Compile(ContextInterface *context, LocalControlFlowGraph *cfg) {
  auto block_cache = context->BlockCodeCache();
  auto frags = Assemble(context, block_cache, cfg);
  Encode(&frags, block_cache);
  FreeFragments(&frags);
}

// Compile some instrumented code for an indirect edge.
void Compile(ContextInterface *context, LocalControlFlowGraph *cfg,
             IndirectEdge *edge, AppPC target_app_pc) {
  auto block_cache = context->BlockCodeCache();
  auto frags = Assemble(context, block_cache, cfg);
  do {
    FineGrainedLocked locker(&(edge->out_edge_pc_lock));
    arch::InstantiateIndirectEdge(edge, &frags, target_app_pc);
    Encode(&frags, block_cache);
  } while (false);
  FreeFragments(&frags);
}

}  // namespace granary
