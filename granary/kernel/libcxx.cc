/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_KERNEL_LIBCXX_CC_
#define GRANARY_KERNEL_LIBCXX_CC_

#include "granary/breakpoint.h"

extern "C" {

// Allocate an exception.
void *__cxa_allocate_exception(void) {
  granary_break_on_fault();
  return nullptr;
}

// Free an exception.
void __cxa_free_exception(void) {
  granary_break_on_fault();
}

// Throwing an exception.
void __cxa_throw(void) {
  granary_break_on_fault();
}

// Called when a pure virtual method is invoked.
void __cxa_pure_virtual(void) {
  granary_break_on_fault();
}

// `operator new`
void *_Znwm(void) {
  granary_break_on_fault();
  return nullptr;
}

// `operator delete`.
void _ZdlPv(void) {
  granary_break_on_fault();
}

}  // extern C

#endif  // GRANARY_KERNEL_LIBCXX_CC_
