/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/code/fragment.h"

namespace granary {
namespace arch {

// Returns a new instruction that will allocate some stack space for virtual
// register slots.
//
// Note: This function has an architecture-specific implementation.
extern NativeInstruction *AllocateStackSpace(int num_bytes);

// Returns a new instruction that will allocate some stack space allocated
// for virtual registers. The amount of space freed does not necessarily
// correspond to the amount allocated, but instead corresponds to how the
// native stack pointer has changed since virtual registers were allocated.
//
// Note: This function has an architecture-specific implementation.
extern NativeInstruction *FreeStackSpace(int num_bytes);

// Mangle all indirect calls and jumps into NOPs.
//
// Note: This function has an architecture-specific implementation.
void RemoveIndirectCallsAndJumps(Fragment *frag);

// Adjusts / mangles an instruction (potentially more than one) so that the
// usage of the stack pointer remains transparent, despite the fact that the
// native stack pointer has been changed to accommodate virtual register spills.
// Returns the next instruction on which we should operate.
//
// Note: This function has an architecture-specific implementation.
extern void AdjustStackInstruction(Fragment *frag, NativeInstruction *instr,
                                   int adjusted_offset,
                                   int next_adjusted_offset);

// Allocates all remaining non-stack spill slots in some architecture and
// potentially mode (e.g. kernel/user) specific way.
//
// Note: This function has an architecture-specific implementation.
extern void AllocateSlots(FragmentList *frags);

}  // namespace arch
namespace {

// Make sure that we only analyze stack usage within fragments where the stack
// pointer behaves like it's on a C-style call stack.
static void InitStackFrameAnalysis(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    arch::RemoveIndirectCallsAndJumps(frag);
    if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
      GRANARY_ASSERT(code_frag->stack.is_checked);
      auto partition = code_frag->partition.Value();
      if (!code_frag->stack.is_valid) {
        partition->analyze_stack_frame = false;

      // Make sure that this fragment is actually within a partition
      // entry/exit. This condition is acceptable because either this code
      // fragment will be followed by:
      //      1)  Another code fragment within the same partition.
      //      2)  A flag exit/entry fragment within the same partition.
      //      3)  A partition exit fragment, within the same partition.
      //
      // If this fragment is a singleton with no enclosing partition entry/
      // exit fragments, then the next fragment (if any) will be part of a
      // different partition.
      //
      // We care about this case because some special things like direct calls
      // and unspecialized returns fit into these cases.
      } else {
        auto has_succ = false;
        for (auto succ : frag->successors) {
          if (succ) {
            if (succ->partition != frag->partition &&
                !code_frag->attr.branches_to_edge_code) {
              partition->analyze_stack_frame = false;
            }
            has_succ = true;
          }
        }
        if (!has_succ) {
          partition->analyze_stack_frame = false;
        }
      }
    }
  }
#ifdef GRANARY_DEBUG
  // Simple verification step.
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<PartitionEntryFragment *>(frag)) {
      auto partition = frag->partition.Value();
      ++partition->num_partition_entry_frags;
      GRANARY_ASSERT(1 == partition->num_partition_entry_frags);
    }
  }
#endif
}

struct FrameAdjust {
  int32_t shift;
  int32_t compute;
} __attribute__((packed));

static_assert(sizeof(uint64_t) == sizeof(FrameAdjust),
              "Invalid structure packing of `struct FrameAdjust`.");

// Initialize the frame adjustment measurements into instruction meta-data.
static void InitFrameAdjust(Fragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      FrameAdjust adjust = {
        ninstr->instruction.StackPointerShiftAmount(),
        ninstr->instruction.ComputedOffsetBelowStackPointer()
      };
      ninstr->SetMetaData(adjust);
    }
  }
}

// Computes the stack pointer offset on exit from this fragment, updates the
// partition info with bounds on the stack pointer offsets, and returns true
// if any changes to the partition info or fragment exit offset were made.
static bool FindFrameSize(PartitionInfo *partition, Fragment *frag) {
  auto offset = frag->stack_frame.entry_offset;
  const auto old_part_min = partition->min_frame_offset;
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      auto adjust = ninstr->MetaData<FrameAdjust>();
      if (adjust.compute) {
        GRANARY_ASSERT(0 > adjust.compute);
        partition->min_frame_offset = std::min(partition->min_frame_offset,
                                               offset + adjust.compute);
      }
      if (adjust.shift) {
        offset += adjust.shift;
        partition->min_frame_offset = std::min(partition->min_frame_offset,
                                               offset);
      }
    }
  }
  if (offset != frag->stack_frame.exit_offset) {
    frag->stack_frame.exit_offset = offset;
    return true;
  }
  return old_part_min != partition->min_frame_offset;
}

// Performs a forward data-flow analysis to find min and max bounds on the
// stack frame size, relative to the partition entry.
static void FindFrameSizes(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto partition = frag->partition.Value();
    if (!partition->analyze_stack_frame) continue;
    InitFrameAdjust(frag);
  }
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : FragmentListIterator(frags)) {
      auto partition = frag->partition.Value();
      if (!partition->analyze_stack_frame) continue;

      changed = FindFrameSize(partition, frag) || changed;

      for (auto succ : frag->successors) {
        if (!succ) continue;
        if (succ->partition != frag->partition) continue;
        if (IsA<PartitionEntryFragment *>(succ)) continue;

        // TODO(pag): What if we have something like:
        //
        //      F1 --.-> F3
        //      F2 --'
        //
        // Where `F1` and `F2` have different `exit_offset`s? This could
        // happen if some instrumentation branches around something like a
        // `PUSH` or a `POP` (on x86).
        succ->stack_frame.entry_offset = frag->stack_frame.exit_offset;
      }
    }
  }
}

// Adjusts all instructions that read from or write to the stack pointer
static void AdjustStackInstructions(Fragment *frag, int frame_space) {
  auto instr = frag->instrs.First();
  auto offset = frag->stack_frame.entry_offset;
  auto next_offset = 0;
  for (Instruction *next_instr(nullptr); instr;
       offset = next_offset, instr = next_instr) {
    next_offset = offset;
    next_instr = instr->Next();
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      auto adjust = ninstr->MetaData<FrameAdjust>();
      next_offset += adjust.shift;
      arch::AdjustStackInstruction(
          frag, ninstr, offset - frame_space, next_offset - frame_space);
    }
  }
}

// Allocates space on the stack for virtual registers.
static void AllocateStackSlots(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto partition = frag->partition.Value();
    if (!partition->analyze_stack_frame) continue;
    const auto vr_space = partition->num_slots * arch::GPR_WIDTH_BYTES +
                          arch::REDZONE_SIZE_BYTES;
    if (vr_space == arch::REDZONE_SIZE_BYTES) continue;
    const auto frame_space = GRANARY_ALIGN_TO(
        partition->min_frame_offset - vr_space, -arch::GPR_WIDTH_BYTES);

    if (IsA<PartitionEntryFragment *>(frag)) {
      frag->instrs.Append(arch::AllocateStackSpace(frame_space));
    } else if (IsA<PartitionExitFragment *>(frag)) {
      frag->instrs.Append(arch::FreeStackSpace(
          -(frame_space - frag->stack_frame.entry_offset)));
    } else if (IsA<SSAFragment *>(frag)) {
      AdjustStackInstructions(frag, frame_space);
    }
  }
}

}  // namespace

void AllocateSlots(FragmentList *frags) {
  InitStackFrameAnalysis(frags);
  FindFrameSizes(frags);
  AllocateStackSlots(frags);
  arch::AllocateSlots(frags);
}

}  // namespace granary
