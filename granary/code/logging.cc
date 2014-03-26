/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"
#include "granary/cfg/operand.h"

#include "granary/code/fragment.h"

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

static const char *colors[] = {
  "aliceblue",
  //"antiquewhite",
  //"antiquewhite3",
  //"antiquewhite4",
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

// Log out the dead registers.
static bool LogDeadRegs(LogLevel level, const RegisterUsageTracker &regs) {
  const char *sep = "";
  auto printed_dead = false;
  for (auto i = 0; i < arch::NUM_GENERAL_PURPOSE_REGISTERS; ++i) {
    if (regs.IsDead(i)) {
      VirtualRegister reg(VR_KIND_ARCH_VIRTUAL, 8, static_cast<uint16_t>(i));
      RegisterOperand rop(reg);
      OperandString op_str;
      rop.EncodeToString(&op_str);
      Log(level, "%s%s", sep, op_str.Buffer());
      sep = ",";
      printed_dead = true;
    }
  }
  return printed_dead;
}

// Log the instructions of a fragment.
static void LogFragmentInstructions(LogLevel level, Fragment *frag) {
  enum {
    NUM_COLORS = sizeof colors / sizeof colors[0]
  };

  // Color the block according to the stack. Red means we haven't determined
  // a suitable stack status, grey means that the stack is treated as invalid,
  // and other colors represent different partitionings.
  const char *stack_color = "white";  // Unknown.
  if (0 < frag->stack_id) {
    auto stack_id = static_cast<size_t>(frag->stack_id);
    stack_color = colors[stack_id % NUM_COLORS];
  } else if (0 > frag->stack_id) {
    stack_color = "grey";
  }

  // A thick black border on a fragment also means that the stack is valid.
  auto pen = 0 < frag->stack_id ? " penwidth=2" : "";

  Log(level, "f%p [fillcolor=%s%s label=<%d|{",
      reinterpret_cast<void *>(frag), stack_color, pen, frag->id);

  if (frag->block_meta && (frag->is_block_head || frag->is_future_block_head)) {
    auto meta = MetaDataCast<ModuleMetaData *>(frag->block_meta);
    Log(level, "%p|", meta->start_pc);
  }

  auto printed_dead = LogDeadRegs(level, frag->entry_regs_live);

  if (!frag->is_exit && !frag->is_future_block_head) {
    Log(level, "%s", printed_dead ? "|" : "");
    for (auto instr : ForwardInstructionIterator(frag->first)) {
      auto ninstr = DynamicCast<NativeInstruction *>(instr);
      if (!ninstr) {
        continue;
      }

      Log(level, "%s", ninstr->OpCodeName());

      // Log the input operands.
      auto sep = " ";
      ninstr->ForEachOperand([&] (Operand *op) {
        if (!op->IsWrite()) {
          OperandString op_str;
          op->EncodeToString(&op_str);
          auto prefix = op->IsConditionalRead() ? "cr " : "";
          Log(level, "%s%s%s", sep, prefix, static_cast<const char *>(op_str));
          sep = ", ";
        }
      });

      // Log the output operands.
      sep = " -&gt; ";
      ninstr->ForEachOperand([&] (Operand *op) {
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
      Log(level, "<BR ALIGN=\"LEFT\"/>");  // Keep instructions left-aligned.
    }

    RegisterUsageTracker all_live;
    all_live.ReviveAll();
    if (!all_live.Equals(frag->exit_regs_live)) {
      Log(level, "|");
      LogDeadRegs(level, frag->exit_regs_live);
    }

    Log(level, "}");
  }

  Log(level, "}>];\n");
}

// Log a list of fragments as a DOT digraph.
void Log(LogLevel level, Fragment *frags) {
  Log(level, "digraph {\n"
             "node [fontname=Courier shape=record"
             " nojustify=false labeljust=l style=filled];\n"
             "f0 [color=white fontcolor=white];\n");
  LogFragmentEdge(level, nullptr, frags);
  for (auto frag : FragmentIterator(frags)) {
    LogFragmentEdges(level, frag);
    LogFragmentInstructions(level, frag);
  }
  Log(level, "}\n");
}

}  // namespace granary
