/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INIT_H_
#define GRANARY_INIT_H_

namespace granary {

#ifdef GRANARY_INTERNAL

enum InitKind {
  INIT_DYNAMIC,
  INIT_STATIC
};

// Initialize Granary.
void Init(InitKind kind, const char *granary_path);

#endif  // GRANARY_INTERNAL

#define GRANARY_INIT(...) \
  __attribute__((used, constructor(102))) \
  static void GRANARY_CAT(InitTool_GLOBAL__I_, GRANARY_UNIQUE_SYMBOL)(void) { \
    GRANARY_USING_NAMESPACE granary; \
    __VA_ARGS__ \
  } \

}  // namespace granary

#endif  // GRANARY_INIT_H_
