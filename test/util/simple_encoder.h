/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef TEST_UTIL_SIMPLE_ENCODER_H_
#define TEST_UTIL_SIMPLE_ENCODER_H_

#include <gtest/gtest.h>

#include "granary/arch/init.h"

#include "granary/metadata.h"
#include "granary/module.h"

#include "test/context.h"
#include "test/index.h"

// Test fixture that can be used for simple instrumenting and encoding test
// cases.
//
// This should be used for test cases where there is no internal control flow
// to the code being instrumented, and the code ends in a function return.
class SimpleEncoderTest : public testing::Test {
 public:
  SimpleEncoderTest(void);
  virtual ~SimpleEncoderTest(void) = default;

  template <typename R, typename... Args>
  R (*InstrumentAndEncode(R (*native_pc)(Args...)))(Args...) {
    auto pc = granary::UnsafeCast<granary::AppPC>(native_pc);
    auto encoded_pc = InstrumentAndEncode(pc);
    return granary::UnsafeCast<R (*)(Args...)>(encoded_pc);
  }

 protected:
  granary::BlockMetaData *AllocateMeta(granary::AppPC pc);
  granary::CachePC InstrumentAndEncode(granary::AppPC pc);

  MockContext context;
  granary::Module module;
  granary::Module code_cache_mod;
  granary::Module edge_cache_mod;
  granary::CodeCache code_cache;
  granary::CodeCache edge_cache;
  granary::MetaDataManager meta_manager;
  MockIndex *index;
  granary::LockedIndex locked_index;
};

#endif  // TEST_UTIL_SIMPLE_ENCODER_H_
