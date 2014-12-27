/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/ssa.h"

#include "granary/code/assemble/8_schedule_registers.h"

#include "granary/util.h"

enum : bool {
  SHARE_SPILL_SLOTS = true
};

namespace granary {
namespace arch {

// Create an instruction to copy a GPR to a spill slot.
//
// Note: This has an architecture-specific implementation.
extern granary::Instruction *SaveGPRToSlot(VirtualRegister gpr,
                                           VirtualRegister slot);

// Create an instruction to copy the value of a spill slot to a GPR.
//
// Note: This has an architecture-specific implementation.
extern granary::Instruction *RestoreGPRFromSlot(VirtualRegister gpr,
                                                VirtualRegister slot);

// Swaps the value of one GPR with another.
//
// Note: This has an architecture-specific implementation.
extern granary::Instruction *SwapGPRWithGPR(VirtualRegister gpr1,
                                            VirtualRegister gpr2);

// Swaps the value of one GPR with a slot.
//
// Note: This has an architecture-specific implementation.
extern granary::Instruction *SwapGPRWithSlot(VirtualRegister gpr1,
                                             VirtualRegister slot);

// Replace the virtual register `old_reg` with the virtual register `new_reg`
// in the operand `op`.
extern bool ReplaceRegInOperand(Operand *op, VirtualRegister old_reg,
                                VirtualRegister new_reg);

// Returns true of `instr` makes a copy of `use0` and `use1` and stores it into
// `def`.
extern bool GetCopiedOperand(const NativeInstruction *instr,
                             SSAInstruction *ssa_instr,
                             SSAOperand **def, SSAOperand **use0,
                             SSAOperand **use1);

}  // namespace arch
namespace {

// Return the Nth architectural GPR.
static VirtualRegister NthArchGPR(size_t n) {
  return VirtualRegister(kVirtualRegisterKindArchGpr, arch::GPR_WIDTH_BYTES,
                         static_cast<uint16_t>(n));
}

// Return the Nth spill slot.
static VirtualRegister NthSpillSlot(size_t n) {
  return VirtualRegister(kVirtualRegisterKindSlot, arch::GPR_WIDTH_BYTES,
                         static_cast<uint16_t>(n));
}

// Register scheduling is a bottom-to-top process, therefore everything should
// be observed from the perspective of where we are now, what we've previously
// done (which corresponds to later instructions), and how we're going to adapt
// our current state to handle the current instruction or earlier instructions.
//
// The first part of register scheduling is to schedule the partition-local
// registers. Partition-local registers are registers that are used in two or
// more fragments. When scheduling a VR, the partition-local scheduler attempts
// to maintain invariants about where the VR's value is located between
// fragments. This invariants-based approach works because earlier steps ensure
// that if a VR is live on exit from a fragment F, then the VR will be live
// on entry to all of F's successors. The key to maintaining this liveness
// property is the compensation fragments added in by step 6.
//
// The second step is to schedule fragment-local registers, which is treated
// as a special case of partition-local.
//
// Algorithm SchedulePartitionLocalRegisters:
// ------------------------------------------
//    While there are still partition local registers to be scheduled:
//      Choose a spill slot number `SLOT`.
//      For each partition P:
//        Find an unscheduled partition-local virtual register VR in P.
//        Find a preferred GPR for VR.
//          Note: A preferred GPR for VR will be a GPR that will ideally store
//                VR in all fragments using VR. This GPR will represent the
//                canonical way of "communicating" VR when VR is live across
//                two fragments. If no preferred GPR exists, then VR must be
//                homed to a slot between fragments.
//
//        For each fragment F in P:
//          Let Loc(VR) = SLOT
//          Let Loc(PGPR) = PGPR
//
//          If VR is live on exit from F:
//            If VR has a PGPR:
//              Let Loc(VR) = PGPR
//              Let Loc(PGPR) = SLOT
//            Else:
//              Let Loc(VR) = LIVE_SLOT
//
//          For each instruction I in F in reverse order:
//            If Loc(VR) is used or defined in I:
//              Find an alternative GPR, AGPR, for VR.
//              Apply Case 1.
//              Let Loc(Loc(VR)) = Loc(VR).
//              Let Loc(VR) = AGPR.
//              Let Loc(AGPR) = SLOT.
//
//            If VR is used or defined in I:
//              If Loc(VR) is SLOT or Loc(VR) is LIVE_SLOT:
//                Find a usable GPR, UGPR, for VR in I.
//                  Note: We give preference to UGPR == PGPR if PGPR is not
//                        used in I.
//                If Loc(VR) is SLOT:
//                  Apply Case 2.1.
//                Else:  # LIVE_SLOT
//                  Apply Case 2.2.
//                Let Loc(UGPR) = SLOT
//                Let Loc(VR) = UGPR
//
//              Replace all instances of VR in I with Loc(VR).
//
//            If VR was defined by I:
//              Assert1 Loc(VR) is a GPR
//              Assert2 VR is not live on entry to F.
//              Apply Case 3.
//              Let Loc(Loc(VR)) = Loc(VR).
//              Let Loc(VR) = SLOT.
//
//          If VR is live on entry to F:
//            If PGPR is valid:
//              Assert3 Loc(VR) != SLOT
//                Note: Failure of this assertion most likely indicates a
//                      missing compensation fragment, as otherwise `Assert1`
//                      would have triggered the issue in advance. This
//                      shouldn't be possible because compensation fragments
//                      have `kAnnotSSANodeKill` instructions to kill VRs.
//              If Loc(VR) != PGPR:
//                Apply Case 4.
//                Let Loc(Loc(VR)) = Loc(VR).
//                Let Loc(VR) = PGPR.
//                Let Loc(PGPR) = SLOT.
//
//            Else If Loc(VR) != LIVE_SLOT:
//              Assert4 Loc(VR) != SLOT
//                Note: Failure of this assertion most likely means a later
//                      instruction killed VR (by defining it).
//              Apply Case 2.2.
//              Let Loc(Loc(VR)) = Loc(VR).
//              Let Lov(VR) LIVE_SLOT.
//          Else:
//            Assert5 VR is live on exit from F
//            Assert6 Loc(VR) != LIVE_SLOT
//            If Loc(VR) != SLOT:
//              Apply Case 5.
//              Let Loc(Loc(VR)) = Loc(VR).
//              Let Loc(VR) = SLOT.
//
// Case 1:  Scheduling a VR where PGPR == r1. When going through the instruction
//          list in reverse order, we see a native use of PGPR, so we need to
//          change where the register is located. We'll change it to be stored
//          in r2.
//
//          save r2 --> slot  (assumed)
//          ...
//          native use of r1  (current position)
//          swap r2 <-> r1  # Move the VR's value (stored in r2) to r1,
//                          # and save r1's value to r2.
//          swap r2 <-> slot  # Restore r2 and save r1 (stored in r2) to
//                            # the slot.
//
//            Note: This is equivalent to:
//              swap r2 <-> slot  # Restore r2, but keep VR's value in slot.
//              swap r1 <-> slot  # Save r1 to slot, and put VR's value into r1.
//          ...
//          restore slot --> r1 (assumed)
//
// Case 2.1:Scheduling a VR where the VR has not previously been spilled in
//          this fragment. We will use r1 as the register that is scheduled to
//          hold VR, and we'll inject a restore of r1.
//
//          save r1 --> slot  (assumed)
//          ...
//          native use of VR  (current position)
//          restore slot --> r1  # Restore r1.
//
// Case 2.2:Scheduling a VR where the VR has not previously been spilled in
//          this fragment, but where it is assumed to be live, and where the
//          normal invariant could not be maintained, and so VR's value is
//          actually located in the slot. We will use r1 as the register that
//          is scheduled to hold VR, and we'll inject a restore of r1.
//
//          save r1 --> slot  (assumed)
//          ...
//          native use of VR  (current position)
//          swap slot <-> r1  # Restore r1, but keep the value of the VR alive.
//
// Case 3:  We've found the definition of a VR, so we just need to spill its
//          GPR before the instruction defining the GPR.
//
//          save r1 --> slot  # Save r1.
//          native def of VR  (current position)
//          ...
//          restore slot --> r1  (assumed)
//
// Case 4:  We're at the entrypoint to a fragment, VR is live on entry to the
//          fragment (and therefore live on exit from F's predecessor), and
//          PGPR is valid, but the VR is not homed to its PGPR. Therefore, we
//          want to maintain the invariant that across these boundaries, VR is
//          homed to PGPR. We will assume that VR is currently homed to r1.
//
//          This case is fundamentally very similar to Case 1.
//
//          <VR live on entry>
//          swap PGPR <-> r1  # Move the VR's value (stored in PGPR) to r1,
//                            # and save r1's value to PGRP.
//          swap PGPR <-> slot  # Restore PGPR and save r1 (stored in PGPR) to
//                              # the slot.
//          ...
//          native use of VR homed to r1  (assumed)
//          ...
//          restore slot --> r1  (assumed)
//
// Case 5:  We're at the entrypoint to a fragment, VR is not live on entry
//          to the fragment, and is currently homed to a register. Therefore
//          we need to add in the original save of the register to the spill
//          slot.
//
//          save r1 --> slot  # Save the GPR used by VR to a slot.
//          ...
//          restore slot --> r1  (assumed)

enum RegLocationType {
  kRegLocationTypeGpr,
  kRegLocationTypeSlot,
  kRegLocationTypeLiveSlot
};

struct RegLocation {
  VirtualRegister loc;
  RegLocationType type;
};

// Used for scheduling registers in a partition.
struct PartitionScheduler {
  PartitionScheduler(VirtualRegister vr_, SSARegisterWeb *reg_web_,
                     size_t slot_, VirtualRegister preferred_gpr_)
    : vr(vr_),
      spill_slot(NthSpillSlot(slot_)),
      reg_web(UnsafeCast<const SSARegisterWeb *>(reg_web_->Find())),
      vr_location{spill_slot, kRegLocationTypeSlot},
      invalid_location{VirtualRegister(), kRegLocationTypeGpr},
      preferred_gpr(preferred_gpr_) {
    for (auto i = 0UL; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
      gpr_locations[i] = {NthArchGPR(i), kRegLocationTypeGpr};
    }
  }

