/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/string.h"

#include "granary/module.h"

#include "test/code/cache.h"
#include "test/context.h"

using namespace granary;
using namespace ::testing;

class ModuleManagerTest : public Test {
 protected:
  ModuleManagerTest(void)
      : m1(nullptr),
        m2(nullptr),
        mod(ModuleKind::KERNEL_MODULE, GRANARY_NAME_STRING) {
    m2.RegisterAllBuiltIn();
  }

  ModuleManager m1;
  ModuleManager m2;
  Module mod;
};

TEST_F(ModuleManagerTest, EmptyDoesNotFindLibC) {
  ASSERT_TRUE(nullptr == m1.FindByName("c"));
}

TEST_F(ModuleManagerTest, EmptyDoesNotHaveExit) {
  ASSERT_TRUE(nullptr == m1.FindByAppPC(UnsafeCast<AppPC>(exit)));
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

TEST_F(ModuleManagerTest, FindRegisteredModule) {
  m1.Register(&mod);
  ASSERT_TRUE(nullptr != m1.FindByName(GRANARY_NAME_STRING));
}

TEST_F(ModuleManagerTest, FindRegisteredModulePC) {
  m1.Register(&mod);
  mod.AddRange(100, 200, 0, 0);
  for (auto addr = 0UL; addr < 300; ++addr) {
    if (100 <= addr && 200 > addr) {
      EXPECT_EQ(&mod, m1.FindByAppPC(UnsafeCast<AppPC>(addr)));
    } else {
      EXPECT_TRUE(nullptr == m1.FindByAppPC(UnsafeCast<AppPC>(addr)));
    }
  }
}

class ModuleTest : public Test {
 protected:
  ModuleTest(void)
      : mod(ModuleKind::KERNEL_MODULE, GRANARY_NAME_STRING) {}
  Module mod;
};

TEST_F(ModuleTest, DoesNotContainNullptr) {
  ASSERT_FALSE(mod.Contains(nullptr));
}

TEST_F(ModuleTest, ReturnsInvalidOffset) {
  auto offset = mod.OffsetOf(nullptr);
  EXPECT_TRUE(nullptr == offset.module);
  EXPECT_TRUE(0 == offset.offset);
}

TEST_F(ModuleTest, HasInitializedKind) {
  ASSERT_TRUE(ModuleKind::KERNEL_MODULE == mod.Kind());
}

TEST_F(ModuleTest, HasInitializedName) {
  ASSERT_TRUE(StringsMatch(GRANARY_NAME_STRING, mod.Name()));
}

class ModuleRangeTest : public Test {
 protected:
  ModuleRangeTest(void)
      : mod(ModuleKind::KERNEL_MODULE, GRANARY_NAME_STRING) {
    mod.AddRange(100, 200, 0, 0);
  }

  void TestPCMembership(void) {
    for (auto addr = 0UL; addr < 300UL; ++addr) {
      if (100 > addr || 200 <= addr) {
        EXPECT_FALSE(mod.Contains(UnsafeCast<AppPC>(addr)));
      } else {
        EXPECT_TRUE(mod.Contains(UnsafeCast<AppPC>(addr)));
      }
    }
  }

  void TestOffsetsInRange(void) {
    uintptr_t addr_offset(0);
    for (auto addr = 100UL; addr < 200UL; ++addr) {
      auto offset = mod.OffsetOf(UnsafeCast<AppPC>(addr));
      EXPECT_EQ(&mod, offset.module);
      EXPECT_EQ(addr_offset, offset.offset);
      ++addr_offset;
    }
  }

  Module mod;
};

TEST_F(ModuleRangeTest, PCsInAndOutOfRange) {
  TestPCMembership();
}

TEST_F(ModuleRangeTest, OffsetsOfPCsInRangeMatch) {
  TestOffsetsInRange();
}

// Incrementally the range [100, 200) into many small ranges that cover
// the same larger range. The range is split from the left-hand side.
//      [100,101), [101,102), ..., [199,200)
// and test that PC membership within the ranges continue to work.
TEST_F(ModuleRangeTest, SplitRangeLHS) {
  uintptr_t addr_offset(0);
  for (auto addr = 100UL; addr < 200UL; ++addr) {
    mod.AddRange(addr, addr + 1, addr_offset++, 0);
    TestPCMembership();
  }
  TestPCMembership();
  TestOffsetsInRange();
}

// Incrementally the range [100, 200) into many small ranges that cover
// the same larger range. The range is split from the right-hand side.
//      [100,101), [101,102), ..., [199,200)
// and test that PC membership within the ranges continue to work.
TEST_F(ModuleRangeTest, SplitRangeRHS) {
  uintptr_t addr_offset(100);
  for (auto addr = 200UL; addr > 100UL; --addr) {
    mod.AddRange(addr - 1, addr, --addr_offset, 0);
    TestPCMembership();
  }
  TestPCMembership();
  TestOffsetsInRange();
}

// Split the range into three sub-ranges:
//    [100,125),[125,175),[175,200)
TEST_F(ModuleRangeTest, SplitRangeMid) {
  mod.AddRange(125, 175, 25, 0);
  TestPCMembership();
  TestOffsetsInRange();
}

// Split the range into two sub-ranges by removing a middle range:
//    [100,125),[125,175),[175,200)
TEST_F(ModuleRangeTest, RemoveRangeMid) {
  mod.RemoveRange(125, 175);
  for (auto addr = 0UL; addr < 300UL; ++addr) {
    if ((100 <= addr && 125 > addr) ||
        (175 <= addr && 200 > addr)) {
      EXPECT_TRUE(mod.Contains(UnsafeCast<AppPC>(addr)));
    } else {
      EXPECT_FALSE(mod.Contains(UnsafeCast<AppPC>(addr)));
    }
  }
}

// Split two contiguous ranges [100,150) and [150,200) into three sub-ranges
// [100,125), [125,175), and [175,200).
TEST_F(ModuleRangeTest, SplitRangeCross) {
  mod.AddRange(100, 150, 0, 0);
  mod.AddRange(150, 200, 50, 0);
  TestPCMembership();
  TestOffsetsInRange();

  mod.AddRange(125, 175, 25, 0);
  TestPCMembership();
  TestOffsetsInRange();
}

class ModuleCodeCacheTest : public Test {
 protected:
  ModuleCodeCacheTest(void)
      : mod(ModuleKind::KERNEL_MODULE, GRANARY_NAME_STRING) {
    mod.SetContext(&context);
  }

  Module mod;
  MockContext context;
  MockCodeCache code_cache1;
  MockCodeCache code_cache2;
  MockCodeCache code_cache3;
  MockCodeCache code_cache4;
  MockCodeCache code_cache5;
};

// Test that we detect that splitting a range flushes code caches and creates
// new ones.
TEST_F(ModuleCodeCacheTest, AddAndRemoveRange) {
  EXPECT_CALL(context, AllocateCodeCache())
      .Times(1)
      .WillOnce(Return(&code_cache1));
  mod.AddRange(100, 200, 0, 0);
  EXPECT_CALL(context, FlushCodeCache(&code_cache1))
      .Times(1);
  mod.RemoveRange(100, 200);
}

// Test that splitting a range into three sub ranges ends up allocating one
// code cache, then flushing it, and allocating three more. Then we'll remove
// all three sub-ranges at once, which should flush the three new code caches.
TEST_F(ModuleCodeCacheTest, SplitRangeMid) {
  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache1));
  mod.AddRange(100, 200, 0, 0);

  EXPECT_CALL(context, AllocateCodeCache())
      .Times(3)
      .WillOnce(Return(&code_cache3))  // Allocate range [125,175).
      .WillOnce(Return(&code_cache4))  // Add range [175,200).
      .WillOnce(Return(&code_cache2));  // Replenish range [100,125).
  EXPECT_CALL(context, FlushCodeCache(&code_cache1))
      .Times(1);  // Flush the range [100,200).

  mod.AddRange(125, 175, 25, 0);
  EXPECT_CALL(context, FlushCodeCache(&code_cache2)).Times(1);
  EXPECT_CALL(context, FlushCodeCache(&code_cache3)).Times(1);
  EXPECT_CALL(context, FlushCodeCache(&code_cache4)).Times(1);
  mod.RemoveRange(100, 200);
}

