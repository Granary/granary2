/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/cast.h"
#include "granary/driver/driver.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"

#include "granary/breakpoint.h"
#include "granary/decoder.h"
#include "granary/environment.h"

#if GRANARY_STANDALONE

namespace granary {

static void test(void) {

  Environment env;
  ControlFlowGraph cfg;
  InstructionDecoder decoder(&env, &cfg);

  auto start_pc = UnsafeCast<AppProgramCounter>(&test);

  decoder.DecodeBasicBlock(start_pc, nullptr);
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

