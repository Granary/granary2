/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/inline_assembly.h"

namespace granary {

// Compile this inline assembly into some instructions within the block
// `block`. This places the inlined instructions before `instr`, which is
// assumed to be the `AnnotationInstruction` containing the inline assembly
// instructions.
//
// Note: This has an architecture-specific implementation.
extern void CompileInlineAssemblyBlock(LocalControlFlowGraph *cfg,
                                       DecodedBasicBlock *block,
                                       Instruction *instr,
                                       InlineAssemblyBlock *asm_block);

// Compile all inline assembly instructions by parsing the inline assembly
// instructions and doing code generation for them.
void CompileInlineAssembly(LocalControlFlowGraph *cfg) {
  for (auto block : cfg->Blocks()) {
    if (auto dblock = DynamicCast<DecodedBasicBlock *>(block)) {
      auto instr = dblock->FirstInstruction();
      for (Instruction *next_instr(nullptr); instr; instr = next_instr) {
        next_instr = instr->Next();
        if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
          if (IA_INLINE_ASSEMBLY == annot->annotation) {
            auto asm_block = reinterpret_cast<InlineAssemblyBlock *>(
                annot->Data<void *>());
            CompileInlineAssemblyBlock(cfg, dblock, instr, asm_block);
            delete asm_block;
            instr->UnsafeUnlink();
          }
        }
      }
    }
  }
}

}  // namespace granary
