/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_KERNEL_LIBCXX_CC_
#define GRANARY_KERNEL_LIBCXX_CC_


// Called inside of `__cxx_global_var_init`.
void __cxa_atexit(void) { }

__extension__ typedef int __guard __attribute__((mode(__DI__)));

// Called when initializing a static variable inside of a function. Treat
// `guard` as an atomic spin lock.
int __cxa_guard_acquire (__guard *g) {
  while (__sync_lock_test_and_set(g, 1)) { }
  return 1;
}

void __cxa_guard_release (__guard *g) {
  *g = 0;
}

void __cxa_guard_abort (__guard *) { }

#endif  // GRANARY_KERNEL_LIBCXX_CC_
