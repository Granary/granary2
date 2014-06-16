/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/encode.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"

#include "granary/code/assemble.h"
#include "granary/code/compile.h"
#include "granary/code/edge.h"

#include "granary/cache.h"
#include "granary/module.h"
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
  int max_edge_size;
  CachePC min_edge_pc;
  CachePC max_edge_pc;
};

// Performs stage encoding of a fragment list. This determines the size of each
// fragment and returns the size (in bytes) of the block-specific and edge-
// specific instructions.
StageEncodeResult StageEncode(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    frag->encoded_size = StageEncode(frag);
  }

  PartitionInfo *last_partition = nullptr;
  StageEncodeResult result = {0, 0, nullptr, nullptr};
  auto edge_size = 0;

  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    auto partition = frag->partition.Value();
    auto edge = partition->edge;

    // Direct edge code.
    if (edge && EDGE_KIND_DIRECT == edge->kind) {
      if (last_partition != partition) {
        result.min_edge_pc = std::min(result.min_edge_pc,
                                      edge->direct->edge_code);
        result.max_edge_pc = std::min(result.max_edge_pc,
                                      edge->direct->edge_code);
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
static void RelativizeCode(FragmentList *frags, CachePC cache_code) {
  PartitionInfo *last_partition = nullptr;
  CachePC edge_code(nullptr);
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    auto partition = frag->partition.Value();

    // Direct edge code.
    if (partition->edge && EDGE_KIND_DIRECT == partition->edge->kind) {
      // Different edge code, so make sure each direct edge block is cache-line
      // aligned.
      if (!edge_code || last_partition != partition) {
        edge_code = partition->edge->direct->edge_code;
      }

      frag->encoded_pc = edge_code;
      edge_code += frag->encoded_size;

    // TODO(pag): "out-edge" code.

    } else {  // Basic block code, and/or in-edge code.
      frag->encoded_pc = cache_code;
      cache_code += frag->encoded_size;
    }

    last_partition = partition;
    RelativizeInstructions(frag, frag->encoded_pc);
  }
}

// Relativize all control-flow instructions.
static void RelativizeCFIs(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
        if (cfi->HasIndirectTarget()) continue;  // No target PC.
        GRANARY_ASSERT(frag->branch_instr == cfi);
        auto target_frag = frag->successors[FRAG_SUCC_BRANCH];
        GRANARY_ASSERT(nullptr != target_frag);
        auto target_pc = target_frag->encoded_pc;
        GRANARY_ASSERT(nullptr != target_pc);

        // Set the target PC if this wasn't a fall-through that was elided.
        if (!cfi->IsNoOp()) cfi->instruction.SetBranchTarget(target_pc);

      } else if (auto branch = DynamicCast<BranchInstruction *>(instr)) {
        auto target = branch->TargetInstruction();
        auto target_pc = target->GetData<CachePC>();
        GRANARY_ASSERT(nullptr != target_pc);
        branch->instruction.SetBranchTarget(target_pc);
      }
    }
  }
}

// Encode all fragments associated with basic block code and not with direct
// edge or out-edge code.
template <typename CondT>
static void ConditionalEncode(FragmentList *frags, CondT cond) {
  arch::InstructionEncoder encoder(arch::InstructionEncodeKind::COMMIT);
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    if (!cond(frag)) continue;
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
      partition->entry_frag = frag;
    }
  }
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!cfrag->attr.is_block_head) continue;
      auto cache_meta = MetaDataCast<CacheMetaData *>(cfrag->attr.block_meta);
      auto partition = cfrag->partition.Value();
      auto entry_frag = partition->entry_frag;
      GRANARY_ASSERT(nullptr == cache_meta->cache_pc);
      cache_meta->cache_pc = entry_frag->encoded_pc;
    }
  }
}

// Update all direct/indirect edge data structures to know about where their
// data is encoded.
static void ConnectEdgesToInstructions(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (EDGE_KIND_INVALID == cfrag->edge.kind) continue;

      auto partition = cfrag->partition.Value();
      GRANARY_ASSERT(nullptr != partition->edge);
      GRANARY_ASSERT(EDGE_KIND_INVALID != partition->edge->kind);

      if (EDGE_KIND_DIRECT == cfrag->edge.kind) {
        GRANARY_ASSERT(EDGE_KIND_DIRECT == partition->edge->kind);

        auto cfi = partition->edge_patch_instruction;
        GRANARY_ASSERT(nullptr != cfi);

        auto edge = cfrag->edge.direct;
        GRANARY_ASSERT(nullptr != edge);

        edge->patch_instruction = cfi->EncodedPC();
      }
    }
  }
}

// Encodes the fragments into the specified code caches.
static void Encode(FragmentList *frags, CodeCacheInterface *block_cache,
                   CodeCacheInterface *edge_cache) {
  auto result = StageEncode(frags);

  GRANARY_ASSERT(0 < result.block_size);
  GRANARY_ASSERT(arch::EDGE_CODE_SIZE_BYTES >= result.max_edge_size);

  auto cache_code = block_cache->AllocateBlock(result.block_size);
  RelativizeCode(frags, cache_code);
  RelativizeCFIs(frags);

  if (result.max_edge_size) {
    result.max_edge_pc += arch::EDGE_CODE_SIZE_BYTES;
    CodeCacheTransaction transaction(edge_cache, result.min_edge_pc,
                                     result.max_edge_pc);
    ConditionalEncode(frags, [] (const Fragment *frag) {
      const auto partition = frag->partition.Value();
      return partition->edge && EDGE_KIND_DIRECT == partition->edge->kind;
    });
  }
  if (auto cache_code_end = cache_code + result.block_size) {
    CodeCacheTransaction transaction(block_cache, cache_code, cache_code_end);
    ConditionalEncode(frags, [] (const Fragment *frag) {
      const auto partition = frag->partition.Value();
      return !partition->edge || EDGE_KIND_DIRECT != partition->edge->kind;
    });
  }
  AssignBlockCacheLocations(frags);
  ConnectEdgesToInstructions(frags);
}

}  // namespace

// Compile some instrumented code.
void Compile(LocalControlFlowGraph *cfg, CodeCacheInterface *edge_code_cache) {
  auto meta = cfg->EntryBlock()->MetaData();
  auto module_meta = MetaDataCast<ModuleMetaData *>(meta);
  auto block_code_cache = module_meta->GetCodeCache();
  auto frags = Assemble(block_code_cache, cfg);
  Encode(&frags, block_code_cache, edge_code_cache);
  FreeFragments(&frags);
}

}  // namespace granary
