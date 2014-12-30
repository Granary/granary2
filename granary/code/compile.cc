/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/encode.h"

#include "granary/base/option.h"

#include "granary/cfg/trace.h"
#include "granary/cfg/block.h"

#include "granary/code/assemble.h"
#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/fragment.h"

#include "granary/app.h"
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

struct CodeCacheUse {
  size_t cache_size[kNumCodeCacheKinds];
  CachePC cache_code[kNumCodeCacheKinds];
};

// Set the encoded address for a label or return address instruction.
static void SetEncodedPC(LabelInstruction *instr, CachePC pc) {
  instr->SetData(pc);
  SetMetaData(instr, pc);
}

// Mark an estimated encode address on all labels/return address annotations.
// This is so that stage encoding is able to guage an accurate size for things.
static void StageEncodeLabels(Fragment *frag) {
  auto estimated_encode_pc = EstimatedCachePC();
  if (frag->entry_label) {
    SetEncodedPC(frag->entry_label, estimated_encode_pc);
  }
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto label = DynamicCast<LabelInstruction *>(instr)) {
      SetEncodedPC(label, estimated_encode_pc);
    }
  }
}

// Stage encode an individual fragment. Returns the number of bytes needed to
// encode all native instructions in this fragment.
static size_t StageEncodeNativeInstructions(Fragment *frag) {
  auto estimated_encode_pc = EstimatedCachePC();
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
  auto size = static_cast<size_t>(encode_pc - estimated_encode_pc);
  GRANARY_ASSERT(0 <= size);
  return size;
}

// Performs stage encoding of a fragment list. This determines the size of each
// fragment and returns the size (in bytes) of the block-specific and edge-
// specific instructions.
static void StageEncode(FragmentList *frags, CodeCacheUse *use) {
  auto first_frag = frags->First();

  // Start by giving every label some plausible location.
  for (auto frag : EncodeOrderedFragmentIterator(first_frag)) {
    StageEncodeLabels(frag);
  }

  // Now that all labels have a plausible encoded location, update every native
  // instruction.
  for (auto frag : EncodeOrderedFragmentIterator(first_frag)) {
    if (frag->encoded_pc) {
      GRANARY_ASSERT(!IsA<CodeFragment *>(frag));
      continue;
    }
    frag->encoded_size = StageEncodeNativeInstructions(frag);
    use->cache_size[frag->cache] += frag->encoded_size;
  }
}

// Relativize the instructions of a fragment.
static void RelativizeInstructions(Fragment *frag, CachePC curr_pc,
                                   bool *update_encode_addresses) {
  if (frag->entry_label) SetEncodedPC(frag->entry_label, curr_pc);

  for (auto instr : InstructionListIterator(frag->instrs)) {

    // `curr_pc` moves forward with the encoded length of instructions.
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      ninstr->instruction.SetEncodedPC(curr_pc);
      curr_pc += ninstr->instruction.EncodedLength();

    // All labels need to know where they are encoded.
    } else if (auto label = DynamicCast<LabelInstruction *>(instr)) {
      SetEncodedPC(label, curr_pc);

    // Record the `curr_pc` for later updating by `UpdateEncodeAddresses`.
    // We update *after* encoding all instructions lest we face a nasty race
    // where some thread sees the updated value of the data and then decides
    // to jump to an incompletely generated block.
    } else if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      if (kAnnotUpdateAddressWhenEncoded == annot->annotation) {
        SetMetaData(annot, curr_pc);
        *update_encode_addresses = true;
      }
    }
  }
}

// Update the pointers associated with all `kAnnotUpdateAddressWhenEncoded`
// annotation instructions. This needs to be done *after* encoded to avoid
// a nasty race where one thread does an indirect jump based on the updated
// pointer and jumps into some incomplete code sequence.
static void UpdateEncodeAddresses(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
        // Update some pointer somewhere with the encoded address of this
        // instruction.
        if (kAnnotUpdateAddressWhenEncoded == annot->annotation) {
          auto cache_pc_ptr = reinterpret_cast<CachePC *>(annot->data);
          *cache_pc_ptr = GetMetaData<CachePC>(annot);
        }
      }
    }
  }
}

// Assign program counters to every fragment and instruction.
static void RelativizeCode(FragmentList *frags, CodeCacheUse *use,
                           bool *update_addresses) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    if (!frag->encoded_pc) {
      auto &cache_code(use->cache_code[frag->cache]);
      GRANARY_ASSERT(nullptr != cache_code);
      frag->encoded_pc = cache_code;
      cache_code += frag->encoded_size;
    }
    RelativizeInstructions(frag, frag->encoded_pc, update_addresses);
  }
}

