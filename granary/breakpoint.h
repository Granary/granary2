/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BREAKPOINT_H_
#define GRANARY_BREAKPOINT_H_

extern "C" {

__attribute__((noreturn))
void granary_break_on_fault(void);

__attribute__((noreturn))
void granary_break_on_unreachable_code(void);

static inline void granary_break_on_fault_if(bool cond) {
  if (cond) {
    granary_break_on_fault();
    __builtin_unreachable();
  }
}
}  // extern C

#endif  // GRANARY_BREAKPOINT_H_
