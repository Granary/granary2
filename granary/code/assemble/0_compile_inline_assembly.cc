/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/inline_assembly.h"

namespace granary {

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
                annot->GetData<void *>());
            asm_block->Compile(cfg, dblock, instr);
            delete asm_block;
            instr->UnsafeUnlink();
          }
        }
      }
    }
  }
}

}  // namespace granary
