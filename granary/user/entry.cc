/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/cast.h"
#include "granary/breakpoint.h"
#include "granary/decoder.h"
#include "granary/cfg/control_flow_graph.h"

#if GRANARY_STANDALONE

namespace granary {

static void test(void) {
  auto start_pc = UnsafeCast<AppProgramCounter>(&test);
  InstructionDecoder decoder;
  InFlightBasicBlock *block(new InFlightBasicBlock(start_pc, nullptr, nullptr));
  driver::InstructionDecoder decoder;
  driver::DecodedInstruction *instr(new driver::DecodedInstruction);
  unsigned char encoded_instr[32] = {0};
  decoder.Decode(instr, UnsafeCast<AppProgramCounter>(&test));
  decoder.Encode(instr, &(encoded_instr[0]));
  granary_break_on_encode(&(encoded_instr[0]));
  decoder.Decode(instr, &(encoded_instr[0]));
  decoder.Encode(instr, &(encoded_instr[0]));
  granary_break_on_encode(&(encoded_instr[0]));
  delete instr;

  InFlightBasicBlock *first_block(nullptr);
  ControlFlowGraph cfg(first_block);
  GRANARY_UNUSED(cfg);
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

