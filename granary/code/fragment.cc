/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"
#include "granary/cfg/operand.h"

#include "granary/code/edge.h"
#include "granary/code/fragment.h"

#include "granary/app.h"
#include "granary/breakpoint.h"
#include "granary/util.h"

#include "os/logging.h"

#ifdef GRANARY_TARGET_debug
# include "granary/base/option.h"
GRANARY_DEFINE_bool(debug_log_instr_note, false,
    "Should the note field, if present, be logged along with the instructions? "
    "In some situations, this can help to pinpoint what function was "
    "responsible for introducing an instruction. The default value is `no`.\n"
    "\n"
    "An instruction note is the return address of the function that likely "
    "created the instruction. This can be helpful when trying to discover the "
    "source of an instruction.\n"
    "\n"
    "Note: This is only meaningful if `--debug_log_fragments` is used.");
#endif  // GRANARY_TARGET_debug

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Fragment, 2),
      (SSAFragment, 2 * 3),
        (CodeFragment, 2 * 3 * 5),
        (FlagEntryFragment, 2 * 3 * 7),
        (FlagExitFragment, 2 * 3 * 11),
      (PartitionEntryFragment, 2 * 13),
      (PartitionExitFragment, 2 * 17),
      (ExitFragment, 2 * 19))

GRANARY_DEFINE_BASE_CLASS(Fragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, SSAFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, CodeFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, PartitionEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, PartitionExitFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, FlagEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, FlagExitFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, ExitFragment)

PartitionInfo::PartitionInfo(int id_)
    : id(id_),
      num_slots(0),
      GRANARY_IF_DEBUG_( num_partition_entry_frags(0) )
      analyze_stack_frame(false),
      min_frame_offset(0),
      entry_frag(nullptr) {}

RegisterUsageInfo::RegisterUsageInfo(void)
    : live_on_entry(),
      live_on_exit() {}

RegisterUsageCounter::RegisterUsageCounter(void) {
  ClearGPRUseCounters();
}

// Clear out the number of usage count of registers in this fragment.
void RegisterUsageCounter::ClearGPRUseCounters(void) {
  memset(&(num_uses_of_gpr[0]), 0, sizeof num_uses_of_gpr);
}

namespace {
static void CountGPRUse(RegisterUsageCounter *counter, VirtualRegister reg) {
  if (reg.IsNative() && reg.IsGeneralPurpose()) {
    counter->num_uses_of_gpr[reg.Number()] += 1;
  }
}
}  // namespace

// Count the number of uses of the arch GPRs in this fragment.
void RegisterUsageCounter::CountGPRUses(Fragment *frag) {
  auto operand_counter = [=] (Operand *op) {
    if (auto reg_op = DynamicCast<RegisterOperand *>(op)) {
      CountGPRUse(this, reg_op->Register());
    } else if (auto mem_op = DynamicCast<MemoryOperand *>(op)) {
      VirtualRegister r1, r2;
      if (mem_op->CountMatchedRegisters({&r1, &r2})) {
        CountGPRUse(this, r1);
        CountGPRUse(this, r2);
      }
    }
  };
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      ninstr->ForEachOperand(operand_counter);
    }
  }
}

CodeAttributes::CodeAttributes(void)
    : block_meta(nullptr),
      branches_to_code(false),
      branch_is_indirect(false),
      branch_is_function_call(false),
      branch_is_jump(false),
      can_add_succ_to_partition(true),
      can_add_pred_to_partition(true),
      has_native_instrs(false),
      reads_flags(false),
      modifies_flags(false),
      is_block_head(false),
      is_return_target(false),
      is_compensation_code(false),
      is_in_edge_code(false),
      follows_cfi(false),
      num_predecessors(0) {}

Fragment::Fragment(void)
    : list(),
      next(nullptr),
      encoded_order(0),
      encoded_size(0),
      encoded_pc(nullptr),
      instrs(),
      partition(nullptr),
      flag_zone(nullptr),
      app_flags(),
      inst_flags(),
      temp(),
      successors{nullptr, nullptr},
      branch_instr(nullptr),
      stack_frame() { }

