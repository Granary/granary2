/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/big_vector.h"
#include "granary/base/list.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/operand.h"

#include "granary/code/assemble.h"
#include "granary/code/fragment.h"
#include "granary/code/logging.h"
#include "granary/code/register.h"

#include "granary/driver/driver.h"

#include "granary/cache.h"
#include "granary/logging.h"
#include "granary/util.h"

namespace granary {

template <unsigned num_bits>
struct RelAddress;

// Relative address displacement suitable for x86 rel32 branches.
template <>
struct RelAddress<32> {
  enum : ptrdiff_t {
    // ~3.9 GB, close enough to 2^32 (4GB), but with a margin of error to
    // account fora bad estimate of `Relativizer::cache_pc`.
    MAX_OFFSET = 4187593113L
  };
};

// Relative address displacement suitable for ARM rel24 branches.
template <>
struct RelAddress<24> {
  enum : ptrdiff_t {
    // 15 MB, close enough to 2^24 (16MB), but with a margin of error to
    // account for a bad estimate of `Relativizer::cache_pc`.
    MAX_OFFSET = 15728640L
  };
};

// Manages simple relativization checks / tasks.
class InstructionRelativizer {
 public:
  explicit InstructionRelativizer(PC estimated_encode_loc)
      : cache_pc(estimated_encode_loc) {}

  inline bool AddressNeedsRelativizing(const void *ptr) const {
    return AddressNeedsRelativizing(reinterpret_cast<PC>(ptr));
  }

  // Returns true if an address needs relativizing.
  bool AddressNeedsRelativizing(PC relative_pc) const {
    auto signed_diff = relative_pc - cache_pc;
    auto diff = 0 > signed_diff ? -signed_diff : signed_diff;
    return MAX_BRANCH_OFFSET < diff;
  }

  // Relativize a particular memory operation within a memory instruction.
  void RelativizeMemOp(DecodedBasicBlock *block, NativeInstruction *instr,
                       const MemoryOperand &mloc) {
    const void *mptr(nullptr);
    if (mloc.MatchPointer(mptr) && AddressNeedsRelativizing(mptr)) {
      driver::RelativizeMemOp(block, instr, mloc, mptr);
    }
  }

  // Relativize a memory instruction.
  void RelativizeMemOp(DecodedBasicBlock *block, NativeInstruction *instr) {
    MemoryOperand mloc1;
    MemoryOperand mloc2;
    auto count = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                             ReadOrWriteTo(mloc2));
    if (2 == count) {
      RelativizeMemOp(block, instr, mloc1);
      RelativizeMemOp(block, instr, mloc2);
    } else if (1 == count) {
      RelativizeMemOp(block, instr, mloc1);
    }
  }

  // Relativize a control-flow instruction.
  void RelativizeCFI(DecodedBasicBlock *block, ControlFlowInstruction *cfi) {
    auto target_block = cfi->TargetBlock();
    if (IsA<NativeBasicBlock *>(target_block)) {
      auto target_pc = target_block->StartAppPC();

      // We always defer to arch-specific relativization because some
      // instructions need to be relativized regardless of whether or not the
      // target PC is far away. For example, on x86, the `LOOP rel8`
      // instructions must always be relativized.
      driver::RelativizeDirectCFI(cfi, &(cfi->instruction), target_pc,
                                  AddressNeedsRelativizing(target_pc));

    // Indirect CFIs might read their target from a PC-relative address.
    } else if (IsA<IndirectBasicBlock *>(target_block)) {
      MemoryOperand mloc;
      if (cfi->MatchOperands(ReadFrom(mloc))) {
        RelativizeMemOp(block, cfi, mloc);
      }
    }
  }

