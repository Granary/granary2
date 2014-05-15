/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/code/assemble/fragment.h"
#include "granary/breakpoint.h"

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Fragment, 2),
      (SSAFragment, 2 * 3),
        (CodeFragment, 2 * 3 * 5),
        (FlagEntryFragment, 2 * 3 * 7),
        (FlagExitFragment, 2 * 3 * 11),
      (PartitionEntryFragment, 2 * 13),
      (PartitionExitFragment, 2 * 17),
      (ExitFragment, 2 * 19))

GRANARY_DEFINE_BASE_CLASS(Fragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, SSAFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, CodeFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, PartitionEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, PartitionExitFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, FlagEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, FlagExitFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, ExitFragment)

// Allocate a spill slot from this spill info. Takes an optional offset that
// can be used to slide the allocated slot by some amount. The offset
// parameter is used to offset partition-local slot allocations by the number
// of fragment local slot allocations.
int SpillInfo::AllocateSpillSlot(int offset) {
  for (auto i = offset; i < num_slots; ++i) {
    if (!used_slots.Get(i)) {
      used_slots.Set(i, true);
      return i;
    }
  }
  num_slots = std::max(num_slots, offset);
  GRANARY_ASSERT(MAX_NUM_SPILL_SLOTS > (1 + num_slots));
  used_slots.Set(num_slots, true);
  return num_slots++;
}

// Mark a spill slot as being used.
void SpillInfo::MarkSlotAsUsed(int slot) {
  GRANARY_ASSERT(slot >= 0);
  GRANARY_ASSERT(MAX_NUM_SPILL_SLOTS > (1 + slot));
  used_slots.Set(slot, true);
  num_slots = std::max(num_slots, slot + 1);
}

// Free a spill slot from active use.
void SpillInfo::FreeSpillSlot(int slot) {
  GRANARY_ASSERT(slot >= 0);
  GRANARY_ASSERT(num_slots > slot);
  GRANARY_ASSERT(used_slots.Get(slot));
  used_slots.Set(slot, false);
}

PartitionInfo::PartitionInfo(int id_)
    : id(id_),
      scheduler_round(0),
      num_local_slots(0),
      num_uses_of_gpr{0},
      preferred_gpr_num(-1),
      vr_being_scheduled(nullptr),
      spill() {}

// Clear out the number of usage count of registers in this partition.
void PartitionInfo::ClearGPRUseCounters(void) {
  memset(&(num_uses_of_gpr[0]), 0, sizeof num_uses_of_gpr);
  preferred_gpr_num = -1;
}

// Count the number of uses of the arch GPRs in this fragment.
void PartitionInfo::CountGPRUses(Fragment *frag) {
  frag->regs.CountGPRUses(frag);
  for (auto i = 0; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
    num_uses_of_gpr[i] += frag->regs.num_uses_of_gpr[i];
  }
}

// Returns the most preferred arch GPR for use by partition-local register
// scheduling.
int PartitionInfo::PreferredGPRNum(void) {
  if (-1 == preferred_gpr_num) {
    auto min = INT_MAX;
    for (auto i = arch::NUM_GENERAL_PURPOSE_REGISTERS - 1; i >= 0; --i) {
      if (num_uses_of_gpr[i] < min) {
        preferred_gpr_num = i;
        min = num_uses_of_gpr[i];
      }
    }
    GRANARY_ASSERT(-1 != preferred_gpr_num);
  }
  return preferred_gpr_num;
}

RegisterUsageInfo::RegisterUsageInfo(void)
    : live_on_entry(),
      live_on_exit(),
      num_uses_of_gpr{0} {}

// Clear out the number of usage count of registers in this fragment.
void RegisterUsageInfo::ClearGPRUseCounters(void) {
  memset(&(num_uses_of_gpr[0]), 0, sizeof num_uses_of_gpr);
}

// Count the number of uses of the arch GPRs in this fragment.
void RegisterUsageInfo::CountGPRUses(Fragment *frag) {
  frag->regs.ClearGPRUseCounters();
  auto gpr_counter = [=] (VirtualRegister reg) {
    if (reg.IsNative() && reg.IsGeneralPurpose()) {
      num_uses_of_gpr[reg.Number()] += 1;
    }
  };
  auto operand_counter = [&] (Operand *op) {
    if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
      gpr_counter(reg_op->Register());
    } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
      VirtualRegister r1, r2, r3;
      if (mem_op->CountMatchedRegisters({&r1, &r2, &r3})) {
        gpr_counter(r1);
        gpr_counter(r2);
        gpr_counter(r3);
      }
    }
  };
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      ninstr->ForEachOperand(operand_counter);
    }
  }
}


CodeAttributes::CodeAttributes(void)
    : is_edge_code(false),
      branches_to_edge_code(false),
      can_add_to_partition(true),
      has_native_instrs(false),
      modifies_flags(false),
      has_flag_split_hint(false),
      is_app_code(false),
      is_block_head(false),
      is_compensation_code(false),
      num_inst_preds(0),
      block_meta(nullptr) {}

Fragment::Fragment(void)
    : list(),
      instrs(),
      partition(),
      flag_zone(),
      flags(),
      temp(),
      successors{nullptr, nullptr},
      branch_instr(nullptr) { }

SSAFragment::~SSAFragment(void) {}
CodeFragment::~CodeFragment(void) {}
PartitionEntryFragment::~PartitionEntryFragment(void) {}
PartitionExitFragment::~PartitionExitFragment(void) {}
FlagEntryFragment::~FlagEntryFragment(void) {}
FlagExitFragment::~FlagExitFragment(void) {}
ExitFragment::~ExitFragment(void) {}

FlagZone::FlagZone(VirtualRegister flag_save_reg_,
                   VirtualRegister flag_killed_reg_)
    : killed_flags(0),
      live_flags(0),
      flag_save_reg(flag_save_reg_),
      flag_killed_reg(flag_killed_reg_),
      used_regs(),
      live_regs(),
      num_frags_in_zone(0),
      only_frag(nullptr) {}

}  // namespace granary