SSAFragment::SSAFragment(void)
    : Fragment(),
      ssa(),
      spill() {}

SSAFragment::~SSAFragment(void) {}

CodeFragment::CodeFragment(void)
    : SSAFragment(),
      attr(),
      type(CODE_TYPE_UNKNOWN),
      stack() {}
CodeFragment::~CodeFragment(void) {}

PartitionEntryFragment::~PartitionEntryFragment(void) {}
PartitionExitFragment::~PartitionExitFragment(void) {}
FlagEntryFragment::~FlagEntryFragment(void) {}
FlagExitFragment::~FlagExitFragment(void) {}
ExitFragment::~ExitFragment(void) {}

FlagZone::FlagZone(VirtualRegister flag_save_reg_,
                   VirtualRegister flag_killed_reg_)
    : killed_flags(0),
      live_flags(0),
      flag_save_reg(flag_save_reg_),
      flag_killed_reg(flag_killed_reg_),
      used_regs(),
      live_regs() {}

namespace os {

// Publicly visible for GDBs sake.
static const char *fragment_partition_color[] = {
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

enum {
  NUM_COLORS = sizeof fragment_partition_color /
               sizeof fragment_partition_color[0]
};

// Color the fragment according to the partition to which it belongs. This is
// meant to be a visual cue, not a perfect association with the fragment's
// partition id.
static const char *FragmentBackground(const Fragment *frag) {
  if (auto partition_info = frag->partition.Value()) {
    if (auto stack_id = static_cast<size_t>(partition_info->id)) {
      return fragment_partition_color[stack_id % NUM_COLORS];
    }
  }
  return "white";
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
                    (op->IsConditionalWrite() ? "rcw " : "rw ") :
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
      auto &ainstr(ninstr->instruction);
      if (ainstr.IsNoOp()) continue;  // Skip no-ops.

      if (!ainstr.WillBeEncoded()) {
        Log(level, "N/E! ");
      }
      auto prefix_names = ninstr->PrefixNames();
      if (prefix_names && prefix_names[0]) Log(level, "%s ", prefix_names);
      Log(level, "%s", ninstr->ISelName());
      LogInputOperands(level, ninstr);
      LogOutputOperands(level, ninstr);
      Log(level, "<BR ALIGN=\"LEFT\"/>");  // Keep instructions left-aligned.
#ifdef GRANARY_TARGET_debug
      if (FLAG_debug_log_instr_note) {
        if (ainstr.note_create) {
          Log(level, "cnote: %p <BR ALIGN=\"LEFT\"/>", ainstr.note_create);
        }
        if (ainstr.note_alter) {
          Log(level, "anote: %p <BR ALIGN=\"LEFT\"/>", ainstr.note_alter);
        }
      }
#endif  // GRANARY_TARGET_debug
    } else if (IsA<LabelInstruction *>(instr)) {
      Log(level, "LABEL %lx:<BR ALIGN=\"LEFT\"/>",
          reinterpret_cast<uintptr_t>(instr));
    }
  }
}

