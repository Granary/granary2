/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/ssa.h"

#include "granary/util.h"

namespace granary {

// Returns a valid `SSAOperand` pointer to the operand being copied if this
// instruction is a copy instruction, otherwise returns `nullptr`.
//
// Note: This has an architecture-specific implementation.
extern SSAOperand *GetCopiedOperand(const NativeInstruction *instr);

// Create an instruction to copy a GPR to a spill slot.
//
// Note: This has an architecture-specific implementation.
extern Instruction *SaveGPRToSlot(VirtualRegister gpr, VirtualRegister slot);

// Create an instruction to copy the value of a spill slot to a GPR.
//
// Note: This has an architecture-specific implementation.
extern Instruction *RestoreGPRFromSlot(VirtualRegister gpr,
                                       VirtualRegister slot);

// Swaps the value of one GPR with another.
//
// Note: This has an architecture-specific implementation.
extern Instruction *SwapGPRWithGPR(VirtualRegister gpr1, VirtualRegister gpr2);

// Swaps the value of one GPR with a slot.
//
// Note: This has an architecture-specific implementation.
extern Instruction *SwapGPRWithSlot(VirtualRegister gpr1, VirtualRegister slot);

// Returns the GPR that is copied by this instruction into a virtual
// register. If this instruction is not a simple copy operation of this form,
// then an invalid virtual register is returned.
//
// Note: This has an architecture-specific implementation.
extern VirtualRegister GPRCopiedToVR(const NativeInstruction *instr);

// Returns the GPR that is copied by this instruction from a virtual
// register. If this instruction is not a simple copy operation of this form,
// then an invalid virtual register is returned.
//
// Note: This has an architecture-specific implementation.
extern VirtualRegister GPRCopiedFromVR(const NativeInstruction *instr);
namespace {

// Applies a function to each `SSAOperand` that defines a register within the
// `SSAInstruction` associated with `instr`.
static void ForEachDefinitionOperandImpl(
    NativeInstruction *instr, std::function<void(SSAOperand *)> &func) {
  if (auto ssa_instr = GetMetaData<SSAInstruction *>(instr)) {
    for (auto &def : ssa_instr->defs) {
      if (SSAOperandAction::WRITE != def.action) break;
      func(&def);
    }
    for (auto &def : ssa_instr->uses) {
      if (SSAOperandAction::READ_WRITE != def.action) break;
      func(&def);
    }
  }
}

// Applies a function to each `SSAOperand` that defines a register within the
// `SSAInstruction` associated with `instr`.
template <typename T>
static void ForEachDefinitionOperand(NativeInstruction *instr, T func_) {
  std::function<void(SSAOperand *)> func(std::cref(func_));
  ForEachDefinitionOperandImpl(instr, func);
}

// Applies a function to each defined `SSANode` within a given instruction.
static void ForEachDefinitionImpl(Instruction *instr,
                                  std::function<void(SSANode *)> &func) {
  if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
    if (IA_SSA_NODE_DEF == ainstr->annotation) {
      if (auto def_node = ainstr->GetData<SSANode *>()) {
        func(def_node);
      }
    }
  } else if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
    ForEachDefinitionOperand(ninstr, [&] (SSAOperand *op) {
      func(op->nodes[0]);
    });
  }
}

// Applies a function to each defined `SSANode` within a given instruction.
template <typename T>
static void ForEachDefinition(Instruction *instr, T func_) {
  std::function<void(SSANode *)> func(std::cref(func_));
  ForEachDefinitionImpl(instr, func);
}

// Allocate `SSASpillStorage` objects for every register web.
static void AllocateSpillStorage(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      ForEachDefinition(instr, [=] (SSANode *node) {
        auto &storage(node->storage.Value());
        if (!storage) {
          storage = new SSASpillStorage;
        }
      });
    }
  }
}

// Allocate `SSASpillStorage` objects for every register web.
static void FreeSpillStorage(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      ForEachDefinition(instr, [=] (SSANode *node) {
        auto &storage(node->storage.Value());
        if (storage) {
          delete storage;
          storage = nullptr;
        }
      });
    }
  }
}

// Free up all SSA-related data structures stored in the fragment instructions.
static void FreeSSAData(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (IsA<NativeInstruction *>(instr)) {
        if (auto ssa_instr = GetMetaData<SSAInstruction *>(instr)) {
          delete ssa_instr;
          ClearMetaData(instr);
        }
      } else if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
        if (IA_SSA_NODE_DEF == ainstr->annotation) {
          delete ainstr->GetData<SSANode *>();
        }
      }
    }
  }
}