  // Current location of a register.
  RegLocation &Loc(VirtualRegister reg) {
    if (reg.IsNative()) {
      if (reg.IsGeneralPurpose()) {
        return gpr_locations[reg.Number()];
      }
    } else if (reg.IsVirtual()) {
      if (reg == vr) {
        return vr_location;
      }
    }

    GRANARY_ASSERT(false);
    // Fallback case for arch regs that aren't GPRs, and for virtual registers
    // that we aren't looking at.
    return invalid_location;
  }

  // Virtual register being scheduled in this partition.
  const VirtualRegister vr;
  const VirtualRegister spill_slot;
  const SSARegisterWeb *reg_web;

  // Current locations of arch GPRs.
  RegLocation gpr_locations[arch::NUM_GENERAL_PURPOSE_REGISTERS];

  // Current location of the VR.
  RegLocation vr_location;

  // Dummy.
  RegLocation invalid_location;

  // Should we try to enforce the invariant that if a VR is live on entry/exit
  // from a fragment then it should be located in its preferred GPR?
  VirtualRegister preferred_gpr;
};

// Get an unscheduled VR. This works on VRs *and* shadow GPRs. Shadow GPRs are
// used for efficient save/restore of individual GPRs.
static bool GetUnscheduledVR(SSARegisterWeb *web, VirtualRegister *found_reg,
                             SSARegisterWeb **found_web) {
  auto &reg(web->Value());
  if (!reg.IsVirtual()) return false;
  if (reg.IsScheduled()) return false;
  reg.MarkAsScheduled();
  *found_reg = reg;
  *found_web = web;
  return true;
}

// Try to find an as-of-yet unscheduled SSA register web in an iterable.
template <typename Iterable>
static bool GetUnscheduledVR(const Iterable it,
                             VirtualRegister *found_reg,
                             SSARegisterWeb **found_web) {
  for (auto web : it) {
    if (GetUnscheduledVR(web, found_reg, found_web)) return true;
  }
  return false;
}

// Try to find an as-of-yet unscheduled SSA register web in a fragment.
static bool GetUnscheduledVR(SSAFragment *frag, VirtualRegister *reg,
                             SSARegisterWeb **web) {
  return GetUnscheduledVR(frag->ssa.exit_reg_webs.Values(), reg, web) ||
         GetUnscheduledVR(frag->ssa.internal_reg_webs, reg, web) ||
         GetUnscheduledVR(frag->ssa.entry_reg_webs.Values(), reg, web);
}

struct GPRScheduler {
  GPRScheduler(void)
      : reg_counts(),
        used_regs() {}

