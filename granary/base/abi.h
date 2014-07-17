/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_ABI_H_
#define GRANARY_BASE_ABI_H_
#ifndef GRANARY_TEST
extern "C" {

void __cxa_pure_virtual(void);
void _ZSt25__throw_bad_function_callv(void);
void _ZdlPv(void);  // operator delete.
void _Znwm(void);  // operator new.

}  // extern C
#endif  // GRANARY_TEST
#endif  // GRANARY_BASE_ABI_H_
