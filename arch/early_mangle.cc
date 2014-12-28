/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/early_mangle.h"
#include "arch/x86-64/builder.h"
#include "arch/x86-64/instruction.h"

#include "granary/base/base.h"

#include "granary/cfg/block.h"
#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

EarlyMangler::EarlyMangler(DecodedBlock *block_)
    : reg_num(0),
      block(block_) {
  for (auto &reg : regs) {
    reg = block->AllocateVirtualRegister();
  }
}

VirtualRegister EarlyMangler::AllocateVirtualRegister(size_t num_bytes) {
  GRANARY_ASSERT(reg_num < kMaxNumVirtRegs);
  auto reg = regs[reg_num++];
  if (arch::GPR_WIDTH_BYTES != num_bytes) {
    reg.Widen(num_bytes);
  }
  return reg;
}

}  // namespace arch
}  // namespace granary
