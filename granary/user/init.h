/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_USER_INIT_H_
#define GRANARY_USER_INIT_H_

#include "granary/base/base.h"


#define GRANARY_INIT(tool_name, ...) \
  namespace { \
    __attribute__((used, constructor(102))) \
    static void GRANARY_CAT(Init, tool_name)(void) { \
      GRANARY_USING_NAMESPACE granary; \
      __VA_ARGS__ \
    } \
  }

#endif  // GRANARY_USER_INIT_H_
