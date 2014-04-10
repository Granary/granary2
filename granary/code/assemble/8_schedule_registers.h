/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_8_SCHEDULE_REGISTERS_H_
#define GRANARY_CODE_ASSEMBLE_8_SCHEDULE_REGISTERS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declaration.
class Fragment;

// Schedule virtual registers to either physical registers or to stack/TLS
// slots.
void ScheduleRegisters(Fragment * const frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_8_SCHEDULE_REGISTERS_H_
