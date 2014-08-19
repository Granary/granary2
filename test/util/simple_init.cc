/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"

#include "test/util/simple_init.h"

#include "arch/init.h"

#include "granary/init.h"

#include "os/memory.h"
#include "os/module.h"

void SimpleInitGranary(void) {
  using namespace granary;
  PreInit();
  os::InitHeap();
  os::InitModuleManager();
  arch::Init();
}
