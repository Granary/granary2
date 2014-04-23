/* Copyright 2014 Peter Goodman, all rights reserved. */
#if 0
#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"
#include "granary/base/bitset.h"
#include "granary/base/list.h"
#include "granary/base/new.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/ssa.h"

#include "granary/util.h"

namespace granary {

// Returns true if this instruction is a copy instruction.
//
// Note: This has an architecture-specific implementation.
extern bool IsCopyInstruction(const NativeInstruction *instr);

// Speculates on whether or not a particular instruction selection exists for
// some set of explicit operands. Returns true if we thing the selection does
// exist.
//
// Note: This has an architecture-specific implementation.
bool TryReplaceOperand(const NativeInstruction *ninstr,
                       const Operand *op, Operand *repl_op);

// Create an instruction to copy a GPR to a spill slot.
//
// Note: This has an architecture-specific implementation.
std::unique_ptr<Instruction> SaveGPRToSlot(VirtualRegister gpr,
                                           VirtualRegister slot);

// Create an instruction to copy the value of a spill slot to a GPR.
//
// Note: This has an architecture-specific implementation.
std::unique_ptr<Instruction> RestoreGPRFromSlot(VirtualRegister gpr,
                                                VirtualRegister slot);

// Represents a storage location for a virtual register. Uses the union-find
// algorithm to settle on a canonical storage location for all virtual registers
// used/defined within a given register web.
class RegisterLocation {
 public:
  inline explicit RegisterLocation(RegisterLocation *next_)
      : next(next_),
        spill_slot(-1),
        reg(),
        parent(this),
        rank(0) {}

  // Find the canonical storage location associated with the current storage
  // location.
  RegisterLocation *Find(void) {
    if (parent != this) {
      parent = parent->Find();
    }
    return parent;
  }

  // Union together two storage locations.
  void UnionWith(RegisterLocation *that) {
    auto this_root = Find();
    auto that_root = that->Find();
    if (this_root != that_root) {
      if (this_root->rank < that_root->rank) {
        this_root->parent = that_root;
      } else if (this_root->rank > that_root->rank) {
        that_root->parent = this_root;
      } else {
        that_root->parent = this_root;
        this_root->rank = that_root->rank + 1;
      }
    }
  }

  GRANARY_DEFINE_NEW_ALLOCATOR(RegisterLocation, {
    SHARED = false,
    ALIGNMENT = 1
  })

  // All storage locations are chained together into a simple linked list for
  // later memory reclamation.
  RegisterLocation *next;

  // To what spill slot is this storage location assigned?
  int spill_slot;

  VirtualRegister reg;

 private:
  RegisterLocation(void) = delete;

  RegisterLocation *parent;
  int rank;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(RegisterLocation);
};

namespace {

// Returns the Nth architectural virtual register.
static VirtualRegister NthArchGPR(int n) {
  GRANARY_ASSERT(0 <= n && arch::NUM_GENERAL_PURPOSE_REGISTERS > n);
  return VirtualRegister(VR_KIND_ARCH_VIRTUAL, arch::GPR_WIDTH_BYTES,
                         static_cast<uint16_t>(n));
}

// The backing storage of a general purpose register, as well as liveness info
// about that GPR.
struct GPRLocation {

  // Current location of this native GPR. This will either be the GPR itself
  // or a spill slot VR.
  VirtualRegister reg;

  // The storage location associated with the virtual register web that stole
  // the GPR (assuming it was indeed stolen).
  RegisterLocation *stolen_by;

  // Whether or not this GPR is live at the current instruction.
  bool is_live;
};

// Locations of where GPRs are currently stored.
struct GPRLocations {
  GPRLocations(void) {
    for (auto i = 0; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
      location[i].reg = NthArchGPR(i);
      location[i].stolen_by = nullptr;
      location[i].is_live = true;  // TODO(pag): Integrate liveness info.
    }
  }

  GPRLocation location[arch::NUM_GENERAL_PURPOSE_REGISTERS];
};

// Tracks whether or not an instruction using a specific architectural GPR.
typedef BitSet<arch::NUM_GENERAL_PURPOSE_REGISTERS> RegUseTracker;

// Packages the essential data used in the scheduler.
struct RegisterScheduler {
 public:
  explicit RegisterScheduler(Fragment *frag_)
      : frag(frag_),
        gprs_used_in_instr(),
        gpr_storage_locs(),
        global_slots(),
        max_num_used_local_slots(0) {}

  inline GPRLocation &LocationOfGPR(int i) {
    return gpr_storage_locs.location[i];
  }

  inline GPRLocation &LocationOfGPR(VirtualRegister gpr) {
    GRANARY_ASSERT(gpr.IsNative());
    return gpr_storage_locs.location[gpr.Number()];
  }

  inline bool GPRIsUsedInInstr(int i) {
    return gprs_used_in_instr.Get(i);
  }

  inline bool GPRIsUsedInInstr(VirtualRegister gpr) {
    GRANARY_ASSERT(gpr.IsNative());
    return gprs_used_in_instr.Get(gpr.Number());
  }

  Fragment *frag;
  RegUseTracker gprs_used_in_instr;
  GPRLocations gpr_storage_locs;