// Test that removing the middle of a range splits it into two sub-ranges, with
// appropriately new code cache allocators.
TEST_F(ModuleCodeCacheTest, RemoveRangeMid) {
  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache1));
  mod.AddRange(100, 200, 0, 0);

  EXPECT_CALL(context, AllocateCodeCache())
      .Times(2)
      .WillOnce(Return(&code_cache3))  // Add range [175,200).
      .WillOnce(Return(&code_cache2));  // Replenish range [100,125).
  EXPECT_CALL(context, FlushCodeCache(&code_cache1))
      .Times(1);  // Flush the range [100,200).

  mod.RemoveRange(125, 175);
  EXPECT_CALL(context, FlushCodeCache(&code_cache2)).Times(1);
  EXPECT_CALL(context, FlushCodeCache(&code_cache3)).Times(1);
  mod.RemoveRange(100, 200);
}

// Test that splitting two contiguous ranges into three contiguous ranges
// results in the expected changes to code caches.
TEST_F(ModuleCodeCacheTest, SplitRangeCross) {
  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache1));
  mod.AddRange(100, 150, 0, 0);

  EXPECT_CALL(context, AllocateCodeCache())
      .Times(1)
      .WillOnce(Return(&code_cache2));
  mod.AddRange(150, 200, 50, 0);

  EXPECT_CALL(context, AllocateCodeCache())
      .Times(3)
      .WillOnce(Return(&code_cache4))  // Allocate range [125,175).
      .WillOnce(Return(&code_cache3))  // Replenish [100,125).
      .WillOnce(Return(&code_cache5));  // Replenish range [175,200).

  EXPECT_CALL(context, FlushCodeCache(&code_cache1))
      .Times(1);  // Flush the range [100,150).
  EXPECT_CALL(context, FlushCodeCache(&code_cache2))
      .Times(1);  // Flush the range [150,200).
  mod.AddRange(125, 175, 25, 0);

  EXPECT_CALL(context, FlushCodeCache(&code_cache3)).Times(1);
  EXPECT_CALL(context, FlushCodeCache(&code_cache4)).Times(1);
  EXPECT_CALL(context, FlushCodeCache(&code_cache5)).Times(1);
  mod.RemoveRange(100, 200);
}

