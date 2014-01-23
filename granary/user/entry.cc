/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <cstdio>

#include "granary/base/cast.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"

#include "granary/breakpoint.h"
#include "granary/driver.h"
#include "granary/environment.h"

#if GRANARY_STANDALONE

namespace granary {

static void Instrument(ControlFlowGraph *cfg) {
  for (auto block : cfg->Blocks()) {
    printf("BB %p:\n", block->app_start_pc);
    for (auto succ : block->Successors()) {
      printf(" -> %p\n", succ.block->app_start_pc);
    }
  }
}

static void Test(void) {
  auto start_pc = UnsafeCast<AppProgramCounter>(&Test);

  Environment env;
  ControlFlowGraph cfg(&env, start_pc);
  Instrument(&cfg);
}

}  // namespace granary

extern "C" {

int main(int argc, const char *argv[]) {

  granary::driver::Init();
  (void) argc;
  (void) argv;

  granary::Test();

  return 0;
}

}  // extern C

#else

#endif