  // Recounts the number of times each arch GPR is used within a partition.
  void RecountGPRUsage(const PartitionInfo *partition,
                       Fragment *first, Fragment *last) {
    reg_counts.ClearGPRUseCounters();
    for (auto frag : FragmentListIterator(first)) {
      if (partition != frag->partition.Value()) continue;
      reg_counts.CountGPRUses(frag);
      if (frag == last) break;
    }
  }

  // Try to get a preferred GPR for use by some VR. This will modify `*reg` and
  // return `true` if a preferred GPR is found. Also, if a preferred GPR is
  // found then the GPR will be marked as live in `min_gpr_num`, thus preventing
  // it from being a preferred GPR again.
  VirtualRegister GetPreferredGPR(UsedRegisterSet *preferred_gprs) {
    auto ret = false;
    auto min_gpr_num = static_cast<size_t>(arch::NUM_GENERAL_PURPOSE_REGISTERS);
    auto min_num_uses = std::numeric_limits<size_t>::max();
    for (auto i = 0UL; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
      if (preferred_gprs->IsLive(i)) continue;
      if (reg_counts.num_uses_of_gpr[i] <= min_num_uses) {
        ret = true;
        min_gpr_num = i;
        min_num_uses = reg_counts.num_uses_of_gpr[i];
      }
    }
    if (ret) {
      preferred_gprs->Revive(min_gpr_num);
      return NthArchGPR(min_gpr_num);
    } else {
      return VirtualRegister();
    }
  }

