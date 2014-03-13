/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/list.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/assemble.h"
#include "granary/code/fragment.h"
#include "granary/code/logging.h"

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

  // Returns true if an address needs relativizing.
  bool AddressNeedsRelativizing(PC relative_pc) const {
    auto signed_diff = relative_pc - cache_pc;
    auto diff = 0 > signed_diff ? -signed_diff : signed_diff;
    return MAX_BRANCH_OFFSET < diff;
  }

  void RelativizeCFI(ControlFlowInstruction *cfi) {
    auto target_block = cfi->TargetBlock();
    if (IsA<NativeBasicBlock *>(target_block)) {
      auto target_pc = target_block->StartAppPC();

      // We always defer to arch-specific relativization because some
      // instructions need to be relativized regardless of whether or not the
      // target PC is far away. For example, on x86, the `LOOP rel8`
      // instructions must always be relativized.
      driver::RelativizeCFI(cfi, &(cfi->instruction), target_pc,
                            AddressNeedsRelativizing(target_pc));
    }
  }

  // Relativizes an individual instruction by replacing addresses that are too
  // far away with ones that use virtual registers or other mechanisms. This is
  // the "easy" side of things, where the virtual register system needs to do
  // the "hard" part of actually making register usage reasonable.
  void RelativizeInstruction(DecodedBasicBlock *block,
                             NativeInstruction *instr) {
    if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
      RelativizeCFI(cfi);
    }

    GRANARY_UNUSED(block);
  }

  // Relativizes instructions that use PC-relative operands that are too far
  // away from our estimate of where this block will be encoded.
  void RelativizeBlock(DecodedBasicBlock *block) {
    for (auto instr : block->Instructions()) {
      auto native_instr = DynamicCast<NativeInstruction *>(instr);
      if (native_instr) {
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
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    if (decoded_block) {
      rel.RelativizeBlock(decoded_block);
    }
  }
}

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
  Log(LogWarning, frags);

  GRANARY_UNUSED(env);
}

}  // namespace granary
