/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gtest/gtest.h>

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/cast.h"

#include "granary/module.h"

using namespace granary;

class ModuleManagerTest : public ::testing::Test {
 protected:
  ModuleManagerTest(void)
      : m1(),
        m2() {
    m2.RegisterAllBuiltIn();
  }

  ModuleManager m1;
  ModuleManager m2;
};

TEST_F(ModuleManagerTest, EmptyDoesNotFindLibC) {
  ASSERT_TRUE(nullptr == m1.FindByName("c"));
}

TEST_F(ModuleManagerTest, EmptyDoesNotHaveExit) {
  ASSERT_TRUE(nullptr == m1.FindByPC(UnsafeCast<AppPC>(exit)));
}

TEST_F(ModuleManagerTest, WithBuiltinFindsGranary) {
  ASSERT_TRUE(nullptr != m2.FindByName(GRANARY_TO_STRING(GRANARY_NAME)));
}

TEST_F(ModuleManagerTest, WithBuiltinFindsLibC) {
  auto libc = m2.FindByName("c");
}

TEST_F(ModuleManagerTest, WithBuiltinFindsPthreads) {
  ASSERT_TRUE(nullptr != m2.FindByName("pthread"));
}

TEST_F(ModuleManagerTest, WithBuiltinFindsLibDL) {
  ASSERT_TRUE(nullptr != m2.FindByName("dl"));
}

class ModuleTest : public ::testing::Test {
 protected:
  ModuleTest(void) {}

  Module m1;
  Module m2;
};