  // Get some GPR for use, so long as the GPR is not part of the
  // `avoid_reg_set`.
  VirtualRegister GetGPR(void) {
    GRANARY_IF_DEBUG( auto found_reg = false; )
    auto min_gpr_num = static_cast<size_t>(arch::NUM_GENERAL_PURPOSE_REGISTERS);
    auto min_num_uses = std::numeric_limits<size_t>::max();
    for (auto i = 0UL; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
      if (used_regs.IsLive(i)) continue;
      if (reg_counts.num_uses_of_gpr[i] < min_num_uses) {
        GRANARY_IF_DEBUG( found_reg = true; )
        min_gpr_num = i;
        min_num_uses = reg_counts.num_uses_of_gpr[i];
      }
    }
    GRANARY_ASSERT(found_reg);
    return NthArchGPR(min_gpr_num);
  }

  // Counts of the number of uses of each register.
  RegisterUsageCounter reg_counts;

  // Registers being used by an instruction.
  UsedRegisterSet used_regs;
};

// Returns true if the virtual register associated with a particular SSA node
// is live within a node map.
static bool IsLive(SSARegisterWebMap &mapped_webs, VirtualRegister reg,
                   const SSARegisterWeb *web) {
  if (mapped_webs.Exists(reg)) {
    const auto mapped_web = mapped_webs[reg];
    return mapped_web->Find() == web;
  }
  return false;
}

// Try to get a GPR for use by an instruction.
static VirtualRegister GetGPR(GPRScheduler *reg_sched, VirtualRegister pgpr) {
  VirtualRegister agpr;
  if (pgpr.IsValid() && reg_sched->used_regs.IsDead(pgpr)) {
    return pgpr;  // Try to use the preferred GPR if possible.
  } else {
    return reg_sched->GetGPR();
  }
}

// Looks through an instruction to see if some node is defined or used.
static void FindDefUse(const SSAInstruction *instr, const SSARegisterWeb *web,
                       bool *is_defined, bool *is_used) {
  for (const auto &op : instr->ops) {
    if (kSSAOperandActionInvalid == op.action) return;
    if (op.reg_web.Find() != web) continue;

    if (kSSAOperandActionWrite == op.action ||
        kSSAOperandActionCleared == op.action) {
      *is_defined = true;
    } else {
      *is_used = true;
    }
  }
}

// Replace a use of a virtual register
static void ReplaceOperandReg(SSAOperand &op, VirtualRegister replacement_reg) {
  const auto &web(op.reg_web);
  auto reg = web.Value();
  GRANARY_ASSERT(reg.IsVirtual());
  GRANARY_ASSERT(replacement_reg.IsNative());
  if (!arch::ReplaceRegInOperand(op.operand, reg, replacement_reg)) {
    GRANARY_ASSERT(false);
  }
}

// Replace all uses of a virtual register associated with a specific SSA node
// with a GPR.
static void ReplaceUsesOfVR(SSAInstruction *instr, const SSARegisterWeb *web,
                            VirtualRegister replacement_reg) {
  for (auto &op : instr->ops) {
    if (kSSAOperandActionInvalid == op.action) return;
    if (op.reg_web.Find() != web) continue;
    ReplaceOperandReg(op, replacement_reg);
  }
}

// The register we want to use for scheduling `vr` is used in this instruction,
// therefore we need to re-home the register.
static void HomeUsedReg(PartitionScheduler *part_sched,
                        GPRScheduler *reg_sched,
                        SSAFragment *frag,
                        Instruction *instr,
                        RegLocation *vr_home) {
  if (!vr_home->loc.IsNative() || !reg_sched->used_regs.IsLive(vr_home->loc)) {
    return; // Not used in this instruction.
  }

  const auto slot = part_sched->spill_slot;
  const auto pgpr = part_sched->preferred_gpr;

  GRANARY_ASSERT(kRegLocationTypeGpr == vr_home->type);
  auto agpr = GetGPR(reg_sched, pgpr);
  GRANARY_ASSERT(vr_home->loc != agpr);

  frag->instrs.InsertAfter(instr, arch::SwapGPRWithSlot(agpr, slot));
  frag->instrs.InsertAfter(instr, arch::SwapGPRWithGPR(vr_home->loc, agpr));

  part_sched->Loc(vr_home->loc) = {vr_home->loc, kRegLocationTypeGpr};
  part_sched->Loc(agpr) = {slot, kRegLocationTypeSlot};

  vr_home->loc = agpr;  // Updates `Loc` by ref.
}

// Schedule all a partition-local register within a specific fragment of the
// partition.
static void ScheduleRegs(PartitionScheduler *part_sched,
                         GPRScheduler *reg_sched, SSAFragment *frag) {
  const auto vr = part_sched->vr;
  const auto reg_web = part_sched->reg_web;
  const auto slot = part_sched->spill_slot;
  const auto pgpr = part_sched->preferred_gpr;
  const auto is_live_on_exit = IsLive(frag->ssa.exit_reg_webs, vr, reg_web);
  const auto is_live_on_entry = IsLive(frag->ssa.entry_reg_webs, vr, reg_web);

  if (is_live_on_exit) {
    if (pgpr.IsValid()) {
      part_sched->Loc(vr) = {pgpr, kRegLocationTypeGpr};
      part_sched->Loc(pgpr) = {slot, kRegLocationTypeSlot};
    } else {
      part_sched->Loc(vr) = {slot, kRegLocationTypeLiveSlot};
    }
  }

  Instruction *first_instr(nullptr);

  auto instr = frag->instrs.Last();
  for (Instruction *prev_instr(nullptr); instr; instr = prev_instr) {
    prev_instr = instr->Previous();

    SSAInstruction *ssa_instr(nullptr);
    auto is_defined = false;
    auto is_used = false;
    auto &vr_home(part_sched->Loc(vr));

    reg_sched->used_regs.KillAll();

    // Annotation instructions can define/kill VRs.
    if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
      auto web = GetMetaData<const SSARegisterWeb *>(ainstr);

      // Note: `kAnnotSSANodeOwner` is not considered a definition because it
      //       is added by the local value numbering stage of SSA construction
      //       for the sake of making it easier to reclaim `SSANode` objects.
      if (kAnnotSSARegisterKill == ainstr->annotation) {
        if (web->Find() == reg_web) {
          // This node can't be homed to a register because the meaning of
          // this is to say that the node was live in a predecessor, but is not
          // live in a successor (of this compensation fragment), and therefore
          // we should not expect and uses of the node to follow this
          // instruction.
          GRANARY_ASSERT(kRegLocationTypeGpr != vr_home.type);

          // Fake a kill as a use. The meaning here is that we expect that this
          // register will start being used, and in fact it's exported from
          // a predecessor fragment's `exit_nodes` into this frag's
          // `entry_nodes`, so we need to have it as a use so that the slot
          // matching happens.
          is_used = true;
        } else {
          continue;
        }

      // Handle a save/restore of a register.
      } else if (kAnnotSSASaveRegister == ainstr->annotation ||
                 kAnnotSSARestoreRegister == ainstr->annotation ||
                 kAnnotSSASwapRestoreRegister == ainstr->annotation) {
        reg_sched->used_regs.Revive(ainstr->Data<VirtualRegister>());

      // Mark that a number of registers need to be live at a specific point.
      } else if (kAnnotSSAReviveRegisters == ainstr->annotation) {
        reg_sched->used_regs = ainstr->Data<UsedRegisterSet>();

      // We can stop here.
      } else if (kAnnotSSAPartitionLocalBegin == ainstr->annotation) {
        first_instr = instr;
        break;

      } else {
        continue;
      }

    // Its a native instruction, need to look to see if the VR is used and/or
    // defined. We also need to see if the current location of the VR is used.
    } else if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      ssa_instr = ninstr->ssa;
      if (!ssa_instr) continue;

      reg_sched->used_regs.Visit(ninstr);

      // If this instruction has no explicit operands, then we can't possibly
      // schedule a VR in, and so we don't need to add the constraint to the
      // system that restricted registers should not be used.
      if (ninstr->NumExplicitOperands()) {
        reg_sched->used_regs.ReviveRestrictedRegisters(ninstr);
      }

      // Figure out if this instruction is defined or used.
      FindDefUse(ssa_instr, reg_web, &is_defined, &is_used);
    }

