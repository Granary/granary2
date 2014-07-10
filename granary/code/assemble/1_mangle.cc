/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/base.h"
#include "granary/arch/driver.h"

#include "granary/base/base.h"
#include "granary/base/list.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/operand.h"

#include "granary/code/assemble/1_mangle.h"

#include "granary/cache.h"  // For `CacheMetaData`.
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {
namespace arch {

// Relativize a direct control-flow instruction.
//
// Note: This has an architecture-specific implementation.
extern void RelativizeDirectCFI(CacheMetaData *meta,
                                ControlFlowInstruction *cfi,
                                arch::Instruction *instr, PC target_pc,
                                bool target_is_far_away);

// Performs mangling of an indirect CFI instruction.
//
// Note: This has an architecture-specific implementation.
extern void MangleIndirectCFI(DecodedBasicBlock *block,
                              ControlFlowInstruction *cfi);

// Performs mangling of an direct CFI instruction.
//
// Note: This has an architecture-specific implementation.
extern void MangleDirectCFI(DecodedBasicBlock *block,
                            ControlFlowInstruction *cfi, AppPC target_pc);

// Relativize a instruction with a memory operand, where the operand loads some
// value from `mem_addr`
//
// Note: This has an architecture-specific implementation.
extern void RelativizeMemOp(DecodedBasicBlock *block, NativeInstruction *ninstr,
                            const MemoryOperand &op, const void *mem_addr);

}  // namespace arch
namespace {

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
    auto signed_diff = relative_pc - cache_pc;
    auto diff = 0 > signed_diff ? -signed_diff : signed_diff;
    return MAX_BRANCH_OFFSET < diff;
  }

  // Relativize a particular memory operation within a memory instruction.
  void RelativizeMemOp(NativeInstruction *instr, const MemoryOperand &mloc) {
    const void *mptr(nullptr);
    if (mloc.MatchPointer(mptr) && AddressNeedsRelativizing(mptr)) {
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

  // Mangle a return, where the target of the return is specialized.
  void MangleSpecializedReturn(ControlFlowInstruction *cfi) {
    GRANARY_UNUSED(cfi);
  }

  // Relativize a control-flow instruction.
  void MangleCFI(ControlFlowInstruction *cfi) {
    auto target_block = cfi->TargetBlock();
    if (IsA<NativeBasicBlock *>(target_block)) {
      // We always defer to arch-specific relativization because some
      // instructions need to be relativized regardless of whether or not the
      // target PC is far away. For example, on x86, the `LOOP rel8`
      // instructions must always be relativized.
      if (!cfi->HasIndirectTarget()) {
        auto target_pc = target_block->StartAppPC();
        RelativizeDirectCFI(GetMetaData<CacheMetaData>(block), cfi,
                            &(cfi->instruction), target_pc,
                            AddressNeedsRelativizing(target_pc));
      } else {
        arch::MangleIndirectCFI(block, cfi);
      }
    // Indirect CFIs might read their target from a PC-relative address.
    } else if (IsA<IndirectBasicBlock *>(target_block)) {
      MemoryOperand mloc;
      if (cfi->MatchOperands(ReadFrom(mloc))) {
        RelativizeMemOp(cfi, mloc);
      }
      arch::MangleIndirectCFI(block, cfi);

    // Need to mangle the indirect direct (with meta-data) into a return to
    // a different program counter.
    } else if (auto return_bb = DynamicCast<ReturnBasicBlock *>(target_block)) {
      if (return_bb->UnsafeMetaData()) {
        MangleSpecializedReturn(cfi);
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

  enum : ptrdiff_t {
    MAX_BRANCH_OFFSET = RelAddress<arch::REL_BRANCH_WIDTH_BITS>::MAX_OFFSET
  };

  DecodedBasicBlock *block;
  PC cache_pc;
};

}  // namespace

// Relativize the native instructions within a LCFG.
void MangleInstructions(CodeCache *code_cache,
                        LocalControlFlowGraph* cfg) {
  auto estimated_encode_loc = code_cache->AllocateBlock(0);

  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      BlockMangler mangler(decoded_block, estimated_encode_loc);
      mangler.Mangle();
    }
  }
}

}  // namespace granary
