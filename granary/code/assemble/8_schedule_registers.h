/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_8_SCHEDULE_REGISTERS_H_
#define GRANARY_CODE_ASSEMBLE_8_SCHEDULE_REGISTERS_H_

namespace granary {

// Forward declaration.
class Fragment;

// Schedule virtual registers.
void ScheduleRegisters(Fragment * const frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_8_SCHEDULE_REGISTERS_H_
