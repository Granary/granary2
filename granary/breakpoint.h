/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BREAKPOINT_H_
#define GRANARY_BREAKPOINT_H_

extern "C" {

[[noreturn]]
void granary_unreachable(const char *error=nullptr);

void granary_curiosity(void);

#define granary_break_on_fault_if(cond, str) \
  if (cond) { \
    granary_unreachable(str); \
    __builtin_unreachable(); \
  }

}  // extern C

#endif  // GRANARY_BREAKPOINT_H_