// Returns the `SSASpillStorage` object associated with an `SSANode`.
static SSASpillStorage *StorageOf(SSANode *node) {
  return node->storage.Value();
}

// Try to eliminate a redundant copy instruction.
static bool TryRemoveCopyInstruction(SSAFragment *frag,
                                     NativeInstruction *instr) {
  if (!GetCopiedOperand(instr)) return false;

  auto ssa_instr = GetMetaData<SSAInstruction *>(instr);
  auto dest_node = ssa_instr->defs[0].nodes[0];
  auto dest_loc = StorageOf(dest_node);

  // There has been a use of this node.
  if (dest_loc->checked_is_local) return false;

  // For transparency, we'll only remove copies that we've likely introduced.
  auto dest_reg = dest_node->reg;
  if (dest_reg.IsNative()) return false;

  // The node isn't used in this fragment, but is used in some future fragment.
  if (frag->ssa.exit_nodes.Exists(dest_reg) &&
      dest_loc == StorageOf(frag->ssa.exit_nodes[dest_reg])) {
    return false;
  }

  instr->UnsafeUnlink();
  delete dest_loc;
  delete ssa_instr;
  return true;
}

// Return the Nth architectural GPR.
static VirtualRegister NthArchGPR(int n) {
  return VirtualRegister(VR_KIND_ARCH_VIRTUAL, arch::GPR_WIDTH_BYTES,
                         static_cast<uint16_t>(n));
}

// Return the Nth spill slot.
static VirtualRegister NthSpillSlot(int n) {
  return VirtualRegister(VR_KIND_VIRTUAL_SLOT, arch::GPR_WIDTH_BYTES,
                         static_cast<uint16_t>(n));
}

// Returns a pointer to an `SSANode`s `SSASpillStorage` if the node is local to
// the current fragment. If the node is local, then a spill slot is allocated
// for the node.
static SSASpillStorage *LocalStorageForNode(SSAFragment *frag, SSANode *node) {
  auto storage = StorageOf(node);
  if (storage->is_local) {
    return storage;
  } else if (storage->checked_is_local) {
    return nullptr;
  } else {
    storage->checked_is_local = true;
    auto reg = node->reg;
    if (frag->ssa.exit_nodes.Exists(reg) &&
        storage == StorageOf(frag->ssa.exit_nodes[reg])) {
      return nullptr;
    }
    if (frag->ssa.entry_nodes.Exists(reg) &&
        storage == StorageOf(frag->ssa.entry_nodes[reg])) {
      return nullptr;
    }

    storage->is_local = true;
    storage->slot = frag->spill.AllocateSpillSlot();

    // Update the partition so that it knows how many local slots are allocated
    // when we get around to global allocation. `+1` is because it's the number
    // of slots, not the maximum slot (as slots numbers start at `0`).
    auto partition = frag->partition.Value();
    partition->num_local_slots = std::max(partition->num_local_slots,
                                          storage->slot + 1);
    partition->num_slots = partition->num_local_slots;
    return storage;
  }
}

enum {
  NUM_GPRS = arch::NUM_GENERAL_PURPOSE_REGISTERS
};

// Fragment-local register scheduler.
class LocalScheduler {
 public:
  explicit LocalScheduler(SSAFragment *frag_, int preferred_gpr_num_=-1)
      : frag(frag_),
        used_regs(),
        live_regs(),
        next_live_regs(frag->regs.live_on_exit),
        preferred_gpr_num(preferred_gpr_num_) {
    for (auto &homable : gpr_is_occupiable) homable = false;
    for (auto &vr : vr_occupying_gpr) vr = nullptr;
    for (auto &slot : slot_containing_gpr) slot = -1;
  }

