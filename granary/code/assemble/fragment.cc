/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/code/assemble/fragment.h"
#include "granary/breakpoint.h"

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Fragment, 2),
      (CodeFragment, 2 * 3),
      (PartitionEntryFragment, 2 * 5),
      (PartitionExitFragment, 2 * 7),
      (FlagEntryFragment, 2 * 11),
      (FlagExitFragment, 2 * 13),
      (ExitFragment, 2 * 17))

GRANARY_DEFINE_BASE_CLASS(Fragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, CodeFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, PartitionEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, PartitionExitFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, FlagEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, FlagExitFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, ExitFragment)

Fragment::Fragment(void)
    : list(),
      instrs(),
      partition(),
      flag_zone(),
      temp(),
      successors{nullptr, nullptr},
      branch_instr(nullptr) { }

CodeFragment::~CodeFragment(void) {}
PartitionEntryFragment::~PartitionEntryFragment(void) {}
PartitionExitFragment::~PartitionExitFragment(void) {}
FlagEntryFragment::~FlagEntryFragment(void) {}
FlagExitFragment::~FlagExitFragment(void) {}
ExitFragment::~ExitFragment(void) {}

FlagZone::FlagZone(VirtualRegister flag_save_reg_,
                   VirtualRegister flag_killed_reg_)
    : killed_flags(0),
      live_flags(0),
      flag_save_reg(flag_save_reg_),
      flag_killed_reg(flag_killed_reg_),
      live_regs() {}

#if 0

// Initialize the fragment from a basic block.
Fragment::Fragment(int id_)
    : fall_through_target(nullptr),
      branch_target(nullptr),
      next(nullptr),
      prev(nullptr),
      partition_sentinel(nullptr),
      cached_back_link(nullptr),
      ssa_vars(nullptr),
      id(id_),
      partition_id(0),
      app_live_flags(0),
      inst_killed_flags(0),
      is_decoded_block_head(false),
      is_future_block_head(false),
      is_exit(false),
      writes_to_stack_pointer(false),
      reads_from_stack_pointer(false),
      kind(FRAG_KIND_INSTRUMENTATION),
      block_meta(nullptr),
      first(nullptr),
      last(nullptr) {}

// Append an instruction into the fragment.
void Fragment::AppendInstruction(std::unique_ptr<Instruction> instr) {
  if (last) {
    last = last->InsertAfter(std::move(instr));
  } else {
    last = first = instr.release();
  }

  // Break this fragment if it changes the stack pointer.
  if (auto ninstr = DynamicCast<NativeInstruction *>(last)) {
    if (FRAG_KIND_APPLICATION != kind && ninstr->IsAppInstruction()) {
      kind = FRAG_KIND_APPLICATION;
    }
    if (ninstr->instruction.WritesToStackPointer()) {
      writes_to_stack_pointer = true;
      reads_from_stack_pointer = ninstr->instruction.ReadsFromStackPointer();
    }
  }
}

// Remove an instruction.
std::unique_ptr<Instruction> Fragment::RemoveInstruction(
    Instruction * const instr) {
  auto prev_instr = instr->Previous();
  auto next_instr = instr->Next();
  if (!prev_instr) {
    GRANARY_ASSERT(instr == first);
    first = next_instr;
  }
  if (!next_instr) {
    GRANARY_ASSERT(instr == last);
    last = prev_instr;
  }
  return Instruction::Unlink(instr);
}

// Insert an instruction before another instruction
Instruction *Fragment::InsertBefore(Instruction *insert_loc,
                                    std::unique_ptr<Instruction> insert_instr) {
  auto inserted = insert_loc->InsertBefore(std::move(insert_instr));
  if (first == insert_loc) {
    first = inserted;
  }
  return inserted;
}

// Insert an instruction before another instruction
Instruction *Fragment::InsertAfter(Instruction *insert_loc,
                                   std::unique_ptr<Instruction> insert_instr) {
  auto inserted = insert_loc->InsertAfter(std::move(insert_instr));
  if (last == insert_loc) {
    last = inserted;
  }
  return inserted;
}
#endif

}  // namespace granary
