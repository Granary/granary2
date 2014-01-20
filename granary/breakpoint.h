/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_BREAKPOINT_H_
#define GRANARY_BREAKPOINT_H_

extern "C" {

void granary_break_on_fault(void);
void granary_break_on_unreachable_code(void);
void granary_break_on_encode(void *);
void granary_break_on_decode(void *);

}  // extern C

#endif  // GRANARY_BREAKPOINT_H_
