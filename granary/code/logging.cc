/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/fragment.h"
#include "granary/code/operand.h"

#include "granary/logging.h"
#include "granary/module.h"

namespace granary {

// Log an individual edge between two fragments.
static void LogFragmentEdge(LogLevel level, Fragment *pred, Fragment *frag) {
  Log(level, "f%p -> f%p;\n", reinterpret_cast<void *>(pred),
                              reinterpret_cast<void *>(frag));
}

// Log the outgoing edges of a fragment.
static void LogFragmentEdges(LogLevel level, Fragment *frag) {
  if (frag->fall_through_target) {
    LogFragmentEdge(level, frag, frag->fall_through_target);
  }
  if (frag->branch_target) {
    LogFragmentEdge(level, frag, frag->branch_target);
  }
}

// Log the instructions of a fragment.
static void LogFragmentInstructions(LogLevel level, Fragment *frag) {
  Log(level, "f%p [label=<%d|",
      reinterpret_cast<void *>(frag),
      frag->id);

  auto print_as_sub_record = false;
  if (frag->block_meta && (frag->is_block_head || frag->is_future_block_head)) {
    auto meta = MetaDataCast<ModuleMetaData *>(frag->block_meta);
    Log(level, "{%p|{", meta->start_pc);
    print_as_sub_record = true;
  }

  for (auto instr : ForwardInstructionIterator(frag->first)) {
    auto ninstr = DynamicCast<NativeInstruction *>(instr);
    if (!ninstr) {
      continue;
    }

    Log(level, "%s", ninstr->OpCodeName());

    // Log the input operands.
    const char *sep = " ";
    ninstr->ForEachOperand([&] (Operand *op) {
      if (!op->IsWrite()) {
        OperandString op_str;
        op->EncodeToString(&op_str);
        Log(level, "%s%s", sep, static_cast<const char *>(op_str));
        sep = ", ";
      }
    });

    // Log the output operands.
    sep = " -&gt; ";
    ninstr->ForEachOperand([&] (Operand *op) {
      if (op->IsWrite()) {
        auto prefix = op->IsRead() ? "r/w " : "";
        OperandString op_str;
        op->EncodeToString(&op_str);
        Log(level, "%s%s%s", sep, prefix, static_cast<const char *>(op_str));
        sep = ", ";
      }
    });
    Log(level, "<BR ALIGN=\"LEFT\"/>");  // Keep instructions left-aligned.
  }

  if (print_as_sub_record) {
    Log(level, "}}");
  }

  Log(level, ">];\n");
}

// Log a list of fragments as a DOT digraph.
void Log(LogLevel level, Fragment *frags) {
  Log(level, "digraph {\n"
             "node [fontname=Courier shape=record"
             " nojustify=false labeljust=l];\n"
             "f0 [color=white fontcolor=white];\n");
  LogFragmentEdge(level, nullptr, frags);
  for (auto frag : FragmentIterator(frags)) {
    LogFragmentEdges(level, frag);
    LogFragmentInstructions(level, frag);
  }
  Log(level, "}\n");
}

}  // namespace granary
