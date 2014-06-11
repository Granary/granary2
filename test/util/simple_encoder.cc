/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "test/util/simple_encoder.h"

#include "granary/cfg/control_flow_graph.h"

#include "granary/code/compile.h"
#include "granary/code/metadata.h"

#include "granary/instrument.h"
#include "granary/util.h"

SimpleEncoderTest::SimpleEncoderTest(void)
    : module(granary::ModuleKind::GRANARY, "granary") {
  meta_manager.Register<granary::ModuleMetaData>();
  meta_manager.Register<granary::CacheMetaData>();
  meta_manager.Register<granary::LiveRegisterMetaData>();
  meta_manager.Register<granary::StackMetaData>();
  granary::arch::Init();

  // Called for the "lazy" meta-data on the function return.
  EXPECT_CALL(context, AllocateCodeCache())
      .Times(1)
      .WillOnce(testing::Return(&code_cache));

  module.SetContext(&context);
  module.AddRange(0, ~0ULL, 0, granary::MODULE_EXECUTABLE);
}

SimpleEncoderTest::~SimpleEncoderTest(void) {
  // Called for the "lazy" meta-data on the function return.
  EXPECT_CALL(context, FlushCodeCache(&code_cache))
      .Times(1);
}

granary::BlockMetaData *SimpleEncoderTest::AllocateMeta(granary::AppPC pc) {
  auto meta = meta_manager.Allocate();
  auto module_meta = granary::MetaDataCast<granary::ModuleMetaData *>(meta);
  module_meta->start_pc = pc;
  module_meta->source.module = &module;
  module_meta->source.offset = 0;
  return meta;
}

granary::CachePC SimpleEncoderTest::InstrumentAndEncode(granary::AppPC pc) {
  using namespace granary;
  LocalControlFlowGraph cfg(&context);

  auto meta = AllocateMeta(pc);

  // Called for the "lazy" meta-data on the function return.
  EXPECT_CALL(context, AllocateEmptyBlockMetaData())
      .Times(1)
      .WillOnce(testing::InvokeWithoutArgs([&] {
        return meta_manager.Allocate();
      }));

  // Allocate all tools to instrument the first block.
  EXPECT_CALL(context, AllocateTools())
      .Times(1)
      .WillOnce(testing::Return(nullptr));

  // Free all tools after instrumenting the LCFG.
  EXPECT_CALL(context, FreeTools(nullptr))
      .Times(1);

  Instrument(&context, &cfg, meta);
  Compile(&cfg, &edge_cache);

  auto block = cfg.EntryBlock();
  auto cache_meta = GetMetaData<CacheMetaData>(block);

  return cache_meta->cache_pc;
}
