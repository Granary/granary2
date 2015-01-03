/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

#include "granary/code/assemble/8_schedule_registers.h"

#include "granary/util.h"

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
// in the instruction `instr`.
//
// Note: This has an architecture-specific implementation.
extern bool TryReplaceRegInInstruction(NativeInstruction *instr,
                                       VirtualRegister old_reg,
                                       VirtualRegister new_reg);
}  // namespace arch
namespace {

// Return the Nth architectural GPR.
static VirtualRegister NthArchGPR(size_t n) {
  return VirtualRegister(kVirtualRegisterKindArchGpr, arch::GPR_WIDTH_BYTES,
                         static_cast<uint16_t>(n));
}

// Return the Nth spill slot.
static VirtualRegister NthSpillSlot(size_t n) {
  GRANARY_ASSERT(arch::MAX_NUM_SPILL_SLOTS > n);
  return VirtualRegister(kVirtualRegisterKindSlot, arch::GPR_WIDTH_BYTES,
                         static_cast<uint16_t>(n));
}

// Mark the partition containing a fragment as using VRs, and therefore
// requiring spill/fill allocation.
static void MarkPartitionAsUsingVRs(CodeFragment *frag) {
  auto partition = frag->partition.Value();
  partition->uses_vrs = true;
}

// Get the set of all VRs to schedule.
static void GetSchedulableVRs(FragmentList *frags, VRIdSet *vrs) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      vrs->Union(cfrag->entry_regs);
      vrs->Union(cfrag->exit_regs);
    }
  }
}

struct RegisterScheduler {
  RegisterScheduler(void)
      : num_slots(0),
        is_in_slot{false},
        slot_for_gpr{0},
        gpr_has_slot(),
        gpr_counts() {}

  // Recounts the uses of GPRs across all frags.
  void ResetGlobal(FragmentList *frags) {
    gpr_counts.ClearGPRUseCounters();
    gpr_counts.CountGPRUses(frags);
  }

  // Recounts the uses of GPRs within a specific frag.
  void ResetLocal(Fragment *frag) {
    gpr_counts.ClearGPRUseCounters();
    gpr_counts.CountGPRUses(frag);
  }

  // Resets the GPR slots. We put `kAnnotRegisterSave/Restore/SwapRestore` in a
  // different "namespace" of slots than normal GPR save/restores because
  // otherwise we'd have to deal with unusual issues that come about due to
  // `kAnnotRegisterSwapRestore` containing a "live" value in the slot.
  void ResetGPRSlots(void) {
    gpr_has_slot.KillAll();
  }

  // Returns `true` if a register GPR has a non-zero count.
  inline size_t NumUsesOfGPR(VirtualRegister gpr) {
    return gpr_counts.NumUses(gpr);
  }

  // Returns the spill slot register associated with an arch GPR.
  VirtualRegister SlotForGPR(VirtualRegister gpr) {
    GRANARY_ASSERT(gpr.IsNative() && gpr.IsGeneralPurpose());
    auto gpr_num = gpr.Number();
    if (!gpr_has_slot.IsLive(gpr_num)) {
      slot_for_gpr[gpr_num] = num_slots++;
      gpr_has_slot.Revive(gpr_num);
    }
    return NthSpillSlot(slot_for_gpr[gpr_num]);
  }

  // Return an unused GPR for use as a preferred GPR.
  VirtualRegister GetPreferredGPR(const UsedRegisterSet &used_regs) {
    for (auto i = 0UL; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
      if (used_regs.IsLive(i)) continue;
      if (!gpr_counts.NumUses(i)) return NthArchGPR(i);
    }
    return VirtualRegister();
  }

