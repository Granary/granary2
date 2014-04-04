/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/instruction.h"

#include "granary/base/big_vector.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/8_schedule_registers.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/register.h"

#include "granary/util.h"

namespace granary {

// Info tracker about an individual virtual register.
class VirtualRegisterInfo {
 public:
  uint16_t num_defs;
  uint16_t num_uses;

  bool has_value:1;
  bool value_reads_from_memory:1;

  arch::Operand value;

  // This is fairly rough constraint. This only really meaningful for introduced
  // `LEA` instructions that defined virtual registers as a combination of
  // several other non-virtual registers.
  LiveRegisterTracker depends_on;

} __attribute__((packed));

// Table that records all info about virtual register usage.
class VirtualRegisterTracker {
 public:
  explicit VirtualRegisterTracker(Fragment * const frags_)
      : frags(frags_) {}

  // Find all defined virtual registers.
  void FindDefinitions(void) {
    for (auto frag : FragmentIterator(frags)) {
      DeadRegisterTracker dead_regs(frag->exit_regs_dead);
      for (auto instr : BackwardInstructionIterator(frag->last)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          VisitDef(ninstr, &dead_regs);
        }
      }
    }
  }

  // Try to elide simple uses of virtual registers.
  void TryReplaceSimpleUses(void) {
    for (auto frag : FragmentIterator(frags)) {
      for (auto instr : BackwardInstructionIterator(frag->last)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          TryReplaceSimpleUse(ninstr);
        }
      }
    }
  }

  // Remove instructions that define unused virtual registers.
  void RemoveUnusedDefs(void) {
    // TODO(pag): What if the instruction modifies the flags??
    for (auto frag : FragmentIterator(frags)) {
      for (Instruction *prev(nullptr), *instr(frag->last);
           instr; instr = prev) {
        prev = instr->Previous();
        if (IsA<NativeInstruction *>(instr)) {
          if (auto def = GetMetaData<VirtualRegisterInfo *>(instr)) {
            if (!def->num_uses) {
              def->num_defs -= 1;
              frag->RemoveInstruction(instr);
            }
          }
        }
      }
    }
  }

 private:
  // Visit all instructions that define a virtual register.
  void VisitDef(NativeInstruction *ninstr, DeadRegisterTracker *dead_regs) {
    auto &instr(ninstr->instruction);
    auto &def(instr.ops[0]);
    auto &source(instr.ops[1]);

    // By default, mark native instructions as not defining any virtual
    // registers.
    ClearMetaData(ninstr);

    // TODO(pag): What if the instruction modifies the flags??

    // Defines a virtual register.
    if (instr.num_explicit_ops && def.IsWrite() && def.IsRegister() &&
        def.reg.IsVirtual()) {
      auto reg = def.reg;
      auto &info(regs[reg.Number()]);

      SetMetaData(ninstr, &info);

      // Say that this virtual register has a value if and only if it is
      // defined once, and only by an instruction that reads a value from a
      // specific location.
      info.num_defs += 1;
      info.has_value = 1 == info.num_defs && 2 == instr.num_explicit_ops &&
                       source.IsRead();

      // If we think we have a value (this might change if we see a later def)
      // then make sure that any architectural registers that our virtual
      // register depends on are not killed between its def and its use(s).
      if (info.has_value) {
        LiveRegisterTracker used_regs;
        RegisterTracker dead_regs_shadow(*dead_regs);
        used_regs.Visit(ninstr);
        if (dead_regs_shadow.Union(used_regs)) {
          // Annoying case:
          //    LEA [RAX] -> %0
          //    mov [%0] -> RAX
          //
          // TODO(pag): Might be better to count uses and defs.
          info.has_value = false;
        } else {
          info.value = source;  // TODO(pag): What about immediates?

          // Used to distinguish memory ops that can be treated as-if they are
          // registers from actual memory loads.
          info.value_reads_from_memory = XED_ICLASS_LEA != instr.iclass &&
                                         source.IsMemory();
        }
      }
    }
    dead_regs->Visit(ninstr);
  }

  // Try to elide a use of a virtual register.
  void TryReplaceSimpleUse(NativeInstruction *ninstr) {
    for (auto &op : ninstr->instruction.ops) {
      if (op.IsRegister()) {
        if (op.reg.IsVirtual() && !op.IsWrite()) {
          TryRemoveVirtRegUseInRegOp(&op);
        }
      } else if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
        if (!op.is_compound && op.reg.IsVirtual()) {
          TryRemoveVirtRegUseInMemOp(&op);
        }
      } else if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
        break;
      }
    }
  }

  // Returns true if `dest` is replaced with `source`. Returns `false` if
  // `source.Number() == dest->Number()`. This will update the source virtual
  // register's usage count in the tracker.
  bool CopyPropagate(VirtualRegister *dest, VirtualRegister source) {
    const auto source_num = source.Number();
    if (dest->Number() != source_num) {
      source.Widen(dest->ByteWidth());
      if (source.IsVirtual()) {
        ++(regs[source_num].num_uses);
      }
      *dest = source;
      return true;
    } else {
      return false;
    }
  }

  // Try to replace one memory operand with another. This is meant for the
  // (presumably) common case where instrumentation is potentially reading
  // memory locations / values but never actually changing memory addresses.
  bool TryReplaceRegUseInMemOp(arch::Operand *use, const arch::Operand &repl) {
    if (XED_ENCODER_OPERAND_TYPE_MEM == repl.type) {
      if (repl.is_compound) {
        use->mem = repl.mem;
        use->is_compound = true;
        return true;
      } else {
        return CopyPropagate(&(use->reg), repl.reg);
      }
    } else if (XED_ENCODER_OPERAND_TYPE_REG == repl.type) {
      if (repl.reg.IsNative()) {
        use->mem.disp = 0;
        use->mem.reg_seg = XED_REG_INVALID;
        use->mem.reg_base = static_cast<xed_reg_enum_t>(
            repl.reg.EncodeToNative());
        use->mem.reg_index = XED_REG_INVALID;
        use->mem.scale = 0;
        use->is_compound = true;
        return true;
      } else {
        return CopyPropagate(&(use->reg), repl.reg);
      }
    } else {  // E.g. immediate.
      return false;
    }
  }

  // Try to elide a virtual register use inside of a memory operand. We can only
  // do this if the virtual register being used has a value and doesn't need to
  // be allocated.
  //
  // We can overwrite a virtual register in a memory operation with one of
  // four things:
  //      1) [%A], value = addr %B  -->   [%B]
  //      2) [%A], value = lea [%B] -->   [%B]
  //      3) [%A], value = reg      -->   [reg]
  //      4) [%A], value = lea [reg]-->   [reg]
  void TryRemoveVirtRegUseInMemOp(arch::Operand *use) {
    auto &repl_info(regs[use->reg.Number()]);
    if (repl_info.has_value && !repl_info.value_reads_from_memory) {
      if (TryReplaceRegUseInMemOp(use, repl_info.value)) {
        return;
      }
    } else {
      GRANARY_ASSERT(!use->is_sticky);
    }
    ++(repl_info.num_uses);
  }

  // Try to replace one virtual register operand with another, or with a
  // physical register.
  bool TryReplaceRegUseInRegOp(arch::Operand *use, const arch::Operand &repl) {
    GRANARY_UNUSED(use);
    GRANARY_UNUSED(repl);
    return false;
  }

  // Try to elide a virtual register use inside of a register operand.
  void TryRemoveVirtRegUseInRegOp(arch::Operand *use) {
    auto &repl_info(regs[use->reg.Number()]);
    if (repl_info.has_value && TryReplaceRegUseInRegOp(use, *use)) {
      return;
    }
    ++(repl_info.num_uses);
  }

  Fragment * const frags;
  BigVector<VirtualRegisterInfo> regs;
};

// Schedule virtual registers to either physical registers or to stack/TLS
// slots.
void ScheduleVirtualRegisters(Fragment * const frags) {
  VirtualRegisterTracker tracker(frags);
  tracker.FindDefinitions();
  tracker.TryReplaceSimpleUses();
  tracker.RemoveUnusedDefs();
}

}  // namespace granary
