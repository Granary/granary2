/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/driver/init.h"

#if GRANARY_STANDALONE

extern "C" {

int main(int argc, const char *argv[]) {
  granary::driver::Init();
  (void) argc;
  (void) argv;
  return 0;
}

}  // extern C

#else

#endif