    HomeUsedReg(part_sched, reg_sched, frag, instr, &vr_home);
    if (!is_used && !is_defined) continue;

    // Inject a fill for this instruction. Filling might restore a GPR if this
    // VR has a preferred GPR, or if there is no preferred GPR, then filling
    // will swap the VR value modified and/or read by this instruction into the
    // spill slot.
    if (kRegLocationTypeSlot == vr_home.type ||
        kRegLocationTypeLiveSlot == vr_home.type) {

      auto agpr = GetGPR(reg_sched, pgpr);
      if (kRegLocationTypeSlot == vr_home.type) {
        frag->instrs.InsertAfter(instr, arch::RestoreGPRFromSlot(agpr, slot));

      } else {  // `LIVE_SLOT`.
        frag->instrs.InsertAfter(instr, arch::SwapGPRWithSlot(agpr, slot));
      }

      part_sched->Loc(agpr) = {slot, kRegLocationTypeSlot};
      vr_home.loc = agpr;  // Updates `Loc` by ref.
      vr_home.type = kRegLocationTypeGpr;
    }

    if (ssa_instr) ReplaceUsesOfVR(ssa_instr, reg_web, vr_home.loc);

    // Inject the spill for this definition.
    if (is_defined) {
      GRANARY_ASSERT(kRegLocationTypeGpr == vr_home.type);
      GRANARY_ASSERT(!is_live_on_entry);
      frag->instrs.InsertBefore(instr, arch::SaveGPRToSlot(vr_home.loc, slot));
      part_sched->Loc(vr_home.loc) = {vr_home.loc, kRegLocationTypeGpr};
      vr_home.loc = slot;
      vr_home.type = kRegLocationTypeSlot;
    }
  }