  void HomeUsedGPRs(NativeInstruction *instr) {
    for (auto used_gpr : used_regs) {
      const auto n = used_gpr.Number();

      auto &vr(vr_occupying_gpr[n]);
      auto &slot(slot_containing_gpr[n]);
      if (!vr) continue;  // No VR stole this GPR.

      // Mark the storage for this node as not being backed.
      vr->reg = VirtualRegister();

      // This GPR will be restored from a slot, but the GPR is available to
      // be re-used by the VR system, therefore we need to save the GPR to
      // the slot, but we don't need to expect a def/use earlier in the
      // instruction stream.
      if (gpr_is_occupiable[n]) {
        frag->instrs.InsertAfter(
            instr, SaveGPRToSlot(vr->reg, NthSpillSlot(vr->slot)));
        vr = nullptr;
        slot = -1;
        gpr_is_occupiable[n] = false;
        continue;
      }

      // We expect a def/use earlier in the instruction stream, and so we will
      // try to move where the VR is currently located.

      auto new_gpr_num = -1;
      auto stole_new_gpr = TryStealGPR(new_gpr_num);
      auto new_gpr = NthArchGPR(new_gpr_num);
      auto spill_slot = NthSpillSlot(slot);

      if (-1 != slot) {  // `used_reg` was restored by later instructions.
        frag->instrs.InsertAfter(instr, SwapGPRWithSlot(new_gpr, spill_slot));
      }

      frag->instrs.InsertAfter(instr, SwapGPRWithGPR(used_gpr, new_gpr));

      if (!stole_new_gpr) {
        slot_containing_gpr[new_gpr_num] = vr->slot;
        // TODO(pag): Anything else?
      }

      vr_occupying_gpr[new_gpr_num] = vr;
      vr->reg = new_gpr;

      vr = nullptr;
      slot = -1;
    }
  }

  enum {
    NUM_USES_MAX = INT_MAX
  };

  int GetMinUsedDeadReg(int &gpr_num) {
    int lowest_num_uses = NUM_USES_MAX;
    for (auto n = NUM_GPRS - 1; n >= 0; --n) {
      if (vr_occupying_gpr[n] && !gpr_is_occupiable[n]) continue;  // In use.
      if (used_regs.IsLive(n)) {
        if (next_live_regs.IsLive(n)) {
          continue;  // Used in the instruction, and remains live.
        }
      } else if (live_regs.IsLive(n)) {
        continue;  // Not dead, and not killed by the instruction.
      }

      // Dead at a later instruction, or killed by this instruction.
      const auto n_num_uses = frag->regs.num_uses_of_gpr[n];
      if (n_num_uses < lowest_num_uses) {
        gpr_num = n;
        lowest_num_uses = n_num_uses;
      }
    }
    return lowest_num_uses;
  }

  int GetMinUsedUnusedReg(int &gpr_num) {
    int lowest_num_uses = NUM_USES_MAX;
    for (auto n = NUM_GPRS - 1; n >= 0; --n) {
      if (vr_occupying_gpr[n] && !gpr_is_occupiable[n]) continue;  // In use.
      if (used_regs.IsLive(n)) {
        continue;  // Used in the instruction.
      }
      const auto n_num_uses = frag->regs.num_uses_of_gpr[n];
      if (n_num_uses < lowest_num_uses) {
        gpr_num = n;
        lowest_num_uses = n_num_uses;
      }
    }
    GRANARY_ASSERT(lowest_num_uses < NUM_USES_MAX);
    GRANARY_ASSERT(-1 != gpr_num);
    return lowest_num_uses;
  }

  // Updates `gpr_num` to be some general-purpose register that will be used to
  // hold the value of some virtual register. Returns `true` if the GPR was dead
  // and has been stolen, and `false` if the GPR needs to be spilled/filled.
  bool TryStealGPR(int &gpr_num) {
    // Try to see if we can use the preferred GPR.
    if (-1 != preferred_gpr_num) {
      if (used_regs.IsLive(preferred_gpr_num)) {
        if (next_live_regs.IsDead(preferred_gpr_num)) {
          gpr_num = preferred_gpr_num;
          return true;  // Killed by this instruction.
        }
      } else {  // Not used in the instruction.
        gpr_num = preferred_gpr_num;
        return live_regs.IsDead(preferred_gpr_num);
      }
    }

    // No preferred GPR, or the preferred GPR is used in this instruction. Find
    // the next best thing.
    auto dead_reg_num = -1;
    auto unused_reg_num = -1;
    auto num_dead_reg_uses = GetMinUsedDeadReg(dead_reg_num);
    auto num_unused_reg_uses = GetMinUsedUnusedReg(unused_reg_num);
    if (num_unused_reg_uses < num_dead_reg_uses) {
      gpr_num = unused_reg_num;
      return false;
    } else {
      GRANARY_ASSERT(-1 != dead_reg_num);
      gpr_num = dead_reg_num;
      return true;
    }
  }

