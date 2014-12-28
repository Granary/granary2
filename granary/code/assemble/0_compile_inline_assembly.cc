/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/trace.h"
#include "granary/cfg/block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/inline_assembly.h"

#include "granary/code/assemble/0_compile_inline_assembly.h"

namespace granary {
namespace arch {

// Compile this inline assembly into some instructions within the block
// `block`. This places the inlined instructions before `instr`, which is
// assumed to be the `AnnotationInstruction` containing the inline assembly
// instructions.
//
// Note: This has an architecture-specific implementation.
extern void CompileInlineAssemblyBlock(Trace *cfg,
                                       DecodedBlock *block,
                                       granary::Instruction *instr,
                                       InlineAssemblyBlock *asm_block);
}  // namespace arch
namespace {

static void CompileInlineAssembly(Trace *cfg,
                                  DecodedBlock *block,
                                  AnnotationInstruction *instr) {
  auto asm_block = instr->Data<InlineAssemblyBlock *>();
  instr->SetData(0UL);
  arch::CompileInlineAssemblyBlock(cfg, block, instr, asm_block);
  delete asm_block;
  Instruction::Unlink(instr);
}

static void CompileInlineAssembly(Trace *cfg,
                                  DecodedBlock *block) {
  auto instr = block->FirstInstruction();
  for (Instruction *next_instr(nullptr); instr; instr = next_instr) {
    next_instr = instr->Next();
    if (auto annot_instr = DynamicCast<AnnotationInstruction *>(instr)) {
      if (kAnnotInlineAssembly == annot_instr->annotation) {
        CompileInlineAssembly(cfg, block, annot_instr);
      }
    }
  }
}

}  // namespace

// Compile all inline assembly instructions by parsing the inline assembly
// instructions and doing code generation for them.
void CompileInlineAssembly(Trace *cfg) {
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBlock *>(block)) {
      CompileInlineAssembly(cfg, decoded_block);
    }
  }
}

}  // namespace granary
