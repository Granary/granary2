/* Copyright 2014 Peter Goodman, all rights reserved. */
#if 0
#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

// Append a non-native, created instruction to the fragment.
#define APP(...) \
  do { \
    __VA_ARGS__; \
    auto ninstr = new NativeInstruction(&ni); \
    frag->AppendInstruction(std::unique_ptr<Instruction>(ninstr)); \
  } while (0)

#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/instruction.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/fragment.h"

namespace granary {
namespace arch {

// Table mapping each iclass to the set of read and written flags by *any*
// selection of that iclass.
extern const FlagsSet ICLASS_FLAGS[];

}  // namespace arch

// Visits all native instructions within the fragment and kills any flags that
// those instructions kill. This does not revive any flags.
//
// Note: This has an architecture-specific implementation.
void KillFragmentFlags(Fragment * const frag) {
  for (auto instr : ForwardInstructionIterator(frag->first)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      auto iclass = ninstr->instruction.iclass;
      auto &flags(arch::ICLASS_FLAGS[iclass]);
      frag->inst_killed_flags |= flags.written.flat;
    }
  }
}

// Visits a native instructions within the fragment and revives/kills
// flags.
//
// Note: This has an architecture-specific implementation.
uint32_t VisitInstructionFlags(const NativeInstruction *instr,
                               uint32_t in_flags) {
  if (instr) {
    auto &flags(arch::ICLASS_FLAGS[instr->instruction.iclass]);
    return (in_flags & ~flags.written.flat) | flags.read.flat;
  } else {
    return in_flags;
  }
}

namespace {

// Get the designated register for saving/restoring flags state for a given
// zone/region of instrumentation code. We steal the meaning the of the `id`
// field of `Fragment`.
VirtualRegister GetFlagRegister(LocalControlFlowGraph *cfg, Fragment *frag) {
  auto bl = frag->cached_back_link;
  if (!bl->flag_save_reg.IsValid()) {
    bl->flag_save_reg = cfg->AllocateVirtualRegister(arch::GPR_WIDTH_BYTES);
  }
  return bl->flag_save_reg;
}

}  // namespace

// Inserts instructions that saves the flags within the fragment `frag`.
void InjectSaveFlags(LocalControlFlowGraph *cfg, Fragment *frag) {
  arch::Instruction ni;
  auto reg = GetFlagRegister(cfg, frag);
  APP(MOV_GPRv_GPRv_89(&ni, reg, XED_REG_RAX));
  APP(LAHF(&ni));
  xed_flag_set_t flags;
  flags.flat = frag->app_live_flags & frag->inst_killed_flags;
  if (flags.s.of) {
    APP(SETO_GPR8(&ni, XED_REG_AL));
  }
}

// Inserts instructions that restore the flags within the fragment `frag`.
//
// Note: This has an architecture-specific implementation.
void InjectRestoreFlags(LocalControlFlowGraph *cfg, Fragment *frag) {
  arch::Instruction ni;
  auto reg = GetFlagRegister(cfg, frag);
  xed_flag_set_t flags;
  flags.flat = frag->app_live_flags & frag->inst_killed_flags;
  if (flags.s.of) {
    APP(ADD_GPR8_IMMb_80r0(&ni, XED_REG_AL, 0x7F));
  }
  APP(SAHF(&ni));
  APP(MOV_GPRv_GPRv_89(&ni, XED_REG_RAX, reg));
}

}  // namespace granary
#endif