  // Tries to steal the "preferred" register that will be used to hold `vr`
  // across multiple fragments. The idea is that we assign a preferred GPR to
  // partition-local VRs, and these preferred GPRs act like a sort of calling
  // convention, in that they allow the partition-local scheduler to make
  // simplifying assumptions about the local of a VR across two fragments that
  // use the VR.
  void RestorePreferredGPR(Instruction *instr, SSASpillStorage *vr) {
    GRANARY_ASSERT(!used_regs.IsLive(preferred_gpr_num));
    vr_occupying_gpr[preferred_gpr_num] = vr;
    vr->reg = NthArchGPR(preferred_gpr_num);
    slot_containing_gpr[preferred_gpr_num] = vr->slot;
    frag->instrs.InsertAfter(
        instr, RestoreGPRFromSlot(vr->reg, NthSpillSlot(vr->slot)));
    next_live_regs.Revive(preferred_gpr_num);
  }

  // Tries to steal a register for use by `vr`. If one can be stolen, then `vr`
  // gets to use the stolen register. Otherwise, a GPR is filled from a spill
  // slot, `vr` gets to use that GPR for prior instructions. Either way, we
  // assign some native GPR to `vr`.
  void StealOrFillSpilledGPR(Instruction *instr, SSASpillStorage *vr) {
    auto gpr_num = -1;
    auto is_stolen = TryStealGPR(gpr_num);
    GRANARY_ASSERT(-1 != gpr_num);
    GRANARY_ASSERT(-1 != vr->slot);

    const auto was_occupiable = gpr_is_occupiable[gpr_num];

    // Are we re-using an already stolen slot? If so, mark the slot at no longer
    // occupiable and mantain our expected invariants.
    if (was_occupiable) {
      GRANARY_ASSERT(nullptr != vr_occupying_gpr[gpr_num]);
      if (-1 != slot_containing_gpr[gpr_num] &&
        vr->slot != slot_containing_gpr[gpr_num]) {
        frag->instrs.InsertBefore(
              instr, SaveGPRToSlot(vr->reg, NthSpillSlot(vr->slot)));
      }
      slot_containing_gpr[gpr_num] = -1;
      gpr_is_occupiable[gpr_num] = false;
    } else {
      GRANARY_ASSERT(-1 == slot_containing_gpr[gpr_num]);
    }

    vr_occupying_gpr[gpr_num] = vr;
    vr->reg = NthArchGPR(gpr_num);
    if (!is_stolen) {
      slot_containing_gpr[gpr_num] = vr->slot;
      if (!was_occupiable) {
        frag->instrs.InsertAfter(
            instr, RestoreGPRFromSlot(vr->reg, NthSpillSlot(vr->slot)));
      }
    }
  }

  // Mark the GPR associated with `vr->reg` as homed.
  void MarkGPRAsHomed(SSASpillStorage *vr) {
    auto gpr_num = vr->reg.Number();
    vr_occupying_gpr[gpr_num] = nullptr;
    slot_containing_gpr[gpr_num] = -1;
  }

  // Mark the GPR associated with `vr->reg` as occupiable. That is, a VR
  // previously occupied in, but no longer needs to occupy it.
  void MarkGPRAsOccupiable(SSASpillStorage *vr) {
    frag->spill.FreeSpillSlot(vr->slot);
    gpr_is_occupiable[vr->reg.Number()] = true;
  }

  SSAFragment *frag;

  UsedRegisterTracker used_regs;
  LiveRegisterTracker live_regs;
  LiveRegisterTracker next_live_regs;

  // The preferred reguster to steal, if it's not used.
  int preferred_gpr_num;

  bool gpr_is_occupiable[NUM_GPRS];
  SSASpillStorage *vr_occupying_gpr[NUM_GPRS];
  int slot_containing_gpr[NUM_GPRS];

 private:
  LocalScheduler(void) = delete;
};

// Replace a use of a virtual register
static void ReplaceOperand(SSAOperand &op) {
  GRANARY_ASSERT(1 == op.nodes.Size());
  auto node = op.nodes[0];
  auto storage = StorageOf(node);
  auto replacement_reg = storage->reg;
  GRANARY_ASSERT(node->reg.IsVirtual());
  GRANARY_ASSERT(replacement_reg.IsNative());
  Operand existing_op(op.operand);
  if (op.is_reg) {
    replacement_reg.Widen(op.operand->ByteWidth());
    RegisterOperand repl_op(replacement_reg);
    GRANARY_IF_DEBUG( auto replaced = ) existing_op.Ref().ReplaceWith(repl_op);
    GRANARY_ASSERT(replaced);
  } else {
    replacement_reg.Widen(arch::ADDRESS_WIDTH_BYTES);
    MemoryOperand repl_op(replacement_reg, op.operand->ByteWidth());
    GRANARY_IF_DEBUG( auto replaced = ) existing_op.Ref().ReplaceWith(repl_op);
    GRANARY_ASSERT(replaced);
  }
}