  // Return the least used GPR for use that's not also used in `used_regs`.
  VirtualRegister GetGPR(const UsedRegisterSet &used_regs) {
    auto found_reg = false;
    auto min_gpr_num = static_cast<size_t>(arch::NUM_GENERAL_PURPOSE_REGISTERS);
    auto min_num_uses = std::numeric_limits<size_t>::max();
    for (auto i = 0UL; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
      if (used_regs.IsLive(i)) continue;
      auto num_uses = gpr_counts.NumUses(i);
      if (num_uses < min_num_uses) {
        found_reg = true;
        min_gpr_num = i;
        min_num_uses = num_uses;
      }
    }
    if (found_reg) return NthArchGPR(min_gpr_num);
    return VirtualRegister();
  }

  // Number of slots allocated.
  size_t num_slots;

  // Tells us whether or not the GPR is currently located in its slot.
  bool is_in_slot[arch::NUM_GENERAL_PURPOSE_REGISTERS];

  // The slot associated with a GPR.
  size_t slot_for_gpr[arch::NUM_GENERAL_PURPOSE_REGISTERS];
  UsedRegisterSet gpr_has_slot;

  // Counts of the number of uses of each register.
  RegisterUsageCounter gpr_counts;
};

// Returns `true` if `vr_id` is used in or defined by `instr`.
static bool IsUsedOrDefined(const NativeInstruction *instr, uint16_t vr_id) {
  if (instr->defined_vr == vr_id) return true;
  for (auto used_vr_id : instr->used_vrs) {
    if (used_vr_id == vr_id) return true;
  }
  return false;
}

// Updates `used_regs` based on registers specifically marked by `ainstr`.
static bool UpdateUseRegs(AnnotationInstruction *instr,
                          UsedRegisterSet *used_regs) {
  if (kAnnotSaveRegister == instr->annotation ||
      kAnnotRestoreRegister == instr->annotation ||
      kAnnotSwapRestoreRegister == instr->annotation) {
    used_regs->Revive(instr->Data<VirtualRegister>());
    return true;
  } else if (kAnnotReviveRegisters == instr->annotation) {
    used_regs->Union(instr->DataRef<UsedRegisterSet>());
    return true;
  } else {
    return false;
  }
}

// Arrange for a label to be *just* before any useful VR-related instructions.
static Instruction *AddSchedLabel(CodeFragment *frag,
                                  Instruction *first_vr_instr) {
  if (first_vr_instr && first_vr_instr->Previous()) {
    return first_vr_instr->Previous();
  }
  auto sched_label = new LabelInstruction;
  if (first_vr_instr) {
    frag->instrs.InsertBefore(first_vr_instr, sched_label);
  } else {
    frag->instrs.Prepend(sched_label);
  }
  return sched_label;
}

// Re-homes a virtual register.
//
// Example: A is `new_vr_home`, B is `old_home`.
//      slot(A) <- A
//      ...
//      <homed on A>
//      <instr using B>
//      slot(B) <- B
//      swap A, B
//      A <- slot(A)
//      <homed on B>
//      ...
//      B <- slot(B)
static void ChangeVRHome(RegisterScheduler *sched, CodeFragment *frag,
                         Instruction *instr, VirtualRegister old_home,
                         VirtualRegister new_home) {
  frag->instrs.InsertAfter(instr, arch::RestoreGPRFromSlot(
      new_home, sched->SlotForGPR(new_home)));

  frag->instrs.InsertAfter(instr, arch::SwapGPRWithGPR(
      old_home, new_home));

  frag->instrs.InsertAfter(instr, arch::SaveGPRToSlot(
      old_home, sched->SlotForGPR(old_home)));
}

