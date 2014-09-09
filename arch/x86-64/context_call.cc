/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/code/fragment.h"

namespace granary {
namespace arch {

// Generates some code to target some client function. The generated code saves
// the machine context and passes it directly to the client function for direct
// manipulation.
CodeFragment *GenerateContextCallCode(ContextInterface *context,
                                      FragmentList *frags, CodeFragment *pred,
                                      uintptr_t func_addr) {
  auto call_frag = new CodeFragment;
  auto exit_frag = new ExitFragment(FRAG_EXIT_NATIVE);
  exit_frag->encoded_pc = context->ContextCallablePC(func_addr);

  pred->successors[FRAG_SUCC_FALL_THROUGH] = call_frag;
  call_frag->successors[FRAG_SUCC_BRANCH] = exit_frag;

  pred->attr.can_add_succ_to_partition = false;
  call_frag->attr.can_add_pred_to_partition = false;
  call_frag->attr.can_add_succ_to_partition = false;

  frags->InsertAfter(pred, call_frag);
  frags->Append(exit_frag);

  if (REDZONE_SIZE_BYTES) {

  }

  if (REDZONE_SIZE_BYTES) {

  }

  return call_frag;
}

}  // namespace arch
}  // namespace granary