  // Relativizes an individual instruction by replacing addresses that are too
  // far away with ones that use virtual registers or other mechanisms. This is
  // the "easy" side of things, where the virtual register system needs to do
  // the "hard" part of actually making register usage reasonable.
  void RelativizeInstruction(DecodedBasicBlock *block,
                             NativeInstruction *instr) {
    if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
      RelativizeCFI(block, cfi);
    } else {
      RelativizeMemOp(block, instr);
    }
  }

  // Relativizes instructions that use PC-relative operands that are too far
  // away from our estimate of where this block will be encoded.
  void RelativizeBlock(DecodedBasicBlock *block) {
    for (auto instr : block->Instructions()) {
      if (auto native_instr = DynamicCast<NativeInstruction *>(instr)) {
        RelativizeInstruction(block, native_instr);
      }
    }
  }

 private:
  InstructionRelativizer(void) = delete;

  enum : ptrdiff_t {
    MAX_BRANCH_OFFSET = RelAddress<arch::REL_BRANCH_WIDTH_BITS>::MAX_OFFSET
  };

  PC cache_pc;
};

namespace {

// Relativize the native instructions within a LCFG.
static void RelativizeLCFG(CodeCacheInterface *code_cache,
                           LocalControlFlowGraph* cfg) {
  auto estimated_encode_loc = code_cache->AllocateBlock(0);
  InstructionRelativizer rel(estimated_encode_loc);
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      rel.RelativizeBlock(decoded_block);
    }
  }
}

// Update a register usage set with another fragment. Returns true if we
// expect to find any local changes in our current fragment's register
// liveness set based on the successor having a change in the last data flow
// iteration.
static bool UpdateRegUsageFromSuccessor(Fragment *succ,
                                        RegisterUsageTracker *regs) {
  if (succ) {
    regs->Union(succ->entry_regs_live);
    return succ->data_flow_changed;
  } else {
    return false;
  }
}

// Calculate the live registers on entry to a fragment.
static void FindLiveEntryRegsToFrag(Fragment *frag,
                                    RegisterUsageTracker *regs) {
  for (auto instr : BackwardInstructionIterator(frag->last)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      regs->Visit(ninstr);
    }
  }
}

// Calculate the live registers on entry to every fragment.
static void FindLiveEntryRegsToFrags(Fragment *frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (frag->is_exit || frag->is_future_block_head) {
      frag->entry_regs_live.ReviveAll();
      frag->data_flow_changed = false;
    } else {
      frag->entry_regs_live.KillAll();
      frag->data_flow_changed = true;
    }
  }

  for (bool data_flow_changed = true; data_flow_changed; ) {
    data_flow_changed = false;

    for (auto frag : FragmentIterator(frags)) {
      if (frag->is_exit || frag->is_future_block_head) {
        continue;
      }

      RegisterUsageTracker regs;
      regs.KillAll();
      auto e1 = UpdateRegUsageFromSuccessor(frag->fall_through_target, &regs);
      auto e2 = UpdateRegUsageFromSuccessor(frag->branch_target, &regs);

      if (!(e1 || e2)) {
        frag->data_flow_changed = false;
        continue;
      }

      FindLiveEntryRegsToFrag(frag, &regs);
      frag->data_flow_changed = !regs.Equals(frag->entry_regs_live);
      data_flow_changed = data_flow_changed || frag->data_flow_changed;
      frag->entry_regs_live = regs;
    }
  }
}

#if 0

class RegisterInfo {

  bool used_after_change_sp:1;
  bool used_after_change_ip:1;
  bool defines_constant:1;
  bool used_as_address:1;
  bool depends_on_sp:1;

  uint8_t num_defs;
  uint8_t num_uses;

  // This is fairly rough constraint. This only really meaningful for introduced
  // `LEA` instructions that defined virtual registers as a combination of
  // several other non-virtual registers.
  RegisterUsageTracker depends_on;

} __attribute__((packed));

// Table that records all info about virtual register usage.
class RegisterTable {
 public:
  void Visit(NativeInstruction *instr) {

  }

 private:
  BigVector<RegisterInfo> regs;
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

  Log(LogWarning, frags);

  GRANARY_UNUSED(env);
}

}  // namespace granary
