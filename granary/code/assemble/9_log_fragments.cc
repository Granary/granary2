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
  if (frag->fall_through_target) {
    LogFragmentEdge(level, frag, frag->fall_through_target);
  }
  if (frag->branch_target) {
    LogFragmentEdge(level, frag, frag->branch_target);
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

// Color the fragment according to the partition to which it belongs. This is
// meant to be a visual cue, not a perfect association with the fragment's
// partition id.
static const char *FragmentBackground(const Fragment *frag) {
  enum {
    NUM_COLORS = sizeof partition_color / sizeof partition_color[0]
  };
  if (0 < frag->partition_id) {
    auto stack_id = static_cast<size_t>(frag->partition_id);
    return partition_color[stack_id % NUM_COLORS];
  } else {
    return "white";
  }
}

// Log some set of dead registers (e.g. dead regs on entry or exit).
static bool LogDeadRegs(LogLevel level, const Fragment *frag,
                        const LiveRegisterTracker &regs) {
  if (FRAG_KIND_APPLICATION != frag->kind && FRAG_KIND_INSTRUMENTATION != frag->kind) {
    return false;
  }
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
  for (auto instr : ForwardInstructionIterator(frag->first)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (ninstr->IsAppInstruction()) {
        Log(level, "<FONT POINT-SIZE=\"11\" FACE=\"Courier-Bold\">");
      }
      Log(level, "%s", ninstr->OpCodeName());
      LogInputOperands(level, ninstr);
      LogOutputOperands(level, ninstr);
      if (ninstr->IsAppInstruction()) {
        Log(level, "</FONT>");
      }
      Log(level, "<BR ALIGN=\"LEFT\"/>");  // Keep instructions left-aligned.
    }
  }
}

// Log the dead registers on exit of a fragment.
static void LogDeadExitRegs(LogLevel level, const Fragment *frag) {
  if (FRAG_KIND_APPLICATION != frag->kind &&
      FRAG_KIND_INSTRUMENTATION != frag->kind) {
    return;
  }
  LiveRegisterTracker all_live;
  all_live.ReviveAll();
  if (!all_live.Equals(frag->exit_regs_live)) {
    Log(level, "|");
    LogDeadRegs(level, frag, frag->exit_regs_live);
  }
}

// If this fragment is the head of a basic block then log the basic block's
// entry address.
static void LogBlockHeader(LogLevel level, const Fragment *frag) {
  if (FRAG_KIND_PARTITION_ENTRY == frag->kind) {
    Log(level, "partition entry|");
  } else if (FRAG_KIND_PARTITION_EXIT == frag->kind) {
    Log(level, "partition exit|");
  } else if (FRAG_KIND_FLAG_ENTRY == frag->kind) {
    Log(level, "flag entry|");
  } else if (FRAG_KIND_FLAG_EXIT == frag->kind) {
    Log(level, "flag exit|");
  } else if (frag->block_meta &&
      (frag->is_decoded_block_head || frag->is_future_block_head)) {
    auto meta = MetaDataCast<ModuleMetaData *>(frag->block_meta);
    Log(level, "%p|", meta->start_pc);
  }
}

// Log info about a fragment, including its decoded instructions.
static void LogFragment(LogLevel level, const Fragment *frag) {
  Log(level, "f%p [fillcolor=%s label=<%d|{",
      reinterpret_cast<const void *>(frag), FragmentBackground(frag), frag->id);
  LogBlockHeader(level, frag);
  auto printed_entry_dead_regs = LogDeadRegs(level, frag,
                                             frag->entry_regs_live);
  if (!frag->is_exit && !frag->is_future_block_head) {
    Log(level, "%s", printed_entry_dead_regs ? "|" : "");
    LogInstructions(level, frag);
    LogDeadExitRegs(level, frag);
    Log(level, "}");
  }
  Log(level, "}>];\n");
}
}  // namespace

// Log a list of fragments as a DOT digraph.
void Log(LogLevel level, Fragment *frags) {
  Log(level, "digraph {\n"
             "node [fontname=courier shape=record"
             " nojustify=false labeljust=l style=filled];\n"
             "f0 [color=white fontcolor=white];\n");
  LogFragmentEdge(level, nullptr, frags);
  for (auto frag : FragmentIterator(frags)) {
    frag->transient_back_link = nullptr;
  }
  for (auto frag : FragmentIterator(frags)) {
    LogFragmentEdges(level, frag);
    LogFragment(level, frag);
  }
  Log(level, "}\n");
}

}  // namespace granary
