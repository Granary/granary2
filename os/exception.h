/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_EXCEPTION_H_
#define OS_EXCEPTION_H_

#include "granary/base/pc.h"

namespace granary {
namespace arch {
class Instruction;
}
namespace os {

// Returns true if the instruction `instr` can cause an exception, and if
// so, updates the application-specific recovery PC, and updates the Granary-
// specific emulation PC for the instruction.
bool GetExceptionInfo(const arch::Instruction *instr, AppPC *recovery_pc,
                      AppPC *emulation_pc);

}  // namespace os
}  // namespace granary

#endif  // OS_EXCEPTION_H_
