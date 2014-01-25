/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <cstdio>

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"
#include "granary/driver.h"
#include "granary/environment.h"

#include "granary/metadata.h"

#if GRANARY_STANDALONE

namespace granary {

struct FooMeta : public SerializableMetaData {
  void Hash(HashFunction *) const { }
  bool Equals(const FooMeta *x) const { return !x; }
};

struct StateMeta : public MutableMetaData {

};

static void Instrument(ControlFlowGraph *cfg) {
  auto foo_meta = detail::meta::GetInfo<FooMeta>();
  auto state_meta = detail::meta::GetInfo<StateMeta>();

  GRANARY_USED(foo_meta);
  GRANARY_USED(state_meta);

  printf("digraph {\n");
  for (auto block : cfg->Blocks()) {
    if (IsA<UnknownBasicBlock *>(block)) {
      continue;
    }
    for (auto succ : block->Successors()) {
      if (succ.cti->IsJump() && !succ.cti->HasIndirectTarget()) {
        cfg->Materialize(succ);
      }

      if (IsA<UnknownBasicBlock *>(succ.block)) {
        printf("bb%p -> indirect\n", block->app_start_pc);
      } else {
        printf("bb%p -> bb%p\n", block->app_start_pc, succ.block->app_start_pc);
      }
    }
  }
  printf("}\n");
}

static void Test(void) {
  auto start_pc = UnsafeCast<AppProgramCounter>(&Instrument);
  Environment env;
  ControlFlowGraph cfg(&env, start_pc);
  Instrument(&cfg);
}

}  // namespace granary

extern "C" {

int main(int argc, const char *argv[]) {
  granary::driver::Init();
  granary::Test();

  GRANARY_UNUSED(argc);
  GRANARY_UNUSED(argv);
  return 0;
}

}  // extern C

#else

#endif