// Handle the special case where we're copying a GPR to a VR. This is
// targeted at cases where a flag zone contains only a single code
// fragment, and where the flags save/restore code is inlined into that
// fragment instead of being part of a flag entry/exit fragment.
static bool SpecialCaseCopyGPRToVR(LocalScheduler *sched,
                                   NativeInstruction *instr,
                                   SSAInstruction *ssa_instr,
                                   SSASpillStorage *vr) {
  auto gpr_copied_to_vr = GPRCopiedToVR(instr);
  if (!gpr_copied_to_vr.IsValid()) return false;

  auto new_copy_instr = SaveGPRToSlot(gpr_copied_to_vr, NthSpillSlot(vr->slot));
  sched->frag->instrs.InsertBefore(instr, new_copy_instr);
  SetMetaData(new_copy_instr, ssa_instr);
  ClearMetaData(instr);
  sched->frag->instrs.Remove(instr);
  delete instr;
  return true;
}

// Perform fragment-local register scheduling.
static void ScheduleFragLocalRegDefs(LocalScheduler *sched,
                                     NativeInstruction *instr,
                                     SSAInstruction *ssa_instr) {
  for (auto &def : ssa_instr->defs) {
    for (auto node : def.nodes) {
      if (!node->reg.IsVirtual()) continue;
      auto vr = LocalStorageForNode(sched->frag, node);
      if (!vr) continue;
      if (!vr->reg.IsValid()) {  // Need to allocate a register for `vr`.
        if (SpecialCaseCopyGPRToVR(sched, instr, ssa_instr, vr)) {
          return;
        }
        sched->StealOrFillSpilledGPR(instr, vr);
      }
      if (SSAOperandAction::WRITE == def.action) {
        sched->MarkGPRAsOccupiable(vr);
      }
      ReplaceOperand(def);
    }
  }
}

// Handle the special case where we're copying a VR to a GPR. This is
// targeted at cases where a flag zone contains only a single code
// fragment, and where the flags save/restore code is inlined into that
// fragment instead of being part of a flag entry/exit fragment.
static bool SpecialCaseCopyVRToGPR(LocalScheduler *sched,
                                   NativeInstruction *instr,
                                   SSAInstruction *ssa_instr,
                                   SSANode *node,
                                   SSASpillStorage *vr) {
  // There are no intermediate uses of this register between this use and its
  // definition.
  auto reg_node = DynamicCast<SSARegisterNode *>(UnaliasedNode(node));
  if (!reg_node) return false;

  // The register is defined by a native instruction in this fragment.
  auto reg_def_instr = DynamicCast<NativeInstruction *>(reg_node->instr);
  if (!reg_def_instr) return false;

  // The native instruction that defines this register does so as a copy of
  // a GPR into the VR.
  if (!GPRCopiedToVR(reg_def_instr).IsValid()) return false;

  // This use of the VR is simple a restoration of whatever was saved by the
  // instruction that defines this VR.
  auto gpr_copied_to_vr = GPRCopiedFromVR(instr);
  if (!gpr_copied_to_vr.IsValid()) return false;

  auto new_copy_instr = RestoreGPRFromSlot(gpr_copied_to_vr,
                                           NthSpillSlot(vr->slot));
  sched->frag->instrs.InsertBefore(instr, new_copy_instr);
  SetMetaData(new_copy_instr, ssa_instr);
  ClearMetaData(instr);
  sched->frag->instrs.Remove(instr);
  delete instr;
  return true;
}

// Perform fragment-local register scheduling.
static void ScheduleFragLocalRegUses(LocalScheduler *sched,
                                     NativeInstruction *instr,
                                     SSAInstruction *ssa_instr) {
  for (auto &use : ssa_instr->uses) {
    auto replace = false;
    for (auto node : use.nodes) {
      if (!node->reg.IsVirtual()) continue;
      auto vr = LocalStorageForNode(sched->frag, node);
      if (!vr) continue;
      if (!vr->reg.IsValid()) {  // Need to allocate a register for `vr`.
        if (SpecialCaseCopyVRToGPR(sched, instr, ssa_instr, node, vr)) {
          return;
        }
        sched->StealOrFillSpilledGPR(instr, vr);
      }
      replace = true;
    }
    if (replace) ReplaceOperand(use);
  }
}

