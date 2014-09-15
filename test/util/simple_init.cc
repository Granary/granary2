/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"
#include "granary/base/cstring.h"

#include "test/util/simple_init.h"

#include "arch/init.h"

#include "granary/init.h"

#include "os/memory.h"
#include "os/module.h"

extern "C" {
// Path to the loaded Granary library. Code cache `mmap`s are associated with
// this file.
extern char granary_mmap_path[];
}  // extern C

void SimpleInitGranary(void) {
  using namespace granary;
  strcpy(granary_mmap_path, "/dev/zero");
  PreInit();
  os::InitHeap();
  os::InitModuleManager();
  arch::Init();
}
