/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INIT_H_
#define GRANARY_INIT_H_

namespace granary {

#ifdef GRANARY_INTERNAL

// Initialize Granary.
void Init(void);

#define GRANARY_INIT(...) \
  namespace { \
  __attribute__((used, constructor(998))) \
  static void InitGranary(void) { \
    GRANARY_USING_NAMESPACE granary; \
    __VA_ARGS__ \
  } \
  }

#endif  // GRANARY_INTERNAL
}  // namespace granary

#endif  // GRANARY_INIT_H_