  BitSet<MAX_NUM_LIVE_VIRTUAL_REGS> global_slots;
  uint8_t max_num_used_local_slots;
};

// Returns the current storage location for an SSAVariable.
inline RegisterLocation *LocationOf(SSAVariable *var) {
  return var->loc->Find();
}

// Assign a storage location to a variable.
static RegisterLocation *AssignRegisterLocation(SSAVariable *var,
                                                RegisterLocation *next) {
  auto def = DefinitionOf(var);
  auto forward_var = DynamicCast<SSAForward *>(var);
  auto forward_def = forward_var ? DefinitionOf(forward_var->parent)
                                 : nullptr;

  // First, guarantee that if this is a forward def, that the thing being
  // forward defined has a location.
  if (forward_def) {
    if (!forward_def->loc) {
      forward_def->loc = new RegisterLocation(next);
    }
  }

  // Next, make sure out var has a location, even if it is a forward def.
  if (!def->loc) {
    next = new RegisterLocation(next);
    def->loc = next;
  }

  // Finally, if `var` is a `SSAForward`, then by construction the forward
  // defined variable now also has location, which is unioned with our
  // variable's location.
  if (forward_def) {
    def->loc->UnionWith(forward_def->loc);
  }

  // If `var` is a `TrivialPhi` then it might not be def. Also, `var` might
  // already have a location assigned to it because it was forward defined
  // (really backward when you think of it, because it's forward when going
  // through the instructions in reverse order).
  if (var != def) {
    if (var->loc) {
      var->loc->UnionWith(def->loc);
    } else {
      var->loc = def->loc;
    }
  }
  return next;
}

// Apply a function to each SSA variable defined by an instruction.
static void ForEachDefinitionImpl(NativeInstruction *instr,
                                  std::function<void(SSAVariable *)> &func) {
  if (auto var = GetMetaData<SSAVariable *>(instr)) {
    while (auto forward_def = DynamicCast<SSAForward *>(var)) {
      func(var);
      var = forward_def->next_instr_def;
    }
    if (var) {
      func(var);
    }
  }
}

// Apply a function to each SSA variable defined by an instruction.
template <typename T>
static inline void ForEachDefinition(NativeInstruction *instr, T clos) {
  std::function<void(SSAVariable *)> func(std::cref(clos));
  ForEachDefinitionImpl(instr, func);
}

// Assign storage locations to all variables defined by this instruction.
static RegisterLocation *AssignInstrDefRegisterLocations(
    NativeInstruction *instr, RegisterLocation *next) {
  ForEachDefinition(instr, [&] (SSAVariable *var) {
    next = AssignRegisterLocation(var, next);
  });
  return next;
}

// Assign a storage location to every PHI node, then try to assign and union
// with the storage locations of all operands of the PHI nodes.
static RegisterLocation *AssignPhiRegisterLocations(Fragment * const frag,
                                                    RegisterLocation *next) {
  for (auto entry_def : frag->ssa_vars->EntryDefs()) {
    if (auto var = DefinitionOf(entry_def.var)) {
      next = AssignRegisterLocation(var, next);
      auto loc = var->loc;
      if (auto phi = DynamicCast<SSAPhi *>(var)) {
        for (auto op : phi->Operands()) {
          auto op_var = op->Variable();
          next = AssignRegisterLocation(op_var, next);
          loc->UnionWith(op_var->loc);
        }
      }
    }
  }
  return next;
}

// Assign storage locations to all variables.
static RegisterLocation *AssignRegisterLocations(Fragment * const frags) {
  RegisterLocation *next(nullptr);
  // Assign a storage location to every "bare" definition.
  for (auto frag : FragmentIterator(frags)) {
    for (auto instr : ForwardInstructionIterator(frag->first)) {
      if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
        next = AssignInstrDefRegisterLocations(ninstr, next);
      }
    }
  }
  for (auto frag : FragmentIterator(frags)) {
    if (frag->ssa_vars) {
      next = AssignPhiRegisterLocations(frag, next);
    }
  }
  return next;
}

// Find the local definition of a particular register by scanning the
// instruction list of a fragment in reverse order, starting at
// `instr_using_reg`.
static SSAVariable *FindLocalInstrDefinitionOf(Instruction *instr_using_reg,
                                               VirtualRegister reg) {
  SSAVariable *def(nullptr);
  for (auto instr : BackwardInstructionIterator(instr_using_reg)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if ((def = DefinitionOf(ninstr, reg)) && IsA<SSARegister *>(def)) {
        return def;
      }
    }
  }
  return nullptr;
}

// Returns true if the definition `def` of the register `def_reg` within the
// fragment `frag` escapes the fragment.
static bool DefinitionEscapesFragment(Fragment *frag, SSAVariable *def,
                                      VirtualRegister def_reg) {
  if (auto exit_def = frag->ssa_vars->ExitDefinitionOf(def_reg)) {
    return LocationOf(def) == LocationOf(exit_def);  // Escapes.
  }
  return false;
}

// Find the definitions of the registers used by a particular instruction.
static SSAVariable *LocalDefinitionOf(Fragment *frag,
                                      Instruction *instr_using_reg,
                                      VirtualRegister reg) {
  // If we've got a definition, then make sure that definition doesn't escape.
  if (auto def = FindLocalInstrDefinitionOf(instr_using_reg, reg)) {
    if (!DefinitionEscapesFragment(frag, def, reg)) {
      return def;  // No exit definition, or different exit definition.
    }
  }
  return nullptr;
}

