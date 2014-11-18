/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/base.h"
#include "arch/driver.h"
#include "arch/util.h"

#include "granary/base/base.h"
#include "granary/base/list.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/operand.h"

#include "granary/code/assemble/1_mangle.h"
#include "granary/code/metadata.h"

#include "granary/cache.h"  // For `CacheMetaData`.
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {
namespace arch {

// Returns true if an address needs to be relativized.
//
// Note: This has an architecture-specific implementation.
extern bool AddressNeedsRelativizing(const void *ptr);

// Relativize a direct control-flow instruction.
//
// Note: This has an architecture-specific implementation.
extern void RelativizeDirectCFI(CacheMetaData *meta,
                                NativeInstruction *cfi,
                                arch::Instruction *instr, PC target_pc,
                                bool target_is_far_away);

// Performs mangling of an indirect CFI instruction.
//
// Note: This has an architecture-specific implementation.
extern void MangleIndirectCFI(DecodedBasicBlock *block,
                              ControlFlowInstruction *cfi,
                              AnnotationInstruction *ret_address);

// Performs mangling of an direct CFI instruction.
//
// Note: This has an architecture-specific implementation.
extern void MangleDirectCFI(DecodedBasicBlock *block,
                            ControlFlowInstruction *cfi, AppPC target_pc);

// Relativize a instruction with a memory operand, where the operand loads some
// value from `mem_addr`.
//
// Note: This has an architecture-specific implementation.
extern void RelativizeMemOp(DecodedBasicBlock *block, NativeInstruction *ninstr,
                            const MemoryOperand &op, const void *mem_addr);

// Mangle a "specialized" return so that is converted into an indirect jump.
//
// Note: This has an architecture-specific implementation.
extern void MangleIndirectReturn(DecodedBasicBlock *block,
                                 ControlFlowInstruction *cfi);

}  // namespace arch
namespace {

// Manages simple relativization checks / tasks.
class BlockMangler {
 public:
  BlockMangler(DecodedBasicBlock *block_, PC estimated_encode_loc)
      : block(block_),
        cache_pc(estimated_encode_loc) {}

  inline bool AddressNeedsRelativizing(const void *ptr) const {
    return AddressNeedsRelativizing(reinterpret_cast<PC>(ptr));
  }

  // Returns true if an address needs relativizing.
  bool AddressNeedsRelativizing(PC relative_pc) const {
    return !arch::AddrIsOffsetReachable(cache_pc, relative_pc);
  }

  // Relativize a particular memory operation within a memory instruction.
  void RelativizeMemOp(NativeInstruction *instr, const MemoryOperand &mloc) {
    const void *mptr(nullptr);
    if (mloc.IsExplicit() && mloc.MatchPointer(mptr)) {
      // Can be accessed using a PC-relative memory operand.
      if (!AddressNeedsRelativizing(mptr)) return;

      // Can be accessed using an absolute address.
      if (!arch::AddressNeedsRelativizing(mptr)) return;

      // Too far to be relative, and too big to be absolute.
      arch::RelativizeMemOp(block, instr, mloc, mptr);
    }
  }

  // Relativize a memory instruction.
  void RelativizeMemOp(NativeInstruction *instr) {
    MemoryOperand mloc1;
    MemoryOperand mloc2;
    auto count = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                             ReadOrWriteTo(mloc2));
    if (2 == count) {
      RelativizeMemOp(instr, mloc1);
      RelativizeMemOp(instr, mloc2);
    } else if (1 == count) {
      RelativizeMemOp(instr, mloc1);
    }
  }

  void MangleFunctionCall(ControlFlowInstruction *cfi) {
    // Always add in an `kAnnotReturnAddressLabel` annotation. In a later stage,
    // we ensure that this annotation is placed in the correct location, even
    // if instructions are inserted between it and the function.
    auto ret_address = new AnnotationInstruction(
        kAnnotReturnAddressLabel, cfi->DecodedPC() + cfi->DecodedLength());
    cfi->InsertAfter(ret_address);
    cfi->return_address = ret_address;

    // Ensure that targets of function calls have valid stack meta-data.
    auto target_generic = cfi->TargetBlock();
    if (auto target = DynamicCast<InstrumentedBasicBlock *>(target_generic)) {
      if (auto target_meta = target->UnsafeMetaData()) {
        auto stack_meta = MetaDataCast<StackMetaData *>(target_meta);
        stack_meta->MarkStackAsValid();
      }
    }
  }

  // Relativize a control-flow instruction.
  void MangleCFI(ControlFlowInstruction *cfi) {
    if (cfi->IsFunctionCall()) {
      MangleFunctionCall(cfi);
    }

    auto target_block = cfi->TargetBlock();
    if (IsA<NativeBasicBlock *>(target_block)) {
      // We always defer to arch-specific relativization because some
      // instructions need to be relativized regardless of whether or not the
      // target PC is far away. For example, on x86, the `LOOP rel8`
      // instructions must always be relativized.
      if (!cfi->HasIndirectTarget()) {
        auto target_pc = target_block->StartAppPC();
        arch::RelativizeDirectCFI(GetMetaData<CacheMetaData>(block), cfi,
                                  &(cfi->instruction), target_pc,
                                  AddressNeedsRelativizing(target_pc));

      // E.g. system calls, interrupt calls.
      } else {
        //arch::MangleIndirectCFI(block, cfi, cfi->return_address);
      }
    // Indirect CFIs might read their target from a PC-relative address.
    } else if (IsA<IndirectBasicBlock *>(target_block)) {
      MemoryOperand mloc;
      if (cfi->MatchOperands(ReadFrom(mloc))) {
        RelativizeMemOp(cfi, mloc);
      }
      arch::MangleIndirectCFI(block, cfi, cfi->return_address);

    // Need to mangle the indirect direct (with meta-data) into a return to
    // a different program counter.
    } else if (auto return_bb = DynamicCast<ReturnBasicBlock *>(target_block)) {
      if (return_bb->UnsafeMetaData()) {
        arch::MangleIndirectReturn(block, cfi);
      }

    // Some CFIs (e.g. very short conditional jumps) might need to be mangled
    // into a form that uses branches.
    } else {
      if (IsA<CachedBasicBlock *>(target_block)) {
        arch::MangleDirectCFI(block, cfi, target_block->StartCachePC());
      } else {
        arch::MangleDirectCFI(block, cfi, target_block->StartAppPC());
      }
    }
  }

  // Relativizes an individual instruction by replacing addresses that are too
  // far away with ones that use virtual registers or other mechanisms. This is
  // the "easy" side of things, where the virtual register system needs to do
  // the "hard" part of actually making register usage reasonable.
  void RelativizeInstruction(NativeInstruction *instr) {
    if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
      MangleCFI(cfi);
    } else {
      if ((instr->IsFunctionCall() || instr->IsJump()) &&
          !instr->HasIndirectTarget()) {
        auto target_pc = instr->instruction.BranchTargetPC();
        arch::RelativizeDirectCFI(GetMetaData<CacheMetaData>(block), instr,
                                  &(instr->instruction), target_pc,
                                  AddressNeedsRelativizing(target_pc));
      }
      RelativizeMemOp(instr);
    }
  }

  // Relativizes instructions that use PC-relative operands that are too far
  // away from our estimate of where this block will be encoded.
  void Mangle(void) {
    Instruction *next_instr(nullptr);
    for (auto instr = block->FirstInstruction(); instr; instr = next_instr) {
      next_instr = instr->Next();
      if (auto native_instr = DynamicCast<NativeInstruction *>(instr)) {
        RelativizeInstruction(native_instr);
      }
    }
  }

 private:
  BlockMangler(void) = delete;

  DecodedBasicBlock *block;
  PC cache_pc;
};

}  // namespace

// Relativize the native instructions within a LCFG.
void MangleInstructions(LocalControlFlowGraph* cfg) {
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      BlockMangler mangler(decoded_block, EstimatedCachePC());
      mangler.Mangle();
    }
  }
}

}  // namespace granary
