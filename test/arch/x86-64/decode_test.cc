/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL
#define GRANARY_TEST

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "arch/driver.h"

#include "granary/base/cast.h"

#include "granary/exit.h"
#include "granary/init.h"

extern "C" {
  extern void TestDecode_Instructions(void);
  extern void TestDecode_Instructions_End(void);
}

TEST(DecodeTest, DecodeCommonInstructions) {
  using namespace granary;
  Init(kInitAttach);

  auto begin = UnsafeCast<AppPC>(TestDecode_Instructions);
  auto end = UnsafeCast<AppPC>(TestDecode_Instructions_End);

  arch::Instruction instr;

  while (begin < end) {
    auto old_begin = begin;
    auto ret = arch::InstructionDecoder::DecodeNext(&instr, &begin);
    EXPECT_TRUE(ret);
    if (!ret) break;
    EXPECT_TRUE(old_begin < begin);
    if (old_begin >= begin) break;
    EXPECT_TRUE(XED_ICLASS_INVALID != instr.iclass);
    if (XED_ICLASS_INVALID == instr.iclass) break;
    EXPECT_TRUE(XED_IFORM_INVALID != instr.iform);
    if (XED_IFORM_INVALID == instr.iform) break;
  }

  Exit(kExitDetach);
}
