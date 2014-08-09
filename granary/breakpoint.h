/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BREAKPOINT_H_
#define GRANARY_BREAKPOINT_H_

extern "C" {

__attribute__((noreturn, analyzer_noreturn))
void granary_unreachable(void);

void granary_curiosity(void);

#define granary_break_on_fault_if(cond) \
  if (cond) { \
    granary_unreachable(); \
    __builtin_unreachable(); \
  }

}  // extern C

#endif  // GRANARY_BREAKPOINT_H_
