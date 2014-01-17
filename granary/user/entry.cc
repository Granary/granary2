/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/cast.h"
#include "granary/debug/breakpoint.h"
#include "granary/driver/driver.h"

#if GRANARY_STANDALONE

namespace granary {

static void test(void) {
  driver::InstructionDecoder decoder;
  driver::DecodedInstruction instr;
  unsigned char encoded_instr[32] = {0};
  decoder.Decode(&instr, UnsafeCast<AppProgramCounter>(&test));
  decoder.Encode(&instr, &(encoded_instr[0]));
  granary_break_on_encode(&(encoded_instr[0]));
  decoder.Decode(&instr, &(encoded_instr[0]));
  decoder.Encode(&instr, &(encoded_instr[0]));
  granary_break_on_encode(&(encoded_instr[0]));
}

}  // namespace granary

extern "C" {

int main(int argc, const char *argv[]) {

  granary::driver::Init();
  (void) argc;
  (void) argv;

  granary::test();

  return 0;
}

}  // extern C

#else

#endif