// Returns the Nth spill slot as a virtual register.
static VirtualRegister NthSpillSlot(int spill_slot) {
  GRANARY_ASSERT(0 <= spill_slot && MAX_NUM_LIVE_VIRTUAL_REGS > spill_slot);
  return VirtualRegister(VR_KIND_VIRTUAL_SLOT, arch::GPR_WIDTH_BYTES,
                           static_cast<uint16_t>(spill_slot));
}

// Returns the virtual spill slot register associated with a storage location
// for a register, regardless of if the backing storage of the location is
// a spill slot or a native GPR.
static VirtualRegister VirtualSpillOf(RegisterLocation *loc) {
  return NthSpillSlot(loc->spill_slot);
}

// Allocate a spill slot from this fragment for a fragment-local virtual
// register.
static VirtualRegister AllocateVirtualSlot(Fragment *frag,
                                           RegisterLocation *loc) {
  for (uint8_t i(0); i < MAX_NUM_LIVE_VIRTUAL_REGS; ++i) {
    const auto mask = 1U << i;
    if (!(frag->spill_slot_allocated_mask & mask)) {
      frag->spill_slot_allocated_mask |= mask;
      frag->num_spill_slots = std::max(i, frag->num_spill_slots);
      loc->spill_slot = static_cast<int>(i);
      loc->reg = VirtualSpillOf(loc);
      return loc->reg;
    }
  }
  GRANARY_ASSERT(false);
  return VirtualRegister();
}

// Get the backing storage location for a given fragment-local virtual register.
static VirtualRegister BackingStorageOf(Fragment *frag, RegisterLocation *loc) {
  if (!loc->reg.IsValid()) {
    loc->reg = AllocateVirtualSlot(frag, loc);
  }
  return loc->reg;
}

// Ensure that a GPR is homed for its use in `using_instr`. If the GPR is not
// homed, then it's located in a spill slot. What we mean by it's located in
// a spill slot is that we assume that it's located there, but nothing has
// actually placed it there. Therefore, the act of homing a register actually
// involves copying the register to the spill slot after `using_instr`, so that
// later code that restores the register from a spill slot is restoring the
// right value. This might seem funny, but recall that we are visiting
// instructions in reverse order. Read the following diagram from bottom-to-top:
//
//          using_instr <-- instruction using `gpr`
//          spill = gpr <-- what we want to inject, it steals `gpr`.
//          ...
//          X           <-- instruction using VR, where VR is stored in `gpr`.
//          gpr = spill <-- restore of `gpr`, which can now be assumed stolen.
static void FillGPRForDefUse(RegisterScheduler *sched, Instruction *using_instr,
                             VirtualRegister gpr, bool is_live) {
  if (gpr.IsNative() && gpr.IsGeneralPurpose()) {
    auto &gpr_loc(sched->LocationOfGPR(gpr));
    if (!gpr_loc.stolen_by || !gpr_loc.is_live) {
      GRANARY_ASSERT(gpr_loc.reg == gpr);
      return;  // It's homed, or it wasn't live after this instruction.
    }
    sched->frag->InsertAfter(using_instr, SaveGPRToSlot(gpr, gpr_loc.reg));
    gpr_loc.reg = gpr;
    gpr_loc.stolen_by->reg = VirtualSpillOf(gpr_loc.stolen_by);
    gpr_loc.stolen_by = nullptr;
    gpr_loc.is_live = is_live;
  }
}

// Ensure that a GPR is homed for its use in `using_instr`.
static void FillGPRForUse(RegisterScheduler *sched, Instruction *using_instr,
                          VirtualRegister gpr) {
  FillGPRForDefUse(sched, using_instr, gpr, true);
}

// Ensure that a GPR is homed for its def in `def_instr`.
static void FillGPRForDef(RegisterScheduler *sched, Instruction *def_instr,
                          VirtualRegister gpr) {
  FillGPRForDefUse(sched, def_instr, gpr, false);
}

// Free a spill slot for use by another fragment-local register within the
// current fragment.
static void FreeVirtualSlot(RegisterScheduler *sched,
                            Instruction *instr_before_var_kill,
                            RegisterLocation *var_loc) {
  auto &var_reg(var_loc->reg);
  if (var_reg.IsValid()) {
    if (var_reg.IsNative()) {
      FillGPRForUse(sched, instr_before_var_kill, var_reg);
    }
    const auto mask = 1U << var_loc->spill_slot;
    GRANARY_ASSERT(0 != (sched->frag->spill_slot_allocated_mask & mask));
    sched->frag->spill_slot_allocated_mask &= ~mask;
    var_reg = VirtualRegister();
  }
}

// Try to steal a dead register. Returns `true` if a dead register was stolen,
// and updates `out_reg` to be the stolen register, otherwise returns `false`
// and leaves `out_reg` as-is.
static bool TryFindStealableDeadRegister(RegisterScheduler *sched,
                                         VirtualRegister *out_reg) {
  for (int i = arch::NUM_GENERAL_PURPOSE_REGISTERS; i--; ) {
    auto &loc(sched->LocationOfGPR(i));
    if (!loc.is_live && !loc.stolen_by && !sched->GPRIsUsedInInstr(loc.reg)) {
      *out_reg = loc.reg;
      return true;
    }
  }
  return false;
}

