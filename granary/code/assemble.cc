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
#include "granary/code/metadata.h"
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
static void UpdateRegUsageFromSuccessor(Fragment *succ,
                                        RegisterUsageTracker *regs) {
  if (succ) {
    regs->Union(succ->entry_regs_live);
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

// Initialize the live entry regs as a data flow problem.
static void InitLiveEntryRegsToFrags(Fragment *frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (frag->is_exit || frag->is_future_block_head) {
      frag->entry_regs_live.ReviveAll();
    } else {
      frag->entry_regs_live.KillAll();
    }
    frag->exit_regs_live.KillAll();
  }
}

// Calculate the live registers on entry to every fragment.
static void FindLiveEntryRegsToFrags(Fragment *frags) {
  InitLiveEntryRegsToFrags(frags);

  for (bool data_flow_changed = true; data_flow_changed; ) {
    data_flow_changed = false;
    for (auto frag : FragmentIterator(frags)) {
      if (frag->is_exit || frag->is_future_block_head) {
        continue;
      }

      RegisterUsageTracker regs;
      regs.KillAll();
      UpdateRegUsageFromSuccessor(frag->fall_through_target, &regs);
      UpdateRegUsageFromSuccessor(frag->branch_target, &regs);
      if (regs.Equals(frag->exit_regs_live)) {
        continue;
      }

      frag->exit_regs_live = regs;
      FindLiveEntryRegsToFrag(frag, &regs);
      if (!regs.Equals(frag->entry_regs_live)) {
        frag->entry_regs_live = regs;
        data_flow_changed = true;
      }
    }
  }
}

// Tracks the stack ID / color of a fragment.
class FragmentColorer {
 public:
  FragmentColorer(void)
      : next_invalid_id(-1),
        next_valid_id(1) {}

  // Mark a fragment as having a stack pointer that appears to behave like
  // a C-style call stack.
  void MarkAsValid(Fragment *frag) {
    if (frag) {
      GRANARY_ASSERT(0 <= frag->stack_id);
      if (!frag->stack_id) {
        frag->stack_id = next_valid_id++;
      }
    }
  }

  // Mark a fragment as having a stack pointer that doesn't necessarily
  // behave like a callstack.
  void MarkAsInvalid(Fragment *frag) {
    if (frag) {
      GRANARY_ASSERT(0 >= frag->stack_id);
      if (!frag->stack_id) {
        frag->stack_id = next_invalid_id--;
      }
    }
  }

  // Try to use information known about the last instruction of the fragment
  // being a control-flow instruction to color a fragment.
  void ColorFragmentByCFI(Fragment *frag) {
    if (auto instr = DynamicCast<ControlFlowInstruction *>(frag->last)) {

      // Assumes that interrupt return, like a function return, reads its
      // target off of the stack.
      if (instr->IsInterruptReturn()) {
        MarkAsValid(frag);
        MarkAsInvalid(frag->fall_through_target);

      // Target block of a system return has an invalid stack.
      } else if (instr->IsSystemReturn()) {
        MarkAsInvalid(frag);
        MarkAsInvalid(frag->fall_through_target);

      // Assumes that function calls/returns push/pop return addresses on the
      // stack. This also makes the assumption that function calls actually
      // lead to returns.
      } else if (instr->IsFunctionCall() || instr->IsFunctionReturn()) {
        MarkAsValid(frag);
        MarkAsValid(frag->branch_target);
        MarkAsValid(frag->fall_through_target);
      }
    }
  }

  // If this fragment is cached then check its meta-data. Mostly we actually
  // care not about this fragment, but about fragments targeting this
  // fragment.
  //
  // We check against the first fragment because we don't want to penalize
  // the first fragment into a different color if back propagation can give
  // it a color on its own.
  bool ColorFragmentByMetaData(Fragment *frag, Fragment *first_frag) {
    auto stack_meta = MetaDataCast<StackMetaData *>(frag->block_meta);
    if (frag != first_frag && stack_meta->has_stack_hint) {
      if (stack_meta->behaves_like_callstack) {
        MarkAsValid(frag);
      } else {
        MarkAsInvalid(frag);
      }
      return true;
    }
    return false;
  }

  // Initialize the fragment coloring.
  void InitColoring(Fragment *frags) {
    for (auto frag : FragmentIterator(frags)) {
      if (frag->reads_stack_pointer) {  // Reads & writes the stack pointer.
        MarkAsValid(frag);
      } else if (frag->block_meta && frag->is_exit) {
        ColorFragmentByMetaData(frag, frags);
      }
      ColorFragmentByCFI(frag);
    }
  }

  // Propagate the coloring from a source fragment to a dest fragment. This
  // can be used for either a successor or predecessor relationship.
  bool PropagateColor(Fragment *source, Fragment *dest) {
    if (dest && !dest->stack_id) {
      if (source->block_meta == dest->block_meta) {
        dest->stack_id = source->stack_id;
      } else if (source->stack_id > 0) {
        MarkAsValid(dest);
      } else {
        MarkAsInvalid(dest);
      }
      return true;
    }
    return false;
  }

  // Perform a backward data-flow pass on the fragment stack ID colorings.
  bool BackPropagate(Fragment *frags) {
    auto global_changed = false;
    for (auto changed = true; changed; ) {
      changed = false;
      for (auto frag : FragmentIterator(frags)) {
        if (!frag->stack_id &&
            !frag->writes_stack_pointer &&
            frag->fall_through_target &&
            frag->fall_through_target->stack_id) {
          changed = PropagateColor(frag->fall_through_target, frag) || changed;
        }
      }
      global_changed = global_changed || changed;
    }
    return global_changed;
  }

  // Perform a forward data-flow pass on the fragment stack ID colorings.
  bool ForwardPropagate(Fragment *frags) {
    auto global_changed = false;
    for (auto changed = true; changed; ) {
      changed = false;
      for (auto frag : FragmentIterator(frags)) {
        if (!frag->stack_id || frag->writes_stack_pointer) {
          continue;
        }
        changed = PropagateColor(frag, frag->branch_target) || changed;
        changed = PropagateColor(frag, frag->fall_through_target) || changed;
      }
      global_changed = global_changed || changed;
    }
    return global_changed;
  }

 private:
  int next_invalid_id;
  int next_valid_id;
};

// Partition the fragments into groups, where each group is labeled/colored by
// their `stack_id` field.
static void PartitionFragmentsByStack(Fragment *frags) {
  FragmentColorer colorer;
  colorer.InitColoring(frags);
  for (auto changed = true; changed; ) {
    changed = colorer.BackPropagate(frags);
    changed = colorer.ForwardPropagate(frags) || changed;

    // If we haven't made progress, then try to take a hint from the meta-data
    // of the entry fragment and propagate it forward (assuming that we have
    // not already deduced the safety of its stack).
    if (!changed && !frags->stack_id) {
      changed = colorer.ColorFragmentByMetaData(frags, nullptr);
    }
  }
  for (auto frag : FragmentIterator(frags)) {
    if (!frag->stack_id) {
      colorer.MarkAsInvalid(frag);
    }
  }
}


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
  PartitionFragmentsByStack(frags);

  Log(LogWarning, frags);

  GRANARY_UNUSED(env);
}

}  // namespace granary