// Fill any registers that remain stolen after scheduling fragment-local VRs.
static void FillRemainingStolenGPRs(const LocalScheduler &sched) {
  auto gpr_num = 0;
  auto instr = sched.frag->instrs.First();
  for (auto slot : sched.slot_containing_gpr) {
    if (-1 != slot) {
      auto vr = sched.vr_occupying_gpr[gpr_num];
      GRANARY_ASSERT(vr->reg.IsNative() && vr->reg.IsGeneralPurpose());
      GRANARY_ASSERT(gpr_num == vr->reg.Number());
      GRANARY_ASSERT(-1 != vr->slot);
      sched.frag->instrs.InsertBefore(
          instr, SaveGPRToSlot(vr->reg, NthSpillSlot(vr->slot)));
    }
    gpr_num += 1;
  }
}

// Does fragment-local register allocation and scheduling for a given fragment.
static void ScheduleFragLocalRegs(SSAFragment *frag) {
  LocalScheduler sched(frag);
  Instruction *prev_instr(nullptr);
  for (auto instr = frag->instrs.Last(); instr; instr = prev_instr) {
    prev_instr = instr->Previous();
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (TryRemoveCopyInstruction(frag, ninstr))  {
        continue;  // Removed a redundant copy operation, `ninstr` now invalid.
      }

      sched.live_regs = sched.next_live_regs;
      sched.next_live_regs.Visit(ninstr);

      sched.used_regs.KillAll();
      sched.used_regs.Visit(ninstr);
      sched.HomeUsedGPRs(ninstr);

      if (auto ssa_instr = GetMetaData<SSAInstruction *>(ninstr)) {
        ScheduleFragLocalRegDefs(&sched, ninstr, ssa_instr);
        ScheduleFragLocalRegUses(&sched, ninstr, ssa_instr);
      }
    }
  }
  FillRemainingStolenGPRs(sched);
}

// Perform fragment-local register allocation and scheduling.
static void ScheduleFragLocalRegs(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto ssa_frag = DynamicCast<SSAFragment *>(frag)) {
      ssa_frag->regs.CountGPRUses(ssa_frag);
      ScheduleFragLocalRegs(ssa_frag);
    }
  }
}

// For each shared VR that enters/exits a given fragment, apply a function
// `func`.
template <typename T>
static void ForEachSharedVR(SSAFragment *frag, T func) {
  for (auto entry_node : frag->ssa.entry_nodes.Values()) {
    if (entry_node->reg.IsVirtual()) {
      func(entry_node, StorageOf(entry_node));
    }
  }
  for (auto exit_node : frag->ssa.exit_nodes.Values()) {
    if (exit_node->reg.IsVirtual()) {
      func(exit_node, StorageOf(exit_node));
    }
  }
}

class SlotScheduler {
 public:
  SlotScheduler(SSAFragment *frag_, SSASpillStorage *vr, int preferred_gpr_num_)
      : frag(frag_),
        entry_node(nullptr),
        exit_node(nullptr),
        slot_reg(),
        preferred_gpr_num(preferred_gpr_num_),
        preferred_steal_gpr(NthArchGPR(preferred_gpr_num)) {
    GRANARY_ASSERT(0 <= preferred_gpr_num && NUM_GPRS > preferred_gpr_num);

    // Find the entry node.
    for (auto node : frag->ssa.entry_nodes.Values()) {
      if (node->reg.IsVirtual() && StorageOf(node) == vr) {
        entry_node = node;
        slot_reg = node->reg;
        break;
      }
    }

    // Find the exit node.
    for (auto node : frag->ssa.exit_nodes.Values()) {
      if (node->reg.IsVirtual() && StorageOf(node) == vr) {
        exit_node = node;
        slot_reg = node->reg;
        break;
      }
    }

    GRANARY_ASSERT(slot_reg.IsVirtual());
  }

  SSAFragment * const frag;

  // The SSA variable associated with this location on entry to the fragment.
  SSANode *entry_node;

  // The SSA variable associated with this location on exit from this fragment.
  SSANode *exit_node;

  // The virtual register associated with this slot.
  VirtualRegister slot_reg;

  // The GPR that we prefer to steal for use by this slot.
  int preferred_gpr_num;
  VirtualRegister preferred_steal_gpr;

 private:
  SlotScheduler(void) = delete;
};

// Perform fragment-local register scheduling.
static void SchedulePartitionLocalRegDef(LocalScheduler *sched,
                                         NativeInstruction *instr,
                                         SSAInstruction *ssa_instr,
                                         SSASpillStorage *vr) {
  for (auto &def : ssa_instr->defs) {
    for (auto node : def.nodes) {
      if (!node->reg.IsVirtual()) continue;
      if (StorageOf(node) != vr) continue;
      if (!vr->reg.IsNative()) {  // Need to allocate a register for `vr`.
        sched->StealOrFillSpilledGPR(instr, vr);
      }
      if (SSAOperandAction::WRITE == def.action) {
        sched->frag->instrs.InsertBefore(
            instr, SaveGPRToSlot(vr->reg, NthSpillSlot(vr->slot)));
        sched->MarkGPRAsHomed(vr);
      }
      ReplaceOperand(def);
    }
  }
}

