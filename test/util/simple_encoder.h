/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef TEST_UTIL_SIMPLE_ENCODER_H_
#define TEST_UTIL_SIMPLE_ENCODER_H_

#include <gtest/gtest.h>

#include "arch/init.h"

#include "granary/base/cast.h"
#include "granary/base/pc.h"

#include "granary/context.h"
#include "granary/translate.h"

// Test fixture that can be used for simple instrumenting and encoding test
// cases.
//
// This should be used for test cases where there is no internal control flow
// to the code being instrumented, and the code ends in a function return.
class SimpleEncoderTest : public testing::Test {
 public:
  SimpleEncoderTest(void);
  virtual ~SimpleEncoderTest(void) = default;

  static void SetUpTestCase(void);
  static void TearDownTestCase(void);

  granary::Context *context;
};

#endif  // TEST_UTIL_SIMPLE_ENCODER_H_