// Spill a register that isn't being used by the current instruction and is
// not already stolen. This updates `out_reg` in place.
static VirtualRegister FindSpillableRegister(RegisterScheduler *sched) {
  for (int i = arch::NUM_GENERAL_PURPOSE_REGISTERS; i--; ) {
    auto &ith_gpr_loc(sched->LocationOfGPR(i));
    if (!ith_gpr_loc.stolen_by && !sched->GPRIsUsedInInstr(i)) {
      return ith_gpr_loc.reg;
    }
  }
  GRANARY_ASSERT(false);
  return VirtualRegister();
}

// Force a virtual register into a general-purpose register. This will try to
// get the virtual register into a general purpose register by stealing a
// physical register, and if none can be stolen, then it will spill a physical
// register.
static void ForceVRToGPR(RegisterScheduler *sched,
                         NativeInstruction * const instr,
                         RegisterLocation *reg_loc) {
  VirtualRegister gpr;
  if (!TryFindStealableDeadRegister(sched, &gpr)) {
    gpr = FindSpillableRegister(sched);
    sched->frag->InsertAfter(instr, RestoreGPRFromSlot(gpr, reg_loc->reg));
  }

  auto &gpr_loc(sched->LocationOfGPR(gpr));
  GRANARY_ASSERT(!gpr_loc.stolen_by);
  gpr_loc.stolen_by = reg_loc;
  gpr_loc.reg = reg_loc->reg;
  reg_loc->reg = gpr;
}

// Schedule a fragment-local virtual register for use in either a register or
// a memory operand.
static VirtualRegister GetScheduledFragLocalReg(RegisterScheduler *sched,
                                                NativeInstruction * const instr,
                                                SSAVariable *var) {
  auto reg_loc = LocationOf(var);
  auto reg_storage = BackingStorageOf(sched->frag, reg_loc);

  // If the current storage for `reg` is a spill slot, then force it into a
  // register.
  if (reg_storage.IsVirtualSlot()) {
    ForceVRToGPR(sched, instr, reg_loc);
    reg_storage = reg_loc->reg;
  }
  GRANARY_ASSERT(reg_storage.IsNative() && reg_storage.IsGeneralPurpose());
  return reg_storage;
}

// Schedule a fragment-local virtual register operand that is a register use.
static void ScheduleFragLocalRegOp(RegisterScheduler *sched,
                                   NativeInstruction * const instr,
                                   RegisterOperand *op, SSAVariable *var) {
  auto reg = op->Register();
  auto reg_storage = GetScheduledFragLocalReg(sched, instr, var);
  reg_storage.Widen(reg.ByteWidth());
  RegisterOperand repl_op(reg_storage);
  GRANARY_IF_DEBUG(auto replaced = ) op->Ref().ReplaceWith(repl_op);
  GRANARY_ASSERT(replaced);
}

// Schedule a fragment-local virtual register operand that is a memory use.
static void ScheduleFragLocalMemOp(RegisterScheduler *sched,
                                   NativeInstruction * const instr,
                                   MemoryOperand *op, SSAVariable *var) {
  auto reg_storage = GetScheduledFragLocalReg(sched, instr, var);
  reg_storage.Widen(arch::ADDRESS_WIDTH_BYTES);
  MemoryOperand repl_op(reg_storage, op->ByteWidth());
  GRANARY_IF_DEBUG(auto replaced = ) op->Ref().ReplaceWith(repl_op);
  GRANARY_ASSERT(replaced);
}

// Schedule a fragment-local register that might be used in this operand. We
// choose different spill/fill strategies based on whether or not a virtual
// register is used as a memory operand or as a register operand.
static void ScheduleFragLocalReg(RegisterScheduler *sched,
                                 NativeInstruction * const instr,
                                 Operand *op) {
  if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
    auto reg = reg_op->Register();
    if (reg.IsVirtual()) {
      // Make sure we start looking for the definition in the right place.
      Instruction *def_search_instr = reg_op->IsRead() ? instr->Previous()
                                                       : instr;
      if (auto def = LocalDefinitionOf(sched->frag, def_search_instr, reg)) {
        ScheduleFragLocalRegOp(sched, instr, reg_op, def);
      }
    }
  } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
    VirtualRegister reg;
    if (mem_op->MatchRegister(reg) && reg.IsVirtual()) {
      if (auto def = LocalDefinitionOf(sched->frag, instr->Previous(), reg)) {
        ScheduleFragLocalMemOp(sched, instr, mem_op, def);
      }
    }
  }
}

// Try to eliminate a redundant copy instruction.
static bool TryRemoveCopyInstruction(RegisterScheduler *sched,
                                     NativeInstruction *instr) {
  RegisterOperand dest_reg_op;
  if (IsCopyInstruction(instr) &&
      instr->MatchOperands(WriteOnlyTo(dest_reg_op))) {
    auto reg = dest_reg_op.Register();
    auto var = GetMetaData<SSARegister *>(instr);
    auto def = DefinitionOf(var);
    if (reg.IsVirtual() && !DefinitionEscapesFragment(sched->frag, var, reg) &&
        def->reg == reg) {
      instr->UnsafeUnlink();
      return true;
    }
  }
  return false;
}

