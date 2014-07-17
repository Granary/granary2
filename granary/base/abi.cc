/* Copyright 2014 Peter Goodman, all rights reserved. */
#ifndef GRANARY_TEST
extern "C" {

void __cxa_pure_virtual(void) { __builtin_trap(); }
void _ZSt25__throw_bad_function_callv(void) { __builtin_trap(); }
void _ZdlPv(void) { __builtin_trap(); }
void _Znwm(void) { __builtin_trap(); }

}  // extern C
#endif  // GRANARY_TEST