// Relativize a CFI/Branch instruction.
static void RelativizeBranch(NativeInstruction *cfi, Fragment *succ) {
  if (cfi && !cfi->HasIndirectTarget()) {
    GRANARY_ASSERT(succ);
    cfi->instruction.SetBranchTarget(succ->encoded_pc);
  }
}

// Relativize all control-flow instructions.
static void RelativizeControlFlow(FragmentList *frags) {
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    RelativizeBranch(frag->fall_through_instr,
                     frag->successors[kFragSuccFallThrough]);
    RelativizeBranch(frag->branch_instr, frag->successors[kFragSuccBranch]);
  }
}

// Encode all fragments associated with basic block code and not with direct
// edge or out-edge code.
static void Encode(FragmentList *frags) {
  CodeCacheTransaction transaction;
  arch::InstructionEncoder encoder(arch::InstructionEncodeKind::COMMIT);
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    GRANARY_ASSERT(nullptr != frag->encoded_pc);
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
        if (!ninstr->instruction.WillBeEncoded()) continue;
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
static void AddBlockTracers(FragmentList *frags) {
  auto estimated_encode_pc = EstimatedCachePC();
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!cfrag->attr.is_block_head) continue;
      auto partition = cfrag->partition.Value();
      auto block_meta = cfrag->block_meta;
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
      auto cache_meta = MetaDataCast<CacheMetaData *>(cfrag->block_meta);
      auto partition = cfrag->partition.Value();
      auto entry_frag = partition->entry_frag;
      GRANARY_ASSERT(!cache_meta->start_pc);
      cache_meta->start_pc = entry_frag->encoded_pc;
    }
  }
}

// Update all direct/indirect edge data structures to know about where their
// data is encoded.
static void ConnectEdgesToInstructions(Fragment *succ, NativeInstruction *br) {
  if (!succ) return;
  if (auto exit = DynamicCast<ExitFragment *>(succ)) {
    if (!exit->direct_edge) return;
    GRANARY_ASSERT(nullptr != br);
    GRANARY_ASSERT(!exit->direct_edge->patch_instruction_pc);
    exit->direct_edge->patch_instruction_pc = br->instruction.EncodedPC();
  }
}

// Update all direct/indirect edge data structures to know about where their
// data is encoded.
static void ConnectEdgesToInstructions(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    ConnectEdgesToInstructions(frag->successors[kFragSuccFallThrough],
                               frag->fall_through_instr);
    ConnectEdgesToInstructions(frag->successors[kFragSuccBranch],
                               frag->branch_instr);
  }
}

// Encodes the fragments into the specified code caches.
static CachePC EncodeAndFree(FragmentList *frags) {
  if (GRANARY_UNLIKELY(FLAG_debug_trace_exec)) AddBlockTracers(frags);
  CodeCacheUse cache_use = {{0}, {nullptr}};
  StageEncode(frags, &cache_use);
  for (auto i = 0; i < kNumCodeCacheKinds; ++i) {
    cache_use.cache_code[i] = AllocateCode(static_cast<CodeCacheKind>(i),
                                           cache_use.cache_size[i]);
  }
  auto update_addresses = false;
  RelativizeCode(frags, &cache_use, &update_addresses);
  RelativizeControlFlow(frags);
  Encode(frags);
  auto entry_pc = frags->First()->encoded_pc;

  // Go through all `kAnnotUpdateAddressWhenEncoded` annotations and update
  // the associated pointers with their resolved addresses.
  if (GRANARY_UNLIKELY(update_addresses)) UpdateEncodeAddresses(frags);

  AssignBlockCacheLocations(frags);
  ConnectEdgesToInstructions(frags);
  FreeFragments(frags);
  return entry_pc;
}

}  // namespace

// Compile some instrumented code.
CachePC Compile(Context *context, Trace *cfg) {
  auto frags = Assemble(context, cfg);
  return EncodeAndFree(&frags);
}

// Compile some instrumented code for an indirect edge.
CachePC Compile(Context *context, Trace *cfg,
                IndirectEdge *edge, BlockMetaData *meta) {
  auto frags = Assemble(context, cfg);
  auto target_app_pc = MetaDataCast<AppMetaData *>(meta)->start_pc;
  arch::InstantiateIndirectEdge(edge, &frags, target_app_pc);
  return EncodeAndFree(&frags);
}

}  // namespace granary