// Ensure that any native GPRs used by a particular operand are homed.
static void HomeNativeGPRs(RegisterScheduler *sched, NativeInstruction *instr,
                           Operand *op) {
  if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
    auto reg = reg_op->Register();
    if (op->IsWrite() &&
        !(reg_op->IsRead() || reg_op->IsConditionalWrite() ||
          reg.PreservesBytesOnWrite())) {
      FillGPRForDef(sched, instr, reg);
    } else {
      FillGPRForUse(sched, instr, reg);
    }
  } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
    VirtualRegister r1, r2, r3;
    if (mem_op->CountMatchedRegisters({&r1, &r2, &r3})) {
      FillGPRForUse(sched, instr, r1);
      FillGPRForUse(sched, instr, r2);
      FillGPRForUse(sched, instr, r3);
    }
  }
}

// Mark a register as being used.
static void MarkRegAsUsed(RegisterScheduler *sched, VirtualRegister reg) {
  if (reg.IsNative() && reg.IsGeneralPurpose()) {
    sched->gprs_used_in_instr.Set(reg.Number(), true);
  }
}

// Mark all GPRs used by a particular instruction as being used (regardless of
// if they are read or write operands).
static void FindUsedNativeGPRs(RegisterScheduler *sched, Operand *op) {
  if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
    MarkRegAsUsed(sched, reg_op->Register());
  } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
    VirtualRegister r1, r2, r3;
    if (mem_op->CountMatchedRegisters({&r1, &r2, &r3})) {
      MarkRegAsUsed(sched, r1);
      MarkRegAsUsed(sched, r2);
      MarkRegAsUsed(sched, r3);
    }
  }
}

// Schedule fragment-local virtual registers. A virtual register is fragment-
// local if it is defined with the fragment, and doesn't share a definition
static void ScheduleFragLocalRegs(RegisterScheduler *sched) {

  //for (auto instr : BackwardInstructionIterator(frag->last)) {
  for (auto instr = sched->frag->last; instr;) {
    const auto prev_instr = instr->Previous();
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      sched->gprs_used_in_instr.SetAll(false);

      // Note: Should visit defs before uses. Should have the effect of
      //       homing+killing defs, and homing+reviving uses.
      ninstr->ForEachOperand([=] (Operand *op) {
        HomeNativeGPRs(sched, ninstr, op);
        FindUsedNativeGPRs(sched, op);
      });

      if (!TryRemoveCopyInstruction(sched, ninstr)) {
        ninstr->ForEachOperand([=] (Operand *op) {
          ScheduleFragLocalReg(sched, ninstr, op);
        });

        // For each defined virtual instruction, free up the spill slot
        // associated with this virtual register if the virtual register is
        // fragment-local and if the virtual register is definitely defind by
        // this instruction.
        ForEachDefinition(ninstr, [=] (SSAVariable *def) {
          auto def_reg = RegisterOf(def);
          if (def_reg.IsVirtual() && IsA<SSARegister *>(def) &&
              !DefinitionEscapesFragment(sched->frag, def, def_reg)) {
            FreeVirtualSlot(sched, prev_instr, LocationOf(def));
          }
        });
      }
    }
    instr = prev_instr;
  }
}

// Fill any registers that remain stolen after scheduling fragment-local VRs.
static void FillRemainingStolenGPRs(RegisterScheduler *sched) {
  auto instr = sched->frag->first;
  for (auto i = 0; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
    auto &loc(sched->LocationOfGPR(i));
    if (loc.stolen_by) {
      GRANARY_ASSERT(loc.reg.IsVirtualSlot());
      sched->frag->InsertBefore(instr, SaveGPRToSlot(NthArchGPR(i), loc.reg));
    }
  }
}

// Schedule virtual registers that are only used an defined within their
// containing fragments.
static void ScheduleLocalRegs(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    frag->is_closed = false;
    frag->num_spill_slots = 0;
    frag->spill_slot_allocated_mask = 0;
    if (frag->ssa_vars) {
      RegisterScheduler sched(frag);
      ScheduleFragLocalRegs(&sched);
      FillRemainingStolenGPRs(&sched);
    }
  }
}

// Merge all frag-local spill info into each fragment's partition sentinel. This
// has the effect of summarizing the worst-case fragment-local spill info from
// the fragments within a partition.
//
// Also links together the fragments via the `prev` pointer, and returns the
// last fragment in the fragment list.
static Fragment *CombineLocalSpillInfo(RegisterScheduler * const sched,
                                       Fragment * const frags) {
  Fragment *last_frag(nullptr);

  // Propagate info from fragments to the sentinel.
  for (auto frag : FragmentIterator(frags)) {
    frag->prev = last_frag;
    last_frag = frag;
    if (frag->ssa_vars && frag->partition_sentinel) {
      auto part_frag = frag->partition_sentinel;
      auto num_spill_slots = frag->num_spill_slots;
      part_frag->num_spill_slots = std::max(part_frag->num_spill_slots,
                                            num_spill_slots);
      part_frag->spill_slot_allocated_mask |= ~((~0x0U) << num_spill_slots);
    } else {
      frag->reg_alloc_round = 0;
    }
  }

  // Propagate info from the sentinel back to the fragments.
  for (auto frag : FragmentIterator(frags)) {
    if (frag->ssa_vars && frag->partition_sentinel) {
      auto part_frag = frag->partition_sentinel;
      frag->num_spill_slots = part_frag->num_spill_slots;
      frag->spill_slot_allocated_mask = part_frag->spill_slot_allocated_mask;

      // Propagate the maximum number of local spill slots allocated anywhere.
      // We track this so that later, when allocating partition-global regs,
      // we can try to avoid stealing the same registers as the local
      // allocation strategy steals.
      sched->max_num_used_local_slots = std::max(
          sched->max_num_used_local_slots, part_frag->num_spill_slots);
    }
  }
  return last_frag;
}