// Schedule the virtual register with id `vr_id`, where the VR will be stored
// in `preferred_gpr` across control-flow edges where it's live.
static void ScheduleRegisters(RegisterScheduler *sched, CodeFragment *frag,
                              const uint16_t vr_id,
                              const VirtualRegister preferred_gpr) {
  // Nothing to do. This fragment isn't in the live range of this VR.
  if (!frag->exit_regs.Contains(vr_id)) return;

  auto vr_is_used_in_later_instr = false;
  const auto vr_is_live_on_entry = frag->entry_regs.Contains(vr_id);
  const auto vr_reg = VirtualRegister(kVirtualRegisterKindVirtualGpr,
                                      arch::GPR_WIDTH_BYTES, vr_id);
  sched->ResetLocal(frag);

  // In which GPR is `vr` homed at the end of the fragment?
  VirtualRegister vr_home(preferred_gpr);

  // The first instruction that is potentially sensitive to VR scheduling.
  Instruction *first_vr_instr(nullptr);

  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    UsedRegisterSet used_regs;
    UsedRegisterSet restricted_regs;
    NativeInstruction *ninstr(nullptr);
    auto vr_is_used_or_defined_in_instr = false;

    if ((ninstr = DynamicCast<NativeInstruction *>(instr))) {
      first_vr_instr = instr;
      used_regs.Visit(ninstr);
      vr_is_used_or_defined_in_instr = IsUsedOrDefined(ninstr, vr_id);
      if (vr_is_used_or_defined_in_instr) {
        used_regs.ReviveRestrictedRegisters(ninstr);
      }
    } else if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
      if (UpdateUseRegs(ainstr, &used_regs)) first_vr_instr = instr;
    }

    // The GPR `vr_home` is used in `instr`, so we need to re-home the VR and
    // make sure we inject the initial spill for `vr_home`.
    //
    // This will only really happen in one of three cases:
    //    1)  The preferred GPR is restricted for this particular instruction.
    //    2)  A later instruction caused case 1, and so we re-homed, and the
    //        current home is live in this instruction.
    //    3)  A different VR hit case 1 and was re-homed to the preferred GPR
    //        of our VR.
    if (used_regs.IsLive(vr_home)) {
      auto new_vr_home = preferred_gpr;
      if (used_regs.IsLive(preferred_gpr)) {
        new_vr_home = sched->GetGPR(used_regs);
      }
      GRANARY_ASSERT(vr_home != new_vr_home);
      GRANARY_ASSERT(new_vr_home.IsNative());

      ChangeVRHome(sched, frag, instr, vr_home, new_vr_home);
      vr_home = new_vr_home;
    }

    if (vr_is_used_or_defined_in_instr) {
      vr_is_used_in_later_instr = true;

      // Replace all uses of this VR in the instruction with `vr_home`.
      GRANARY_ASSERT(vr_home.IsNative() && vr_home.IsGeneralPurpose());
      GRANARY_IF_DEBUG( auto replaced = ) arch::TryReplaceRegInInstruction(
          ninstr, vr_reg, vr_home);
      GRANARY_ASSERT(replaced);
    }
  }

  if (vr_is_used_in_later_instr) MarkPartitionAsUsingVRs(frag);

  // Live on entry, need to make sure that the VR is homed to its preferred
  // GPR across control-transfers.
  if (vr_is_live_on_entry) {
    if (preferred_gpr != vr_home) {
      auto sched_label = AddSchedLabel(frag, first_vr_instr);
      ChangeVRHome(sched, frag, sched_label, vr_home, preferred_gpr);
    }

  // Not live on entry, need to set up an initial spill.
  } else {
    auto sched_label = AddSchedLabel(frag, first_vr_instr);
    frag->instrs.InsertAfter(sched_label, arch::SaveGPRToSlot(
        vr_home, sched->SlotForGPR(vr_home)));
  }
}

// Tells us if the VR with id `vr_id` is *really* live on exit.
static bool VRIsLiveOnExit(CodeFragment *frag, const uint16_t vr_id) {
  for (auto succ : frag->successors) {
    if (!succ) continue;
    auto succ_cfrag = DynamicCast<CodeFragment *>(succ);
    if (!succ_cfrag) continue;

    if (succ_cfrag->attr.is_compensation_frag) {
      if (succ_cfrag->exit_regs.Contains(vr_id)) return true;
    } else {
      return true;
    }
  }
  return false;
}

