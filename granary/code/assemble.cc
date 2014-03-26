/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

#include "granary/code/assemble.h"

// Stages of assembly.
#include "granary/code/assemble/1_relativize.h"
#include "granary/code/assemble/2_build_fragment_list.h"
#include "granary/code/assemble/3_find_live_arch_registers.h"
#include "granary/code/assemble/4_partition_fragments.h"
#include "granary/code/assemble/9_log_fragments.h"

#include "granary/logging.h"
#include "granary/util.h"

GRANARY_DEFINE_bool(debug_log_assembled_fragments, false,
    "Log the assembled fragments before doing final linking. The default is "
    "false.")

namespace granary {
namespace {

#if 0
// Info tracker about an individual virtual register.
class VirtualRegisterInfo {
 public:
  /*
  bool used_after_change_sp:1;
  bool used_after_change_ip:1;
  bool defines_constant:1;
  bool used_as_address:1;
  bool depends_on_sp:1;
  */

  unsigned num_defs;
  unsigned num_uses;

  // This is fairly rough constraint. This only really meaningful for introduced
  // `LEA` instructions that defined virtual registers as a combination of
  // several other non-virtual registers.
  RegisterUsageTracker depends_on;

} __attribute__((packed));

// Table that records all info about virtual register usage.
class VirtualRegisterTable {
 public:
  void Visit(NativeInstruction *instr) {
    instr->ForEachOperand([] (Operand *op) {
      const auto reg_op = DynamicCast<const RegisterOperand *>(op);
      if (!reg_op || !reg_op->IsVirtual()) {
        return;
      }
      const auto reg = reg_op->Register();
      auto &reg_info(regs[reg.Number()]);
      if (reg_op->IsRead() || reg_op->IsConditionalWrite()) {
        ++reg_info.num_uses;
      }
      if (reg_op->IsWrite()) {
        ++reg_info.num_defs;
      }
    });
  }

 private:
  BigVector<VirtualRegisterInfo> regs;
};
#endif

}  // namespace

// Assemble the local control-flow graph.
void Assemble(ContextInterface* env, CodeCacheInterface *code_cache,
              LocalControlFlowGraph *cfg) {

  // "Fix" instructions that might use PC-relative operands that are now too
  // far away from their original data/targets (e.g. if the code cache is really
  // far away from the original native code in memory).
  RelativizeLCFG(code_cache, cfg);

  // Split the LCFG into fragments. The relativization step might introduce its
  // own control flow, as well as instrumentation tools. This means that
  // `DecodedBasicBlock`s no longer represent "true" basic blocks because they
  // can contain internal control-flow. This makes further analysis more
  // complicated, so to simplify things we re-split up the blocks into fragments
  // that represent the "true" basic blocks.
  auto frags = BuildFragmentList(cfg);

  // Find the live registers on entry to the fragments.
  FindLiveEntryRegsToFrags(frags);

  // Try to figure out the stack frame size on entry to / exit from every
  // fragment.
  PartitionFragmentsByStackUse(frags);

  if (FLAG_debug_log_assembled_fragments) {
    Log(LogDebug, frags);
  }

  GRANARY_UNUSED(env);
}

}  // namespace granary