// Applies a function to every entry and exit definition within a fragment.
template <typename FuncT>
static void ForEachLocation(Fragment * const frag, FuncT func) {
  for (auto entry_def : frag->ssa_vars->EntryDefs()) {
    if (entry_def.var) {
      func(LocationOf(DefinitionOf(entry_def.var)));
    }
  }
  for (auto exit_def : frag->ssa_vars->ExitDefs()) {
    if (exit_def) {
      func(LocationOf(DefinitionOf(exit_def)));
    }
  }
}

// Returns true if a fragment contains some unscheduled registers.
static bool FragmentHasUnscheduledRegs(Fragment * const frag) {
  auto has_unscheduled_regs = false;
  ForEachLocation(frag, [&] (RegisterLocation *loc) {
    has_unscheduled_regs = has_unscheduled_regs || !loc->reg.IsValid();
  });
  return has_unscheduled_regs;
}

// Allocates slots to all virtual registers within a given fragment.
static void AllocateUnscheduledRegs(RegisterScheduler * const sched,
                                    Fragment * const frag,
                                    Fragment * const part_frag) {
  ForEachLocation(frag, [=] (RegisterLocation *loc) {
    if (!loc->reg.IsValid()) {
      loc->reg = AllocateVirtualSlot(frag, loc);
      auto slot = static_cast<unsigned>(loc->spill_slot);

      part_frag->spill_slot_allocated_mask |= 1UL << slot;
      ++(part_frag->num_spill_slots);

      // Mark the slot as having been allocated *somewhere*. Our scheduling
      // algorithm is pretty simple: assign registers to only one global slot
      // at a time.
      sched->global_slots.Set(slot, true);
    }
  });
}

// Returns true if the location `loc` of the register `reg` represents a
// definition in `frag`, which is the fragment whose registers were most
// recently allocated within the partition associated with `part_frag`. This
// Is a basic way of asking: do there exist shared definitions between two
// fragments.
static bool RegDefInAllocFrag(RegisterLocation *loc, VirtualRegister reg,
                              Fragment *frag) {
  if (auto entry_var = frag->ssa_vars->EntryDefinitionOf(reg)) {
    if (LocationOf(DefinitionOf(entry_var)) == loc) {
      return true;
    }
  }
  if (auto exit_var = frag->ssa_vars->ExitDefinitionOf(reg)) {
    return LocationOf(DefinitionOf(exit_var)) == loc;
  }
  return false;
}

// Propagates a register allocation from a chosen fragment (the allocation info
// was cached in `alloc_frag`) to `frag`, but only if `frag` uses one of the
// registers being allocated. The idea here is that we want to cheat and be
// conservative and include the transitive closure of
static void PropagateAllocatedRegs(Fragment * const frag,
                                   Fragment * const part_frag) {
  auto alloc_frag = part_frag->partition_sentinel;
  ForEachLocation(frag, [=] (RegisterLocation *loc) {
    auto reg = loc->reg;
    if (reg.IsVirtualSlot()) {
      auto slot = static_cast<unsigned>(loc->spill_slot);
      auto mask = 1UL << slot;
      GRANARY_ASSERT(0 != (alloc_frag->spill_slot_allocated_mask & mask));
      GRANARY_ASSERT(0 != (part_frag->spill_slot_allocated_mask & mask));

      // Propagate the allocation.
      if (!(frag->spill_slot_allocated_mask & mask) ||
          RegDefInAllocFrag(loc, reg, alloc_frag)) {
        frag->spill_slot_allocated_mask |= part_frag->spill_slot_allocated_mask;
      }
    }
  });
}

// Allocate virtual registers that are used across several fragments within a
// partition. To simplify the problem, we consider any virtual register live on
// entry/exit from a fragment to interfere. This means that the granularity of
// live ranges is fragments and not individual instructions.
static void AllocateNonLocalRegs(RegisterScheduler *sched,
                                 Fragment * const last_frag) {
  auto reg_alloc_round = 1;

  // Visit the fragments in reverse order (this is basically a post-order
  // traversal) and schedule the registers.
  for (auto changed = true; changed; ++reg_alloc_round) {
    changed = false;
    for (auto frag : ReverseFragmentIterator(last_frag)) {
      if (frag->ssa_vars && frag->partition_sentinel && !frag->is_closed) {
        auto part_frag = frag->partition_sentinel;

        // We need to choose a fragment from this partition, and schedule all
        // registers from that fragment.
        if (part_frag->reg_alloc_round < reg_alloc_round) {
          if (FragmentHasUnscheduledRegs(frag)) {
            changed = true;

            // Within this partition, we've now "chosen" the set of registers
            // to allocate (by choosing a fragment). We will allocate all
            // registers from the partition spill slot mask.
            part_frag->partition_sentinel = frag;
            part_frag->reg_alloc_round = reg_alloc_round;
            part_frag->spill_slot_allocated_mask = 0;

            AllocateUnscheduledRegs(sched, frag, part_frag);
          }
          frag->is_closed = true;

        // We've now chosen a fragment, scheduled its registers, now we need
        // to mark those register slots scheduled everywhere else where *any*
        // of those scheduled registers appears.
        } else {
          PropagateAllocatedRegs(frag, part_frag);
        }
      }
    }
  }
}