// Perform fragment-local register scheduling.
static void SchedulePartitionLocalRegUse(LocalScheduler *sched,
                                         NativeInstruction *instr,
                                         SSAInstruction *ssa_instr,
                                         SSASpillStorage *vr) {
  for (auto &use : ssa_instr->uses) {
    for (auto node : use.nodes) {
      if (!node->reg.IsVirtual()) continue;
      if (StorageOf(node) != vr) continue;
      if (!vr->reg.IsNative()) {  // Need to allocate a register for `vr`.
        sched->StealOrFillSpilledGPR(instr, vr);
      }
      ReplaceOperand(use);
    }
  }
}

// Schedule a slot from the bottom-up.
static void SchedulePartitionLocalReg(const SlotScheduler &sched,
                                      SSAFragment *frag, SSASpillStorage *vr) {
  const auto spill_slot = NthSpillSlot(vr->slot);
  const auto preferred_gpr_num = sched.preferred_gpr_num;

  LocalScheduler local_sched(frag, sched.preferred_gpr_num);
  const auto preferred_gpr_is_busy = frag->spill.gprs_holding_vrs.IsLive(
      preferred_gpr_num);

  // Mark the preferred GPR as busy in this fragment. This is so that the
  // same GPR can't simultaneously be "busy" (i.e. contain) the value of
  // two different virtual registers on exit/entry from a fragment.
  frag->spill.gprs_holding_vrs.Revive(preferred_gpr_num);

  // If the VR is live on exit from this frag, then we assume that any
  // the VR is assumed to be located in the preferred GPR on entry to any
  // successors. We don't make this assumption if the preferred GPR has
  // already been marked as busy.
  if (sched.exit_node && !preferred_gpr_is_busy) {
    vr->reg = sched.preferred_steal_gpr;
    local_sched.slot_containing_gpr[preferred_gpr_num] = vr->slot;
    local_sched.vr_occupying_gpr[preferred_gpr_num] = vr;

  // Not live on exit, or it is live, and so its also busy.
  } else {
    vr->reg = spill_slot;
  }

  Instruction *prev_instr(nullptr);
  for (auto instr = frag->instrs.Last(); instr; instr = prev_instr) {
    prev_instr = instr->Previous();

    auto ninstr = DynamicCast<NativeInstruction *>(instr);
    if (!ninstr) {
      // Handle compensation instructions.
      if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
        if (IA_SSA_NODE_UNDEF == ainstr->annotation) {
          auto node_vr = StorageOf(ainstr->GetData<SSANode *>());
          if (node_vr == vr) {
            local_sched.RestorePreferredGPR(ainstr, vr);
          }
        }
      }
      continue;
    }

    local_sched.live_regs = local_sched.next_live_regs;
    local_sched.next_live_regs.Visit(ninstr);

    local_sched.used_regs.KillAll();
    local_sched.used_regs.Visit(ninstr);
    local_sched.HomeUsedGPRs(ninstr);

    auto ssa_instr = GetMetaData<SSAInstruction *>(ninstr);
    if (!ssa_instr) continue;

    SchedulePartitionLocalRegDef(&local_sched, ninstr, ssa_instr, vr);
    SchedulePartitionLocalRegUse(&local_sched, ninstr, ssa_instr, vr);
  }

  // It's not live on entry, make sure to spill the register to the necessary
  // slot.
  if (!sched.entry_node) {
    FillRemainingStolenGPRs(local_sched);

  // It's live on entry, but it's not in its preferred place. Need to maintain
  // the necessary invariants.
  } else if (vr->reg != sched.preferred_steal_gpr) {
    if (vr->reg.IsVirtualSlot()) {
      if (preferred_gpr_is_busy) return; // It should be left in its slot.

      // Predecessor assumes its stolen, so we need to restore native GPR from
      // its slot.
      frag->instrs.Prepend(RestoreGPRFromSlot(sched.preferred_steal_gpr,
                                              vr->reg));

    } else if (vr->reg.IsNative()) {
      if (preferred_gpr_is_busy) {  // Need to put it into its slot.
        frag->instrs.Prepend(SwapGPRWithSlot(vr->reg, spill_slot));

      } else {  // Need to put it into its preferred GPR.
        frag->instrs.Prepend(SwapGPRWithSlot(sched.preferred_steal_gpr,
                                             spill_slot));
        frag->instrs.Prepend(SwapGPRWithGPR(vr->reg,
                                            sched.preferred_steal_gpr));
      }
    } else {
      GRANARY_ASSERT(false);
    }
  }
}

