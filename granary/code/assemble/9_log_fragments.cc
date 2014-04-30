/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"
#include "granary/cfg/operand.h"

#include "granary/code/assemble/fragment.h"

#include "granary/logging.h"
#include "granary/module.h"

namespace granary {
namespace {
// Log an individual edge between two fragments.
static void LogFragmentEdge(LogLevel level, const Fragment *pred,
                            const Fragment *frag) {
  Log(level, "f%p -> f%p;\n", reinterpret_cast<const void *>(pred),
                              reinterpret_cast<const void *>(frag));
}

// Log the outgoing edges of a fragment.
static void LogFragmentEdges(LogLevel level, Fragment *frag) {
  for (auto succ : frag->successors) {
    if (succ) {
      LogFragmentEdge(level, frag, succ);
    }
  }
}

static const char *partition_color[] = {
  "aliceblue",
  "aquamarine",
  "aquamarine3",
  "bisque2",
  "brown1",
  "burlywood1",
  "cadetblue1",
  "chartreuse1",
  "chocolate1",
  "darkolivegreen3",
  "darkorchid2"
};

enum {
  NUM_COLORS = sizeof partition_color / sizeof partition_color[0]
};

static const char *FragmentBorder(const Fragment *frag) {
  if (auto code = DynamicCast<CodeFragment *>(frag)) {
    if (!code->stack.is_checked) {
      return "red";
    } else if (!code->stack.is_valid) {
      return "white";
    }
  }
  return "black";
}

// Color the fragment according to the partition to which it belongs. This is
// meant to be a visual cue, not a perfect association with the fragment's
// partition id.
static const char *FragmentBackground(const Fragment *frag) {
  if (IsA<ExitFragment *>(frag)) {
    return "white";
  }
  auto stack_id = static_cast<size_t>(frag->partition->id);
  return stack_id ? partition_color[stack_id % NUM_COLORS] : "white";
}

// Log the input-only operands.
static void LogInputOperands(LogLevel level, NativeInstruction *instr) {
  auto sep = " ";
  instr->ForEachOperand([&] (Operand *op) {
    if (!op->IsWrite()) {
      OperandString op_str;
      op->EncodeToString(&op_str);
      auto prefix = op->IsConditionalRead() ? "cr " : "";
      Log(level, "%s%s%s", sep, prefix, static_cast<const char *>(op_str));
      sep = ", ";
    }
  });
}

// Log the output operands. Some of these operands might also be inputs.
static void LogOutputOperands(LogLevel level, NativeInstruction *instr) {
  auto sep = " -&gt; ";
  instr->ForEachOperand([&] (Operand *op) {
    if (op->IsWrite()) {
      auto prefix = op->IsRead() ?
                    (op->IsConditionalWrite() ? "r/cw " : "r/w ") :
                    (op->IsConditionalWrite() ? "cw " : "");
      OperandString op_str;
      op->EncodeToString(&op_str);
      Log(level, "%s%s%s", sep, prefix, static_cast<const char *>(op_str));
      sep = ", ";
    }
  });
}

// Log the instructions of a fragment.
static void LogInstructions(LogLevel level, const Fragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (!ninstr->IsAppInstruction()) {
        Log(level, "&nbsp;  ");
      }
      Log(level, "%s", ninstr->OpCodeName());
      LogInputOperands(level, ninstr);
      LogOutputOperands(level, ninstr);
      Log(level, "<BR ALIGN=\"LEFT\"/>");  // Keep instructions left-aligned.
    }
  }
}

// If this fragment is the head of a basic block then log the basic block's
// entry address.
static void LogBlockHeader(LogLevel level, const Fragment *frag) {
  if (IsA<PartitionEntryFragment *>(frag)) {
    Log(level, "partition entry|");
  } else if (IsA<PartitionExitFragment *>(frag)) {
    Log(level, "partition exit|");
  } else if (IsA<FlagEntryFragment *>(frag)) {
    Log(level, "flag entry|");
  } else if (IsA<FlagExitFragment *>(frag)) {
    Log(level, "flag exit|");
  } else if (IsA<ExitFragment *>(frag)) {
    Log(level, "exit");
  } else if (auto code = DynamicCast<CodeFragment *>(frag)) {
    if (code->attr.block_meta && code->attr.is_block_head) {
      auto meta = MetaDataCast<ModuleMetaData *>(code->attr.block_meta);
      Log(level, "%p|", meta->start_pc);
    }
  }
}

// Log info about a fragment, including its decoded instructions.
static void LogFragment(LogLevel level, const Fragment *frag) {
  Log(level, "f%p [fillcolor=%s color=%s label=<{",
      reinterpret_cast<const void *>(frag),
      FragmentBackground(frag), FragmentBorder(frag));
  LogBlockHeader(level, frag);
  if (!IsA<ExitFragment *>(frag)) {
    LogInstructions(level, frag);
    Log(level, "}");
  }
  Log(level, "}>];\n");
}
}  // namespace

// Log a list of fragments as a DOT digraph.
void Log(LogLevel level, FragmentList *frags) {
  Log(level, "digraph {\n"
             "node [fontname=courier shape=record"
             " nojustify=false labeljust=l style=filled];\n"
             "f0 [label=enter];\n");
  LogFragmentEdge(level, nullptr, frags->First());
  for (auto frag : FragmentListIterator(frags)) {
    LogFragmentEdges(level, frag);
    LogFragment(level, frag);
  }
  Log(level, "}\n");
}

}  // namespace granary