  auto &vr_home(part_sched->Loc(vr));
  if (is_live_on_entry) {
    GRANARY_ASSERT(kRegLocationTypeSlot != vr_home.type);

    // Need to make sure that the VR is homed to its preferred GPR for
    // transitions between fragments.
    if (pgpr.IsValid()) {
      GRANARY_ASSERT(vr_home.loc.IsNative());
      if (vr_home.loc != pgpr) {
        frag->instrs.InsertBefore(first_instr,
                                  arch::SwapGPRWithGPR(vr_home.loc, pgpr));
        frag->instrs.InsertBefore(first_instr,
                                  arch::SwapGPRWithSlot(pgpr, slot));
      }

    // Need to put the VR into its spill slot for transitions between fragments.
    } else if (kRegLocationTypeLiveSlot != vr_home.type) {
      GRANARY_ASSERT(vr_home.loc.IsNative());
      frag->instrs.InsertBefore(first_instr,
                                arch::SwapGPRWithSlot(vr_home.loc, slot));
    } else {
      GRANARY_ASSERT(vr_home.loc == slot);
    }

  // Not live on entry, i.e. this is one of the first defs of this VR, so we
  // need to add the initial reg spill.
  } else {
    if (kRegLocationTypeGpr == vr_home.type) {
      frag->instrs.InsertBefore(first_instr,
                                arch::SaveGPRToSlot(vr_home.loc, slot));
    } else {
      GRANARY_ASSERT(kRegLocationTypeLiveSlot != vr_home.type);
      GRANARY_ASSERT(vr_home.loc == slot);
    }
  }
}

