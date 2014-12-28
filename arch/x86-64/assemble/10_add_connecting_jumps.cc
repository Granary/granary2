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

// Don't encode `instr`, but leave it in place.
void ElideInstruction(Instruction *instr) {
  instr->dont_encode = true;
}

// Adds a fall-through jump, if needed, to this fragment.
NativeInstruction *AddFallThroughJump(Fragment *frag,
                                      Fragment *fall_through_frag) {
  auto label = new LabelInstruction;
  fall_through_frag->instrs.Prepend(label);

  arch::Instruction ni;
  JMP_RELBRd(&ni, label);
  auto instr = new BranchInstruction(&ni, label);
  frag->instrs.Append(instr);
  return instr;
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

#ifdef GRANARY_TARGET_debug
# ifdef GRANARY_WHERE_user
extern "C" {
extern int getpid(void);
extern ssize_t write (int filedes, const void *buffer, size_t size);
extern long long read(int __fd, void *__buf, size_t __nbytes);
}  // extern C

namespace {

static void TrapOnBadFallThrough(void) {
  char buff[1024];
  auto num_bytes = Format(
      buff, sizeof buff,
      "Fell off the end of a basic block!\n"
     "Process ID for attaching GDB: %d\n", getpid());
  write(1, buff, num_bytes);
  while (true) read(0, buff, 1);  // Never return!

  GRANARY_ASSERT(false);
}

// A pointer to a native address, that points to `TrapOnBadFallThrough`. This
// will be a memory leak, but that is fine.
static NativeAddress *trap_func_ptr = nullptr;

}  // namespace
# endif  // GRANARY_WHERE_user

// Catches erroneous fall-throughs off the end of the basic block.
void AddFallThroughTrap(Fragment *frag) {
  arch::Instruction ni;
# ifdef GRANARY_WHERE_user
  if (GRANARY_UNLIKELY(!trap_func_ptr)) {
    new NativeAddress(UnsafeCast<PC>(TrapOnBadFallThrough), &trap_func_ptr);
  }
  CALL_NEAR_MEMv(&ni, &(trap_func_ptr->addr));
  frag->instrs.Append(new NativeInstruction(&ni));

# else  // GRANARY_WHERE_kernel
  UD2(&ni);
  frag->instrs.Append(new NativeInstruction(&ni));
# endif
}
#endif  // GRANARY_TARGET_debug

}  // namespace arch
}  // namespace granary
