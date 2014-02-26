/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BREAKPOINT_H_
#define GRANARY_BREAKPOINT_H_

extern "C" {

void granary_break_on_fault(void);
void granary_break_on_unreachable_code(void);
void granary_break_on_encode(const void *);
void granary_break_on_decode(const void *);

inline void granary_break_on_fault_if(bool cond) {
  if (cond) {
    granary_break_on_fault();
    __builtin_unreachable();
  }
}

}  // extern C

#endif  // GRANARY_BREAKPOINT_H_
