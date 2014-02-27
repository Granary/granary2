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
      : m1(nullptr),
        m2(nullptr) {
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
  ASSERT_TRUE(nullptr != m2.FindByName(GRANARY_NAME_STRING));
}

TEST_F(ModuleManagerTest, WithBuiltinFindsLibC) {
  ASSERT_TRUE(nullptr != m2.FindByName("c"));
}

TEST_F(ModuleManagerTest, WithBuiltinFindsPthreads) {
  ASSERT_TRUE(nullptr != m2.FindByName("pthread"));
}

TEST_F(ModuleManagerTest, WithBuiltinFindsLibDL) {
  ASSERT_TRUE(nullptr != m2.FindByName("dl"));
}

class ModuleRangeTest : public ::testing::Test {
 protected:
  ModuleRangeTest(void)
      : kern(ModuleKind::KERNEL, "kernel"),
        mod(ModuleKind::KERNEL_MODULE, GRANARY_NAME_STRING) {
    mod.AddRange(100, 200, 0, 0);
  }

  Module kern;
  Module mod;
};

TEST_F(ModuleRangeTest, Foo) { }
