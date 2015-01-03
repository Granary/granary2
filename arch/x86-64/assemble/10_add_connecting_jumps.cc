/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"  // For `NativeAddress`.

#include "os/logging.h"

namespace granary {
namespace arch {

// Adds a fall-through jump, if needed, to this fragment.
void AddFallThroughJump(Fragment *frag, Fragment *fall_through_frag) {
  Instruction ni;
  JMP_RELBRd(&ni, fall_through_frag->encoded_pc);  // Doesn't matter if null.
  frag->fall_through_instr = new NativeInstruction(&ni);
  frag->instrs.Append(frag->fall_through_instr);
}

// Returns true if the target of a jump must be encoded in a nearby location.
bool IsNearRelativeJump(NativeInstruction *instr) {
  switch (instr->instruction.iclass) {
    case XED_ICLASS_JRCXZ:
    case XED_ICLASS_LOOP:
    case XED_ICLASS_LOOPE:
    case XED_ICLASS_LOOPNE:
      return true;
    default:
      return false;
  }
}

namespace {

// Instruction iclass reversers for conditional branches, indexed by
// `instr->iclass - XED_ICLASS_JB`.
typedef void (CFIBuilder)(Instruction *, PC);
static CFIBuilder * const kReversedCFIBuilderss[] = {
  JNB_RELBRd<PC>,
  JNBE_RELBRd<PC>,
  JNL_RELBRd<PC>,
  JNLE_RELBRd<PC>,
  nullptr,
  nullptr,
  JB_RELBRd<PC>,
  JBE_RELBRd<PC>,
  JL_RELBRd<PC>,
  JLE_RELBRd<PC>,
  JO_RELBRd<PC>,
  JP_RELBRd<PC>,
  JS_RELBRd<PC>,
  JZ_RELBRd<PC>,
  JNO_RELBRd<PC>,
  JNP_RELBRd<PC>,
  nullptr,
  JNS_RELBRd<PC>,
  JNZ_RELBRd<PC>
};

}  // namespace

// Try to negate the branch condition. Returns `false` if the branch condition
// was not merged.
bool TryNegateBranchCondition(NativeInstruction *instr) {
  if (!instr) return false;
  if (IsNearRelativeJump(instr)) return false;
  if (instr->HasIndirectTarget()) return false;
  if (!instr->IsConditionalJump()) return false;
  auto &ainstr(instr->instruction);
  kReversedCFIBuilderss[ainstr.iclass - XED_ICLASS_JB](&ainstr, nullptr);
  return true;
}

#ifdef GRANARY_TARGET_debug
extern "C" {
void granary_break_on_bad_fall_through(void) {
  GRANARY_ASSERT(false);
}
}  // extern C

// Catches erroneous fall-throughs off the end of the basic block.
void AddFallThroughTrap(Fragment *frag) {
  arch::Instruction ni;
  CALL_NEAR_RELBRd(&ni, granary_break_on_bad_fall_through);
  frag->instrs.Append(new NativeInstruction(&ni));
}
#endif  // GRANARY_TARGET_debug

}  // namespace arch
}  // namespace granary