// Test that adding three contiguous ranges does not cause code caches to be
// flushed.
TEST_F(ModuleCodeCacheTest, ContiguousRanges) {
  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache1));
  mod.AddRange(100, 200, 0, 0);

  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache2));
  mod.AddRange(200, 300, 0, 0);

  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache3));
  mod.AddRange(300, 400, 0, 0);

  EXPECT_CALL(context, FlushCodeCache(&code_cache1)).Times(1);
  EXPECT_CALL(context, FlushCodeCache(&code_cache2)).Times(1);
  EXPECT_CALL(context, FlushCodeCache(&code_cache3)).Times(1);
  mod.RemoveRange(100, 400);
}

// Test that adding three non-contiguous ranges does not cause code caches to be
// flushed.
TEST_F(ModuleCodeCacheTest, NonContiguousRanges) {
  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache1));
  mod.AddRange(100, 200, 0, 0);

  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache2));
  mod.AddRange(300, 400, 0, 0);

  EXPECT_CALL(context, AllocateCodeCache())
    .Times(1)
    .WillOnce(Return(&code_cache3));
  mod.AddRange(500, 600, 0, 0);

  EXPECT_CALL(context, FlushCodeCache(&code_cache1)).Times(1);
  mod.RemoveRange(100, 200);
  EXPECT_CALL(context, FlushCodeCache(&code_cache2)).Times(1);
  mod.RemoveRange(300, 400);
  EXPECT_CALL(context, FlushCodeCache(&code_cache3)).Times(1);
  mod.RemoveRange(500, 600);

}
