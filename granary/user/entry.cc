/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"

#include "granary/breakpoint.h"
#include "granary/driver.h"
#include "granary/environment.h"

#if GRANARY_STANDALONE

namespace granary {

static void test(void) {
  auto start_pc = UnsafeCast<AppProgramCounter>(&test);

  Environment env;
  ControlFlowGraph cfg(&env, start_pc);

}

}  // namespace granary

extern "C" {

int main(int argc, const char *argv[]) {

  granary::driver::Init();
  (void) argc;
  (void) argv;

  granary::test();

  return 0;
}

}  // extern C

#else

#endif