// Returns the definition of the variable (on entry to a fragment) that will
// be spilled/filled from a particular spill slot.
static SSAVariable *FindEntryDefForSlot(Fragment * const frag, int slot) {
  for (auto entry_def : frag->ssa_vars->EntryDefs()) {
    if (entry_def.var) {
      auto loc = LocationOf(entry_def.var);
      if (loc->spill_slot == slot) {
        GRANARY_ASSERT(loc->reg.IsVirtualSlot());
        return entry_def.var;
      }
    }
  }
  return nullptr;
}

// Returns the definition of the variable (on exit from a fragment) that will
// be spilled/filled from a particular spill slot.
static SSAVariable *FindExitDefForSlot(Fragment * const frag, int slot) {
  for (auto exit_def : frag->ssa_vars->ExitDefs()) {
    if (exit_def) {
      auto loc = LocationOf(exit_def);
      if (loc->spill_slot == slot) {
        GRANARY_ASSERT(loc->reg.IsVirtualSlot());
        return exit_def;
      }
    }
  }
  return nullptr;
}

struct SlotScheduler {
  // The SSA variable associated with this location on entry to the fragment.
  SSAVariable *entry_def;

  // The SSA variable associated with this location on exit from this fragment.
  SSAVariable *exit_def;

  // The slot's current location. This is either a spill slot, or a general-
  // purpose register. In practice it should be stored in the
  // `preferred_steal_gpr`, but sometimes there are conflicts.
  VirtualRegister slot_loc;

  // The virtual register associated with this slot. This will either be
  // `RegisterOf(exit_def)` or `RegisterOf(entry_def)`.
  VirtualRegister slot_reg;

  // The GPR that we prefer to steal for use by this slot.
  VirtualRegister preferred_steal_gpr;

  // The specific slot being scheduled.
  int slot;
};

enum {
  NUM_GPRS = arch::NUM_GENERAL_PURPOSE_REGISTERS,
  MAX_LIVE_GPRS = MAX_NUM_LIVE_VIRTUAL_REGS
};

// Steal a register for a spill slot.
static VirtualRegister StealRegisterForSlot(const BitSet<NUM_GPRS> used_gprs) {
  for (int i = NUM_GPRS; i--;) {
    if (!used_gprs.Get(i)) {
      return NthArchGPR(i);
    }
  }
  GRANARY_ASSERT(false);
  return VirtualRegister();
}

// Schedule a slot from the bottom-up. This assumes that
static void BottomUpScheduleSlot(SlotScheduler * const sched,
                                 Fragment * const frag,
                                 Instruction *last) {
  const auto spill_slot = NthSpillSlot(sched->slot);
  const auto preferred_gpr_num = sched->preferred_steal_gpr.Number();

  for (auto instr : BackwardInstructionIterator(last)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {

      // Simplifying assumption enforced by prior steps:
      //  - No two distinct virtual registers within this block share the same
      //    slot, even if they don't truly interfere. This is because the
      //    granularity of interference is a single fragment, not a single
      //    instruction.

      // Look for conflicts with the current slot location.
      bool slot_reg_is_used = false;
      bool slot_loc_is_used = false;
      BitSet<NUM_GPRS> used_gprs;

      // Updates some register tracking state.
      auto track_use = [&] (VirtualRegister reg) {
        slot_reg_is_used = slot_reg_is_used || reg == sched->slot_reg;
        slot_loc_is_used = slot_loc_is_used || reg == sched->slot_loc;
        if (reg.IsNative() && reg.IsGeneralPurpose()) {
          used_gprs.Set(reg.Number(), true);
        }
      };

      // Visit all operands to figure out what regs are used and not used.
      ninstr->ForEachOperand([&] (Operand *op) {
        if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
          track_use(reg_op->Register());
        } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
          VirtualRegister r1, r2, r3;
          if (mem_op->CountMatchedRegisters({&r1, &r2, &r3})) {
            track_use(r1);
            track_use(r2);
            track_use(r3);
          }
        }
      });

      // If the slot location is used, then we need to restore the GPR that is
      // currently stored in the slot. What this really means is that we need
      // to save the GPR to the slot after the instruction that uses it, because
      // after that point we assume that the GPR will be used.
      //
      // TODO(pag): Could be smarter about only saving the GPR to the slot if
      //            the GPR is live after the current instruction.
      if (slot_loc_is_used) {
        GRANARY_ASSERT(sched->slot_loc.IsNative());
        frag->InsertAfter(instr, SaveGPRToSlot(sched->slot_loc, spill_slot));
        sched->slot_loc = spill_slot;
      }

      // The instruction uses the VR associated with the slot, and it's not
      // located in a GPR, so we need to steal a GPR. What this really means
      // is that after this instruction, we need to restore the GPR's value from
      // the slot, because we assume it is live after the instruction.
      if (slot_reg_is_used && !sched->slot_loc.IsNative()) {
        GRANARY_ASSERT(spill_slot == sched->slot_loc);
        if (!used_gprs.Get(preferred_gpr_num)) {
          sched->slot_loc = sched->preferred_steal_gpr;
        } else {
          sched->slot_loc = StealRegisterForSlot(used_gprs);
        }
        frag->InsertAfter(instr,
                          RestoreGPRFromSlot(sched->slot_loc, spill_slot));
      }
    }
  }
}