// If this fragment is the head of a basic block then log the basic block's
// entry address.
static void LogBlockHeader(LogLevel level, const Fragment *frag) {
  if (frag->encoded_order) Log(level, "%d ", frag->encoded_order);

  if (IsA<PartitionEntryFragment *>(frag)) {
    Log(level, "allocate space|");
  } else if (IsA<PartitionExitFragment *>(frag)) {
    Log(level, "deallocate space|");
  } else if (IsA<FlagEntryFragment *>(frag)) {
    Log(level, "save flags|");
  } else if (IsA<FlagExitFragment *>(frag)) {
    Log(level, "restore flags|");
  } else if (auto exit_frag = DynamicCast<ExitFragment *>(frag)) {
    switch (exit_frag->kind) {
      case FRAG_EXIT_NATIVE: Log(level, "native"); break;
      case FRAG_EXIT_FUTURE_BLOCK_DIRECT:
        Log(level, "direct edge -&gt; app %p",
            MetaDataCast<AppMetaData *>(exit_frag->block_meta)->start_pc);
        break;
      case FRAG_EXIT_FUTURE_BLOCK_INDIRECT: Log(level, "indirect edge"); break;
      case FRAG_EXIT_EXISTING_BLOCK: Log(level, "existing block"); break;
    }

  } else if (auto code = DynamicCast<CodeFragment *>(frag)) {
    auto partition = code->partition.Value();
    Log(level, CODE_TYPE_APP == code->type ? "app " : "inst ");
    if (partition) Log(level, "p%u ", partition->id);
    if (code->attr.is_in_edge_code) Log(level, "inedge ");
    if (code->attr.modifies_flags) Log(level, "mflags ");
    if (!code->attr.can_add_succ_to_partition) Log(level, "!addsucc2p ");
    if (!code->attr.can_add_pred_to_partition) Log(level, "!add2predp ");
    if (code->attr.branches_to_code) Log(level, "-&gt;code ");
    if (code->attr.branch_is_indirect) Log(level, "-&gt;ind ");
    if (code->attr.follows_cfi) Log(level, "cfi~&gt; ");
    if (STACK_INVALID == code->stack.status) Log(level, "badstack ");
    if (code->encoded_size) Log(level, "size=%d ", code->encoded_size);
    if (code->branch_instr) {
      Log(level, "binstr=%s ", code->branch_instr->OpCodeName());
    }

    if (code->attr.block_meta && code->attr.is_block_head) {
      auto meta = MetaDataCast<AppMetaData *>(code->attr.block_meta);
      Log(level, "|%p", meta->start_pc);
    } else if (code->attr.is_compensation_code) {
      Log(level, "|compensation code");
    }
  }
}

static void LogLiveRegisters(LogLevel level, const Fragment *frag) {
  auto sep = "";
  auto logged = false;
  for (auto reg : frag->regs.live_on_entry) {
    RegisterOperand op(reg);
    OperandString op_str;
    if (!logged) Log(level, "|");
    op.EncodeToString(&op_str);
    Log(level, "%s%s", sep, op_str.Buffer());
    sep = ",";
    logged = true;
  }

}

static void LogLiveVRs(LogLevel level , const Fragment *frag) {
  auto ssa_frag = DynamicCast<SSAFragment *>(frag);
  if (!ssa_frag) return;
  auto logged = false;
  auto sep = "";
  for (auto vr : ssa_frag->ssa.entry_nodes.Keys()) {
    if (vr.IsVirtual()) {
      if (!logged) Log(level, "|");
      Log(level, "%s%%%d", sep, vr.Number());
      sep = ",";
      logged = true;
    }
  }
}

// Log info about a fragment, including its decoded instructions.
static void LogFragment(LogLevel level, const Fragment *frag) {
  Log(level, "f%p [fillcolor=%s label=<{", reinterpret_cast<const void *>(frag),
      FragmentBackground(frag));
  LogBlockHeader(level, frag);
  LogLiveRegisters(level, frag);
  LogLiveVRs(level, frag);
  if (frag->instrs.First()) {
    Log(level, "|");
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

}  // namespace os
namespace {

// Free the instructions from a fragment.
static void FreeInstructions(Fragment *frag) {
  auto instr = frag->instrs.First();
  for (Instruction *next_instr(nullptr); instr; instr = next_instr) {
    next_instr = instr->Next();
    instr->UnsafeUnlink();  // Will self-destruct.
  }
}

// Free the partition info for a fragment.
static void FreePartitionInfo(Fragment *frag) {
  auto &partition(frag->partition.Value());
  if (partition) {
    delete partition;
    partition = nullptr;
  }
}

}  // namespace

// Free all fragments, their instructions, etc.
void FreeFragments(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    FreeInstructions(frag);
    FreePartitionInfo(frag);
  }
  Fragment *frag(frags->First());
  Fragment *next_frag(nullptr);
  for (; frag; frag = next_frag) {
    next_frag = frag->next;
    delete frag;
  }
}

}  // namespace granary
