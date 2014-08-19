/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gmock/gmock.h>
#include <cstdio>

#define GRANARY_INTERNAL

#include "test/util/simple_init.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"

#include "granary/context.h"
#include "granary/tool.h"
#include "granary/translate.h"

using namespace granary;
using namespace testing;

class VariadicArgsTest : public Test {
 public:
  virtual ~VariadicArgsTest(void) = default;
  VariadicArgsTest(void)
      : context() {}

  static void SetUpTestCase(void) {
    SimpleInitGranary();
  }
 protected:
  Context context;
};

static int va_sum(int n, ...) {
  va_list ls;
  va_start(ls, n);
  auto total = 0;
  for (auto i = 0; i < n; ++i) {
    total += va_arg(ls, int);
  }
  return total;
}

static int va_sum_list(int n, va_list ls) {
  auto total = 0;
  for (auto i = 0; i < n; ++i) {
    total += va_arg(ls, int);
  }
  return total;
}

static int va_sum2(int n, ...) {
  va_list ls;
  va_start(ls, n);
  return va_sum_list(n, ls);
}

static auto va_summer = va_sum;

static int sum_0(void) {
  return va_summer(0);
}

static int sum_1_0(void) {
  return va_summer(1, 0);
}

static int sum_1_1(void) {
  return va_summer(1, 1);
}

static int sum_1_10(void) {
  return va_summer(1, 10);
}

static int sum_3_3_3_3(void) {
  return va_summer(3, 3, 3, 3);
}

// Hopefully this will go through the PLT and GOT.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-extra-args"
__attribute__((noinline))
static int do_fprintf(void) {
  asm("":::"memory");
  return fprintf(fopen("/dev/null", "w"),
                 "%f%f%f%f%f-%f",
                 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);  // Extra args!!!
}
#pragma clang diagnostic pop

TEST_F(VariadicArgsTest, TestDirectVariadic) {
  auto inst_va_sum = Translate(&context, va_sum);
  auto va_summer = UnsafeCast<int(*)(...)>(inst_va_sum);
  EXPECT_EQ(0, va_summer(0));
  EXPECT_EQ(0, va_summer(1, 0));
  EXPECT_EQ(10, va_summer(1, 10));
  EXPECT_EQ(10, va_summer(2, 10, 0));
  EXPECT_EQ(10, va_summer(2, 0, 10));
  EXPECT_EQ(9, va_summer(3, 3, 3, 3));
  EXPECT_EQ(12, va_summer(4, 3, 3, 3, 3));
  EXPECT_EQ(12, va_summer(4, 3, 3, 3, 3, 3));  // Passes extra args!
  EXPECT_EQ(15, va_summer(5, 3, 3, 3, 3, 3));
  EXPECT_EQ(18, va_summer(6, 3, 3, 3, 3, 3, 3));
}

TEST_F(VariadicArgsTest, TestDirectRecursiveVariadic) {
  auto inst_va_sum = Translate(&context, va_sum2);
  auto va_summer = UnsafeCast<int(*)(...)>(inst_va_sum);
  EXPECT_EQ(0, va_summer(0));
  EXPECT_EQ(0, va_summer(1, 0));
  EXPECT_EQ(10, va_summer(1, 10));
  EXPECT_EQ(10, va_summer(2, 10, 0));
  EXPECT_EQ(10, va_summer(2, 0, 10));
  EXPECT_EQ(9, va_summer(3, 3, 3, 3));
  EXPECT_EQ(12, va_summer(4, 3, 3, 3, 3));
  EXPECT_EQ(12, va_summer(4, 3, 3, 3, 3, 3));  // Passes extra args!
  EXPECT_EQ(15, va_summer(5, 3, 3, 3, 3, 3));
  EXPECT_EQ(18, va_summer(6, 3, 3, 3, 3, 3, 3));
}

TEST_F(VariadicArgsTest, TestIndirectVariadic) {
  auto summers = {sum_0, sum_1_0, sum_1_1, sum_1_10, sum_3_3_3_3};
  for (auto summer : summers) {
    auto inst_summer = Translate(&context, summer);
    auto va_summer = UnsafeCast<int(*)(void)>(inst_summer);
    auto expected_result = summer();
    EXPECT_EQ(expected_result, va_summer());
  }
}

TEST_F(VariadicArgsTest, TestPLTAndGOT) {
  auto inst_do_fprintf = Translate(&context, do_fprintf);
  auto inst_fprintf = UnsafeCast<int(*)(void)>(inst_do_fprintf);
  EXPECT_EQ(49, inst_fprintf());
}