// Schedule all virtual registers associated with the same allocated spill slot.
static void ScheduleSlot(Fragment * const frag,
                         const VirtualRegister preferred_steal_gpr,
                         int slot) {
  const uint64_t gpr_busy_mask = (1UL << preferred_steal_gpr.Number());

  SlotScheduler sched;
  sched.entry_def = FindEntryDefForSlot(frag, slot);
  sched.exit_def = FindExitDefForSlot(frag, slot);
  sched.slot_loc = NthSpillSlot(slot);
  sched.slot_reg = sched.exit_def ? RegisterOf(sched.exit_def)
                                  : RegisterOf(sched.entry_def);
  if (sched.exit_def && !(frag->spill_slot_allocated_mask & gpr_busy_mask)) {
    sched.slot_loc = preferred_steal_gpr;
  }
  sched.preferred_steal_gpr = preferred_steal_gpr;
  sched.slot = slot;

  if (sched.exit_def) {
    BottomUpScheduleSlot(&sched, frag, frag->last);
  }

  // Mark the preferred GPR as busy in this block. This is so that the same
  // arch GPR can't simultaneously be "busy" (i.e. contain) the value of two
  // different virtual registers.
  frag->spill_slot_allocated_mask |= gpr_busy_mask;
}

// Schedule all virtual registers that are used in one or more fragments. By
// this point they should all be allocated. One challenge for scheduling is that
// a virtual register might be placed in two different physical registers
// across in two or more successors of a fragment, and needs to be in the same
// spot in the fragment itself.
static void ScheduleNonLocalRegs(RegisterScheduler * const sched,
                                 Fragment * const last_frag) {
  // Clear out all spill slot masks of non-partition fragments. We will use
  // these to determine which arch GPRs are busy (i.e. already holding a
  // virtual register) on *entry* to a block.
  for (auto frag : ReverseFragmentIterator(last_frag)) {
    if (frag->ssa_vars) {
      frag->spill_slot_allocated_mask = 0;
    }
  }

  int gpr_start = sched->max_num_used_local_slots;
  for (auto slot = 0; slot < MAX_LIVE_GPRS; ++slot) {
    if (!sched->global_slots.Get(slot)) {
      continue;
    }

    // The preferred arch GPR that we want to steal.
    const auto preferred_gpr = NthArchGPR(
        NUM_GPRS - ((NUM_GPRS * MAX_LIVE_GPRS + gpr_start) % NUM_GPRS) - 1);

    // Go through every fragment and try to schedule the virtual register whose
    // `RegisterLocation::reg.Number()` is `slot`.
    for (auto frag : ReverseFragmentIterator(last_frag)) {
      if (!frag->ssa_vars) {
        continue;  // No useful instructions.
      }

      sched->frag = frag;
      auto frag_uses_slot = false;
      ForEachLocation(frag, [&] (RegisterLocation *loc) {
        frag_uses_slot = frag_uses_slot || loc->spill_slot == slot;
      });
      if (frag_uses_slot) {
        ScheduleSlot(frag, preferred_gpr, slot);
      }
    }
    ++gpr_start;
  }
}

// Clean up all allocated storage locations.
static void CleanUpRegisterLocations(RegisterLocation *loc) {
  for (RegisterLocation *next_loc(nullptr); loc; loc = next_loc) {
    next_loc = loc->next;
    delete loc;
  }
}

// Remove SSA definitions from an instruction.
static void CleanUpSSADefs(NativeInstruction *instr) {
  if (auto var = GetMetaData<SSAVariable *>(instr)) {
    while (auto forward_def = DynamicCast<SSAForward *>(var)) {
      var = forward_def->next_instr_def;
      DestroySSAObj(forward_def);
    }
    if (var) {
      DestroySSAObj(var);
    }
    ClearMetaData(instr);
  }
}

// Clean up all SSA variable definitions.
static void CleanUpSSAVars(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (frag->ssa_vars) {
      for (auto instr : ForwardInstructionIterator(frag->first)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          CleanUpSSADefs(ninstr);
        }
      }
      delete frag->ssa_vars;
      frag->ssa_vars = nullptr;
    }
  }
}

}  // namespace

// Schedule virtual registers.
void ScheduleRegisters(Fragment * const frags) {
  auto loc = AssignRegisterLocations(frags);
  ScheduleLocalRegs(frags);
  RegisterScheduler sched(nullptr);
  auto last_frag = CombineLocalSpillInfo(&sched, frags);
  AllocateNonLocalRegs(&sched, last_frag);
  ScheduleNonLocalRegs(&sched, last_frag);
  CleanUpRegisterLocations(loc);
  CleanUpSSAVars(frags);
}

}  // namespace granary
#endif