// Returns the `SSASpillStorage` associated with either of an entry/exit VR
// used in `frag`, or `nullptr`.
static bool FragUsesVR(SSAFragment *frag, SSASpillStorage *vr) {
  auto is_used = false;
  ForEachSharedVR(frag, [&] (SSANode *, SSASpillStorage *node_vr) {
    if (vr == node_vr) {
      is_used = true;
    }
  });
  return is_used;
}

// Update the partition info with the count of the number of uses of every GPR
// in every fragment of each partition.
static void CountNumRegUses(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto partition = frag->partition.Value();
    partition->ClearGPRUseCounters();
  }
  for (auto frag : FragmentListIterator(frags)) {
    auto partition = frag->partition.Value();
    partition->CountGPRUses(frag);
  }
}

static SSASpillStorage *GetUnscheduledVR(SSAFragment *frag) {
  SSASpillStorage *vr(nullptr);
  ForEachSharedVR(frag, [&] (SSANode *, SSASpillStorage *node_vr) {
    if (-1 == node_vr->slot && !vr) {
      vr = node_vr;
    }
  });

  if (!vr) {  // No remaining unscheduled VRs.
    frag->all_regs_scheduled = true;
  }

  return vr;
}

// Schedule all virtual registers that are used in one or more fragments. By
// this point they should all be allocated. One challenge for scheduling is that
// a virtual register might be placed in two different physical registers
// across in two or more successors of a fragment, and needs to be in the same
// spot in the fragment itself.
static void SchedulePartitionLocalRegs(FragmentList *frags) {
  for (auto allocated = true; allocated; ) {
    allocated = false;

    // Continually update the register counts, as scheduling will change
    // the counts, and thus change our preferences.
    CountNumRegUses(frags);

    for (auto frag : ReverseFragmentListIterator(frags)) {
      auto ssa_frag = DynamicCast<SSAFragment *>(frag);
      if (!ssa_frag) continue;  // Doesn't use VRs.
      if (ssa_frag->all_regs_scheduled) continue;

      auto partition = ssa_frag->partition.Value();
      auto &vr(partition->vr_being_scheduled);
      auto found_vr = false;

      if (!vr) {
        if (!(vr = GetUnscheduledVR(ssa_frag))) continue;
        allocated = true;
        found_vr = true;
        GRANARY_ASSERT(-1 == vr->slot);
        vr->slot = ssa_frag->spill.AllocateSpillSlot(
            partition->num_local_slots);

        // Keep track of how many slots have been allocated to this partition.
        // This is used later when actually allocating the slots on the stack
        // or globally.
        partition->num_slots = std::max(partition->num_slots, vr->slot + 1);
      }

      if (found_vr || FragUsesVR(ssa_frag, vr)) {

        // Mark this slot as used in every fragment where it appears as either
        // live on entry or live on exit. Because of the way that live on entry/
        // exit was built (via data flow), we should get a natural cover of the
        // transitive closure of all potentially simultaneously live reg (at
        // the fragment granularity).
        ssa_frag->spill.MarkSlotAsUsed(vr->slot);

        auto preferred_gpr_num = partition->PreferredGPRNum();
        SlotScheduler sched(ssa_frag, vr, preferred_gpr_num);
        SchedulePartitionLocalReg(sched, ssa_frag, vr);
      }
    }

    // If we allocated any VRs, then make sure we reset the field representing
    // the current VR being allocated in each partition.
    if (allocated) {
      for (auto frag : ReverseFragmentListIterator(frags)) {
        auto partition = frag->partition.Value();
        partition->vr_being_scheduled = nullptr;
      }
    }
  }
}

// Frees all flag zone data structures.
static void FreeFlagZones(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto &flag_zone(frag->flag_zone.Value());
    if (flag_zone) {
      delete flag_zone;
      flag_zone = nullptr;
    }
  }
}

}  // namespace

// Schedule virtual registers.
void ScheduleRegisters(FragmentList *frags) {
  AllocateSpillStorage(frags);
  ScheduleFragLocalRegs(frags);
  SchedulePartitionLocalRegs(frags);
  FreeSpillStorage(frags);
  FreeSSAData(frags);
  FreeFlagZones(frags);
}

}  // namespace granary