// Schedule the virtual register with id `vr_id`, where the VR will be stored
// in slot across control-flow edges where it's live.
static void ScheduleRegisters(RegisterScheduler *sched, CodeFragment *frag,
                              const uint16_t vr_id, const size_t slot) {
  // Nothing to do. This fragment isn't in the live range of this VR.
  if (!frag->exit_regs.Contains(vr_id)) return;

  auto vr_is_used_in_later_instr = VRIsLiveOnExit(frag, vr_id);
  const auto vr_is_defined_in_frag = frag->def_regs.Exists(vr_id);
  const auto vr_is_live_on_entry = frag->entry_regs.Contains(vr_id);
  const auto slot_reg = NthSpillSlot(slot);
  const auto vr_reg = VirtualRegister(kVirtualRegisterKindVirtualGpr,
                                      arch::GPR_WIDTH_BYTES, vr_id);
  sched->ResetLocal(frag);

  // The first instruction that is potentially sensitive to VR scheduling.
  Instruction *first_vr_instr(nullptr);

  // The current home of the VR with id `vr_id`. Might be a GPR or a
  // spill slot.
  VirtualRegister vr_home(slot_reg);

  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    UsedRegisterSet used_regs;
    UsedRegisterSet restricted_regs;
    NativeInstruction *ninstr(nullptr);
    auto vr_is_used_or_defined_in_instr = false;

    if ((ninstr = DynamicCast<NativeInstruction *>(instr))) {
      first_vr_instr = instr;
      used_regs.Visit(ninstr);
      vr_is_used_or_defined_in_instr = IsUsedOrDefined(ninstr, vr_id);
      if (vr_is_used_or_defined_in_instr) {
        used_regs.ReviveRestrictedRegisters(ninstr);
      }
    } else if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
      if (UpdateUseRegs(ainstr, &used_regs)) first_vr_instr = instr;
    }

    // The GPR `vr_home` is used in `instr`, so we'll conservatively re-home
    // on the slot.
    if (vr_home.IsNative() && used_regs.IsLive(vr_home)) {
      GRANARY_ASSERT(vr_is_used_in_later_instr);
      frag->instrs.InsertAfter(instr, arch::RestoreGPRFromSlot(
          vr_home, slot_reg));
      frag->instrs.InsertAfter(instr, arch::SaveGPRToSlot(
          vr_home, sched->SlotForGPR(vr_home)));
      vr_home = slot_reg;
    }

    if (vr_is_used_or_defined_in_instr) {
      if (slot_reg == vr_home) {
        vr_home = sched->GetGPR(used_regs);
        frag->instrs.InsertAfter(instr, arch::RestoreGPRFromSlot(
            vr_home, sched->SlotForGPR(vr_home)));
        if (vr_is_used_in_later_instr && vr_is_defined_in_frag) {
          frag->instrs.InsertAfter(instr, arch::SaveGPRToSlot(
              vr_home, slot_reg));
        }
      }

      // Replace all uses of this VR in the instruction with `vr_home`.
      GRANARY_ASSERT(vr_home.IsNative() && vr_home.IsGeneralPurpose());
      GRANARY_IF_DEBUG( auto replaced = ) arch::TryReplaceRegInInstruction(
          ninstr, vr_reg, vr_home);
      GRANARY_ASSERT(replaced);

      vr_is_used_in_later_instr = true;
    }
  }

  if (vr_is_used_in_later_instr) MarkPartitionAsUsingVRs(frag);

  if (slot_reg != vr_home) {
    GRANARY_ASSERT(vr_is_used_in_later_instr);

    auto sched_label = AddSchedLabel(frag, first_vr_instr);

    // Only restore the VR's value if its got an incoming value.
    if (vr_is_live_on_entry) {
      frag->instrs.InsertAfter(sched_label,
                               arch::RestoreGPRFromSlot(vr_home, slot_reg));
    }
    frag->instrs.InsertAfter(sched_label, arch::SaveGPRToSlot(
        vr_home, sched->SlotForGPR(vr_home)));
  }
}

