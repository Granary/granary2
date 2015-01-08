/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL
#define GRANARY_TEST

#include "test/util/simple_encoder.h"

#include "granary/cfg/trace.h"

#include "granary/code/compile.h"
#include "granary/code/edge.h"

#include "granary/context.h"
#include "granary/exit.h"
#include "granary/index.h"
#include "granary/init.h"
#include "granary/instrument.h"
#include "granary/util.h"

using namespace granary;
using namespace testing;

SimpleEncoderTest::SimpleEncoderTest(void)
    : context(GlobalContext()) {}

void SimpleEncoderTest::SetUpTestCase(void) {
  Init(kInitAttach);
}

void SimpleEncoderTest::TearDownTestCase(void) {
  Exit(kExitDetach);
}
