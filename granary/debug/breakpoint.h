/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DEBUG_BREAKPOINT_H_
#define GRANARY_DEBUG_BREAKPOINT_H_

extern "C" {

void granary_break_on_fault(void);
void granary_break_unreachable(void);
void granary_break_on_encode(void *);

}  // extern C

#endif  // GRANARY_DEBUG_BREAKPOINT_H_
