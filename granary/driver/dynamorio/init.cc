/* Copyright 2014 Peter Goodman, all rights reserved. */

extern "C" {
#include "dependencies/dynamorio/globals.h"
}

namespace granary {
namespace driver {

void Init(void) {
  proc_init();
}

}  // namespace driver
}  // namespace granary

