/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/encode.h"

#include "granary/cfg/basic_block.h"

#include "granary/code/encode.h"

#include "granary/cache.h"
#include "granary/util.h"

namespace granary {
namespace {

// Stage encode an individual fragment. Returns the number of bytes needed to
// encode all native instructions in this fragment.
static int StageEncode(Fragment *frag) {
  arch::InstructionEncoder encoder(arch::InstructionEncodeKind::STAGED);
  CachePC encode_addr = nullptr;
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (!ninstr->instruction.IsNoOp()) {
        GRANARY_IF_DEBUG( bool encoded = ) encoder.EncodeNext(
            &(ninstr->instruction), &encode_addr);
        GRANARY_ASSERT(encoded);
      }
    } else if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      if (IA_LABEL == annot->annotation ||
          IA_RETURN_ADDRESS == annot->annotation) {
        annot->data = reinterpret_cast<uintptr_t>(encode_addr);
      }
    }
  }
  return static_cast<int>(reinterpret_cast<intptr_t>(encode_addr));
}

struct StageEncodeResult {
  int block_size;
  int num_direct_edges;
  int max_edge_size;
};

// Performs stage encoding of a fragment list. This determines the size of each
// fragment and returns the size (in bytes) of the block-specific and edge-
// specific instructions.
StageEncodeResult StageEncode(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    frag->encoded_size = StageEncode(frag);
  }

  PartitionInfo *last_partition = nullptr;
  StageEncodeResult result = {0, 0, 0};
  auto edge_size = 0;

  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    auto partition = frag->partition.Value();

    // Direct edge code.
    if (partition->is_edge_code && !partition->is_indirect_edge_code) {
      if (last_partition != partition) {
        result.num_direct_edges += 1;
        last_partition = partition;
        edge_size = 0;
      }
      edge_size += frag->encoded_size;
      result.max_edge_size = std::max(edge_size, result.max_edge_size);

    // Basic block code.
    } else {
      result.block_size += frag->encoded_size;
    }
  }

  // Align direct edge code chunks to be sized according to the cache lines.
  result.max_edge_size = GRANARY_ALIGN_TO(result.max_edge_size,
                                          arch::CACHE_LINE_SIZE_BYTES);
  return result;
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
      if (IA_LABEL == annot->annotation ||
          IA_RETURN_ADDRESS == annot->annotation) {
        annot->data = reinterpret_cast<uintptr_t>(curr_pc);
      }
    }
  }
}

// Assign program counters to every fragment and instruction.
static void RelativizeCode(FragmentList *frags, CachePC cache_code,
                           CachePC edge_code) {
  PartitionInfo *last_partition = nullptr;
  auto entry_pc = cache_code;

  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    auto partition = frag->partition.Value();

    // Direct edge code.
    if (partition->is_edge_code && !partition->is_indirect_edge_code) {
      if (last_partition != partition) {
        last_partition = partition;
        auto edge_code_addr = reinterpret_cast<uintptr_t>(edge_code);
        edge_code_addr = GRANARY_ALIGN_TO(edge_code_addr,
                                          arch::CACHE_LINE_SIZE_BYTES);
        edge_code = reinterpret_cast<CachePC>(edge_code_addr);
      }

      frag->encoded_pc = edge_code;
      edge_code += frag->encoded_size;

    // Basic block code.
    } else {
      if (GRANARY_UNLIKELY(nullptr != entry_pc)) {
        // Make sure the entry block has its meta-data updated.
        if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
          auto cache_meta = MetaDataCast<CacheMetaData *>(
              cfrag->attr.block_meta);
          GRANARY_ASSERT(nullptr != cache_meta);
          GRANARY_ASSERT(nullptr == cache_meta->cache_pc);
          cache_meta->cache_pc = entry_pc;
          entry_pc = nullptr;
        }
      }

      frag->encoded_pc = cache_code;
      cache_code += frag->encoded_size;
    }
    RelativizeInstructions(frag, frag->encoded_pc);
  }
}

// Relativize all control-flow instructions.
static void RelativizeCFIs(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
        if (cfi->IsNoOp()) continue;  // Might have been elided.
        if (cfi->HasIndirectTarget()) continue;  // No target PC.
        GRANARY_ASSERT(frag->branch_instr == cfi);
        auto target_frag = frag->successors[FRAG_SUCC_BRANCH];
        GRANARY_ASSERT(nullptr != target_frag);
        auto target_pc = frag->encoded_pc;
        GRANARY_ASSERT(nullptr != target_pc);
        cfi->instruction.SetBranchTarget(target_pc);

        // Update the target block's meta-data with where this block will be
        // encoded. At first sight, this is sort of a backwards way of doing
        // things, but it's reasonable
        auto target_block = DynamicCast<InstrumentedBasicBlock *>(
            cfi->TargetBlock());
        GRANARY_ASSERT(nullptr != target_block);
        if (auto cache_meta = GetMetaData<CacheMetaData>(target_block)) {
          GRANARY_ASSERT(nullptr == cache_meta->cache_pc);
          cache_meta->cache_pc = target_pc;

          // TODO(pag): This sets `cache_pc` to the edge code PC, which might
          //            be undesirable.
        }

      } else if (auto branch = DynamicCast<BranchInstruction *>(instr)) {
        auto target = branch->TargetInstruction();
        auto target_pc = target->GetData<CachePC>();
        GRANARY_ASSERT(nullptr != target_pc);
        branch->instruction.SetBranchTarget(target_pc);
      }
    }
  }
}

// Encode all fragments that fall into the [begin, end) range.
static void EncodeInRange(FragmentList *frags, CachePC begin, CachePC end) {
  arch::InstructionEncoder encoder(arch::InstructionEncodeKind::COMMIT);
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    if (frag->encoded_pc < begin) continue;
    if (frag->encoded_pc >= end) continue;
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

}  // namespace

// Encodes the fragments into the specified code caches.
void Encode(FragmentList *frags, CodeCacheInterface *block_cache,
            CodeCacheInterface *edge_cache) {
  auto result = StageEncode(frags);
  auto edge_allocation = result.max_edge_size * result.num_direct_edges;
  auto cache_code = block_cache->AllocateBlock(result.block_size);
  auto edge_code = edge_cache->AllocateBlock(edge_allocation);

  RelativizeCode(frags, cache_code, edge_code);
  RelativizeCFIs(frags);

  edge_cache->BeginTransaction();
  EncodeInRange(frags, edge_code, edge_code + edge_allocation);
  edge_cache->EndTransaction();

  block_cache->BeginTransaction();
  EncodeInRange(frags, cache_code, cache_code + result.block_size);
  block_cache->EndTransaction();

}

}  // namespace granary
