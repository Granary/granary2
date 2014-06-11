/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "granary/arch/driver.h"

#include "granary/base/cast.h"

extern "C" {
  extern void TestDecode_Instructions(void);
  extern void TestDecode_Instructions_End(void);
}

TEST(EncodeTest, EncodeCommonInstructions) {
  using namespace granary;
  arch::Init();

  auto begin = UnsafeCast<AppPC>(TestDecode_Instructions);
  auto end = UnsafeCast<AppPC>(TestDecode_Instructions_End);

  arch::InstructionDecoder decoder;
  arch::InstructionEncoder staged_encoder(arch::InstructionEncodeKind::STAGED);
  arch::InstructionEncoder commit_encoder(arch::InstructionEncodeKind::COMMIT);
  arch::Instruction instr;

  while (begin < end) {
    auto ret = decoder.DecodeNext(&instr, &begin);
    if (!ret) break;

    uint8_t mem[XED_MAX_INSTRUCTION_BYTES] = {0};
    ret = staged_encoder.Encode(&instr, &(mem[0]));
    EXPECT_TRUE(ret);
    if (!ret) break;

    ret = commit_encoder.Encode(&instr, &(mem[0]));
    EXPECT_TRUE(ret);
    if (!ret) break;
  }
}