// Find the bounds of a partition in the fragment list.
static void FindPartitionBounds(FragmentList *frags, PartitionInfo *partition,
                                Fragment **first_frag, Fragment **last_frag) {
  for (auto frag : ReverseFragmentListIterator(frags)) {
    if (frag->partition.Value() == partition) {
      if (!*last_frag) *last_frag = frag;
      *first_frag = frag;
    }
  }
}

// Used for scheduling slots for native GPR saves/restores.
struct SaveRestoreScheduler {
 public:
  SaveRestoreScheduler(PartitionInfo *partition_)
      : partition(partition_) {
    memset(&(gpr_slots[0]), 0, sizeof gpr_slots);
  }

  VirtualRegister SlotForGPR(VirtualRegister gpr) {
    GRANARY_ASSERT(gpr.IsNative() && gpr.IsGeneralPurpose());
    auto &slot(gpr_slots[gpr.Number()]);
    if (!slot.IsValid()) {
      slot = NthSpillSlot(partition->num_slots++);
    }
    return slot;
  }

 protected:
  PartitionInfo *partition;

  VirtualRegister gpr_slots[arch::NUM_GENERAL_PURPOSE_REGISTERS];

 private:
  SaveRestoreScheduler(void) = delete;
};

// Schedule the saves/restores of arch GPRs.
static void ScheduleSaveRestores(SaveRestoreScheduler *slot_sched,
                                 SSAFragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    auto ainstr = DynamicCast<AnnotationInstruction *>(instr);
    if (!ainstr) continue;

    if (kAnnotSSASaveRegister == ainstr->annotation) {
      auto gpr = ainstr->Data<VirtualRegister>();
      auto slot = slot_sched->SlotForGPR(gpr);
      frag->instrs.InsertAfter(instr, arch::SaveGPRToSlot(gpr, slot));

    } else if (kAnnotSSARestoreRegister == ainstr->annotation) {
      auto gpr = ainstr->Data<VirtualRegister>();
      auto slot = slot_sched->SlotForGPR(gpr);
      frag->instrs.InsertAfter(instr, arch::RestoreGPRFromSlot(gpr, slot));

    } else if (kAnnotSSASwapRestoreRegister == ainstr->annotation) {
      auto gpr = ainstr->Data<VirtualRegister>();
      auto slot = slot_sched->SlotForGPR(gpr);
      frag->instrs.InsertAfter(instr, arch::SwapGPRWithSlot(gpr, slot));
    }
  }
}

