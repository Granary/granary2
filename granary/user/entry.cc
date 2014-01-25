/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <cstdio>

#include "granary/base/options.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"
#include "granary/driver.h"
#include "granary/environment.h"

#include "granary/metadata.h"

namespace granary {

GRANARY_DEFINE_string(tools, "")

static void Init(void) {
  granary::driver::Init();
  printf("--tools=%s\n", FLAG_tools);
}

}  // namespace granary

#ifdef GRANARY_STANDALONE

extern "C" {
int main(int argc, const char *argv[]) {
  granary::InitOptions(argc, argv);
  granary::Init();
  return 0;
}
}  // extern C
#else

__attribute__((constructor))
void granary_init(void) {
  granary::InitOptions(getenv("GRANARY_OPTIONS"));
  granary::Init();
}

#endif
