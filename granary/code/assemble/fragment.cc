/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/code/assemble/fragment.h"
#include "granary/breakpoint.h"

namespace granary {

// Initialize the fragment from a basic block.
Fragment::Fragment(int id_)
    : fall_through_target(nullptr),
      branch_target(nullptr),
      next(nullptr),
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

}  // namespace granary