// Schedule all partition-local virtual registers within the fragments of a
// given partition.
static void ScheduleRegs(FragmentList *frags, PartitionInfo *partition) {
  GPRScheduler gpr_sched;
  VirtualRegister reg;
  VirtualRegister preferred_gpr;
  SSARegisterWeb *reg_web(nullptr);
  UsedRegisterSet preferred_gprs;

  // Used to bound the iterating through the fragment list after the first
  // register has been scheduled.
  Fragment *first_frag(frags->First());
  Fragment *last_frag(nullptr);
  FindPartitionBounds(frags, partition, &first_frag, &last_frag);

  auto slot_num = 0UL;
  auto found_reg = false;
  auto orig_last_frag = last_frag;

  // Schedule the virtual registers.
  do {
    found_reg = false;
    for (auto frag : ReverseFragmentListIterator(last_frag)) {

      // Filter on only a specific partition.
      if (frag->partition.Value() != partition) continue;

      auto ssa_frag = DynamicCast<SSAFragment *>(frag);
      if (!ssa_frag) continue;

      // Go find the register to schedule if we don't have one yet.
      if (!found_reg) {
        last_frag = frag;
        if (!(found_reg = GetUnscheduledVR(ssa_frag, &reg, &reg_web))) {
          continue;
        }

        gpr_sched.RecountGPRUsage(partition, first_frag, last_frag);
        preferred_gpr = gpr_sched.GetPreferredGPR(&preferred_gprs);
        slot_num = partition->num_slots++;
      }

      PartitionScheduler sched(reg, reg_web, slot_num, preferred_gpr);
      ScheduleRegs(&sched, &gpr_sched, ssa_frag);

      if (frag == first_frag) break;
    }
  } while (found_reg);

  // Schedule the save/restores.
  SaveRestoreScheduler slot_sched(partition);
  for (auto frag : ReverseFragmentListIterator(orig_last_frag)) {
    if (frag->partition.Value() == partition) {
      if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
        ScheduleSaveRestores(&slot_sched, ssa_frag);
      }
    } else if (frag == first_frag) {
      break;
    }
  }
}

// Schedule fragment-local registers. We start by doing things one partition at
// at time. Identifying partitions is simple because every partition has a
// single entrypoint: its PartitionEntryFragment. There are technically some
// partitions with no such fragment, but those don't use virtual registers.
static void ScheduleRegs(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto part_frag = DynamicCast<PartitionEntryFragment *>(frag)) {
      auto partition = part_frag->partition.Value();
      GRANARY_ASSERT(nullptr != partition);
      ScheduleRegs(frags, partition);
    }
  }
}

// Add annotations to the fragment that marks the "beginning" of the fragment
// to the register scheduler. This is so that we can know where the first
// spills need to be placed, as well as knowing how to order local and global
// spills.
static void AddFragBeginAnnotations(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
      auto global_annot = new AnnotationInstruction(
          kAnnotSSAPartitionLocalBegin);
      ssa_frag->instrs.Prepend(global_annot);
    }
  }
}

}  // namespace

// Schedule virtual registers.
void ScheduleRegisters(FragmentList *frags) {
  AddFragBeginAnnotations(frags);
  ScheduleRegs(frags);
}

}  // namespace granary