// Schedule the saves/restores of arch GPRs.
static void ScheduleSaveRestores(RegisterScheduler *sched,
                                 CodeFragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    auto ainstr = DynamicCast<AnnotationInstruction *>(instr);
    if (!ainstr) continue;

    if (kAnnotSaveRegister == ainstr->annotation) {
      auto gpr = ainstr->Data<VirtualRegister>();
      auto slot = sched->SlotForGPR(gpr);
      frag->instrs.InsertAfter(instr, arch::SaveGPRToSlot(gpr, slot));

    } else if (kAnnotRestoreRegister == ainstr->annotation) {
      auto gpr = ainstr->Data<VirtualRegister>();
      auto slot = sched->SlotForGPR(gpr);
      frag->instrs.InsertAfter(instr, arch::RestoreGPRFromSlot(gpr, slot));

    } else if (kAnnotSwapRestoreRegister == ainstr->annotation) {
      auto gpr = ainstr->Data<VirtualRegister>();
      auto slot = sched->SlotForGPR(gpr);
      frag->instrs.InsertAfter(instr, arch::SwapGPRWithSlot(gpr, slot));
    } else {
      continue;
    }

    // If we fell through, then this partition uses VRs.
    MarkPartitionAsUsingVRs(frag);
  }
}

// Schedule the saves/restores of arch GPRs.
static void ScheduleSaveRestores(RegisterScheduler *sched,
                                 FragmentList *frags) {
  // Schedule the save/restores.
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      ScheduleSaveRestores(sched, cfrag);
    }
  }
}

// Assign the slots to the partitions for later slot allocation.
static void MarkPartitionUseCounts(RegisterScheduler *sched,
                                   FragmentList *frags) {
  if (!sched->num_slots) return;
  for (auto frag : FragmentListIterator(frags)) {
    if (auto partition = frag->partition.Value()) {
      if (partition->uses_vrs) partition->num_slots = sched->num_slots;
    }
  }
}

}  // namespace

// Schedule virtual registers.
void ScheduleRegisters(FragmentList *frags) {
  VRIdSet vrs;
  RegisterScheduler sched;
  UsedRegisterSet preferred_gprs;

  // TODO(pag): Weighted sorting of VRs by number of uses, where the number of
  //            uses is weighted to favor hot code.
  GetSchedulableVRs(frags, &vrs);

  for (auto vr_id : vrs) {
    sched.ResetGlobal(frags);

    // Allocate a slot for the VR, and try to find a preferred GPR for the VR.
    // The idea with the preferred GPRs is that we ideally want the VR to be
    // homed to a specific GPR over the entire live range of the VR.
    // Specifically, we also want the GPR to be homed to its preferred GPR
    // across control-flow edges. Otherwise, we say the VR is always in its
    // slot across control-flow edges.
    auto preferred_gpr = sched.GetPreferredGPR(preferred_gprs);
    auto slot = 0UL;
    if (preferred_gpr.IsValid()) {
      preferred_gprs.Revive(preferred_gpr);
    } else {
      slot = sched.num_slots++;
    }

    for (auto frag : FragmentListIterator(frags)) {
      if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {

        // This VR has a preferred GPR, and so it will be homed to that GPR
        // across control-flow transfers.
        if (preferred_gpr.IsValid()) {

          // Only thing in compensation fragments are implicit register
          // kills for VRs that are homed to preferred GPRs.
          if (cfrag->attr.is_compensation_frag) {
            if (cfrag->entry_regs.Contains(vr_id) &&
                !cfrag->exit_regs.Contains(vr_id)) {
              cfrag->instrs.Prepend(arch::RestoreGPRFromSlot(
                  preferred_gpr, sched.SlotForGPR(preferred_gpr)));
            }

          } else {
            ScheduleRegisters(&sched, cfrag, vr_id, preferred_gpr);
          }

        // Without preferred GRPs, all transfers will end up going through
        // spill slots anyway, so there is no interference with compensation
        // code.
        } else if (!cfrag->attr.is_compensation_frag) {
          ScheduleRegisters(&sched, cfrag, vr_id, slot);
        }
      }
    }
  }

  sched.ResetGPRSlots();
  ScheduleSaveRestores(&sched, frags);
  MarkPartitionUseCounts(&sched, frags);
}

}  // namespace granary
