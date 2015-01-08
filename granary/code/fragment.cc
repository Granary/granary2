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

#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
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
    "Note: This is only meaningful if `--debug_log_fragments` is used, or if\n"
    "      one is using GDB commands, such as `xdot-frags`, to print out\n"
    "      fragments.");
#endif  // GRANARY_TARGET_debug, GRANARY_TARGET_test

namespace granary {

GRANARY_IMPLEMENT_NEW_ALLOCATOR(PartitionInfo)
GRANARY_IMPLEMENT_NEW_ALLOCATOR(Fragment)
GRANARY_IMPLEMENT_NEW_ALLOCATOR(CodeFragment)
GRANARY_IMPLEMENT_NEW_ALLOCATOR(PartitionEntryFragment)
GRANARY_IMPLEMENT_NEW_ALLOCATOR(PartitionExitFragment)
GRANARY_IMPLEMENT_NEW_ALLOCATOR(FlagEntryFragment)
GRANARY_IMPLEMENT_NEW_ALLOCATOR(FlagExitFragment)
GRANARY_IMPLEMENT_NEW_ALLOCATOR(NonLocalEntryFragment)
GRANARY_IMPLEMENT_NEW_ALLOCATOR(ExitFragment)

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Fragment, 2),
      (CodeFragment, 2 * 3),
        (FlagEntryFragment, 2 * 3 * 5),
        (FlagExitFragment, 2 * 3 * 7),
      (PartitionEntryFragment, 2 * 11),
      (PartitionExitFragment, 2 * 13),
      (NonLocalEntryFragment, 2 * 17),
      (ExitFragment, 2 * 19))

GRANARY_DEFINE_BASE_CLASS(Fragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, CodeFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, PartitionEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, PartitionExitFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, FlagEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, FlagExitFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, NonLocalEntryFragment)
GRANARY_DEFINE_DERIVED_CLASS_OF(Fragment, ExitFragment)

PartitionInfo::PartitionInfo(int id_)
    : entry_frag(nullptr),
      uses_vrs(false),
      num_slots(0),
      id(id_),
      GRANARY_IF_DEBUG_( num_partition_entry_frags(0) )
      min_frame_offset(0),
      analyze_stack_frame(true) {}

RegisterUsageCounter::RegisterUsageCounter(void) {
  ClearGPRUseCounters();
}

// Clear out the number of usage count of registers in this fragment.
void RegisterUsageCounter::ClearGPRUseCounters(void) {
  memset(&(num_uses_of_gpr[0]), 0, sizeof num_uses_of_gpr);
}

// Count the number of uses of the arch GPRs in all fragments.
void RegisterUsageCounter::CountGPRUses(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    CountGPRUses(frag);
  }
}


// Count the number of uses of the arch GPRs in this fragment.
void RegisterUsageCounter::CountGPRUses(Fragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      CountGPRUses(ninstr);
    } else if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
      CountGPRUses(ainstr);
    }
  }
}

void RegisterUsageCounter::CountGPRUse(VirtualRegister reg) {
  if (reg.IsNative() && reg.IsGeneralPurpose()) {
    num_uses_of_gpr[reg.Number()] += 1;
  }
}

// Returns the number of uses of a particular GPR.
size_t RegisterUsageCounter::NumUses(VirtualRegister reg) const {
  GRANARY_ASSERT(reg.IsNative() && reg.IsGeneralPurpose());
  return num_uses_of_gpr[reg.Number()];
}

// Returns the number of uses of a particular GPR.
size_t RegisterUsageCounter::NumUses(size_t reg_num) const {
  GRANARY_ASSERT(arch::NUM_GENERAL_PURPOSE_REGISTERS > reg_num);
  return num_uses_of_gpr[reg_num];
}

// Count the number of uses of the arch GPRs in a particular instruction.
void RegisterUsageCounter::CountGPRUses(const AnnotationInstruction *instr) {
  if (kAnnotSaveRegister == instr->annotation ||
      kAnnotRestoreRegister == instr->annotation ||
      kAnnotSwapRestoreRegister == instr->annotation) {
    CountGPRUse(instr->Data<VirtualRegister>());
  } else if (kAnnotReviveRegisters == instr->annotation) {
    auto used_regs = instr->Data<UsedRegisterSet>();
    for (auto reg : used_regs) {
      CountGPRUse(reg);
    }
  }
}

CodeAttributes::CodeAttributes(void)
    : branch_is_function_call(false),
      can_add_succ_to_partition(true),
      can_add_pred_to_partition(true),
      has_native_instrs(false),
      reads_flags(false),
      modifies_flags(false),
      is_block_head(false),
      is_compensation_frag(false) {}

Fragment::Fragment(void)
    : list(),
      next(nullptr),
      encoded_order(0),
      num_predecessors(0),
      encoded_size(0),
      encoded_pc(nullptr),
      block_meta(nullptr),
      kind(kFragmentKindInvalid),
      cache(kCodeCacheKindHot),
      stack_status(kStackStatusValid),
      entry_label(nullptr),
      instrs(),
      partition(nullptr),
      flag_zone(),
      app_flags(),
      inst_flags(),
      entry_exit_frag(nullptr),
      successors{nullptr, nullptr},
      branch_instr(nullptr),
      fall_through_instr(nullptr),
      stack_frame() { }

CodeFragment::CodeFragment(void)
    : Fragment(),
      attr(),
      entry_regs(),
      exit_regs(),
      def_regs() {}

CodeFragment::~CodeFragment(void) {}

PartitionEntryFragment::~PartitionEntryFragment(void) {}
PartitionExitFragment::~PartitionExitFragment(void) {}
FlagEntryFragment::~FlagEntryFragment(void) {}
FlagExitFragment::~FlagExitFragment(void) {}
NonLocalEntryFragment::~NonLocalEntryFragment(void) {}
ExitFragment::~ExitFragment(void) {}

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

// Log an instruction operand.
static void LogOperand(LogLevel level, const Operand *op, const char *sep) {
  auto read_prefix = "";
  auto write_prefix = "";
  if (op->IsRegister() || op->IsMemory()) {
    if (op->IsRead()) {
      read_prefix = op->IsConditionalRead() ? "cr" : "r";
    }
    if (op->IsWrite()) {
      write_prefix = op->IsConditionalWrite() ? "cw" : "w";
    }
  }
  OperandString op_str;
  op->EncodeToString(&op_str);
  Log(level, "%s%s%s %s", sep, read_prefix, write_prefix,
      static_cast<const char *>(op_str));
}

static void LogRegister(LogLevel level, VirtualRegister reg, const char *sep) {
  RegisterOperand op(reg);
  OperandString op_str;
  op.EncodeToString(&op_str);
  Log(level, "%s%s", sep, static_cast<const char *>(op_str));
}

#define NEW_LINE "<BR ALIGN=\"LEFT\"/>"
#define FONT_BLUE "<FONT COLOR=\"blue\">"
#define END_FONT "</FONT>"
#define STRIKE "<S>"
#define END_STRIKE "</S>"

#if defined(GRANARY_TARGET_debug) || defined(GRANARY_TARGET_test)
static void LogInstructionNote(LogLevel level, const arch::Instruction *instr) {
  if (!FLAG_debug_log_instr_note) return;
  if (instr->note_create) Log(level, "cnote: %p " NEW_LINE, instr->note_create);
  if (instr->note_alter) Log(level, "anote: %p " NEW_LINE, instr->note_alter);
}
#endif  // GRANARY_TARGET_debug, GRANARY_TARGET_test

static void LogInstruction(LogLevel level, NativeInstruction *instr) {
  auto &ainstr(instr->instruction);
  if (ainstr.IsNoOp()) return;  // Skip no-ops.
  if (!ainstr.WillBeEncoded()) {
    Log(level, STRIKE);
  }
  auto op_sep = " ";
  auto prefix_names = instr->PrefixNames();
  if (prefix_names && prefix_names[0]) Log(level, "%s ", prefix_names);
  Log(level, "%s", instr->ISelName());
  instr->ForEachOperand([&] (Operand *op) {
    LogOperand(level, op, op_sep);
    op_sep = ", ";
  });
  if (!ainstr.WillBeEncoded()) {
    Log(level, END_STRIKE);
  }
  Log(level, NEW_LINE);  // Keep instructions left-aligned.
  GRANARY_IF_DEBUG( LogInstructionNote(level, &ainstr); )
}

static void LogInstruction(LogLevel level, LabelInstruction *instr) {
  Log(level, FONT_BLUE "@label %lx:" END_FONT NEW_LINE,
      reinterpret_cast<uintptr_t>(instr));
}

static void LogUsedRegs(LogLevel level, AnnotationInstruction *instr) {
  Log(level, FONT_BLUE "@used");
  auto sep = " ";
  auto used_regs = instr->Data<UsedRegisterSet>();
  for (auto gpr : used_regs) {
    LogRegister(level, gpr, sep);
    sep = ", ";
  }
  Log(level, END_FONT NEW_LINE);
}

static void LogInstruction(LogLevel level, AnnotationInstruction *instr) {
  auto kind = "";
  if (kAnnotSaveRegister == instr->annotation) {
    kind = "@save";
  } else if (kAnnotRestoreRegister == instr->annotation) {
    kind = "@restore";
  } else if (kAnnotSwapRestoreRegister == instr->annotation) {
    kind = "@swap_restore";
  } else if (kAnnotReviveRegisters == instr->annotation) {
    return LogUsedRegs(level, instr);
  } else if (kAnnotInvalidStack == instr->annotation) {
    Log(level, FONT_BLUE "@invalid_stack" END_FONT NEW_LINE);
    return;
  } else if (kAnnotCondLeaveNativeStack == instr->annotation) {
    Log(level, FONT_BLUE "@offstack" END_FONT NEW_LINE);
    return;
  } else if (kAnnotCondEnterNativeStack == instr->annotation) {
    Log(level, FONT_BLUE "@onstack" END_FONT NEW_LINE);
    return;
  } else if (kAnnotUpdateAddressWhenEncoded == instr->annotation) {
    Log(level, FONT_BLUE "@update_addr_with_encoded_pc" END_FONT NEW_LINE);
    return;
  } else {
    return;
  }
  OperandString op_str;
  RegisterOperand op(instr->Data<VirtualRegister>());
  op.EncodeToString(&op_str);
  Log(level, FONT_BLUE "%s %s" END_FONT NEW_LINE, kind,
      static_cast<const char *>(op_str));
}

// Log the instructions of a fragment.
static void LogInstructions(LogLevel level, const Fragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      LogInstruction(level, ninstr);
    } else if (auto linstr = DynamicCast<LabelInstruction *>(instr)) {
      LogInstruction(level, linstr);
    } else if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
      LogInstruction(level, ainstr);
    }
  }
}

// If this fragment is the head of a basic block then log the basic block's
// entry address.
static void LogBlockHeader(LogLevel level, const Fragment *frag) {
  if (frag->encoded_order) Log(level, "%d ", frag->encoded_order);
  switch (frag->cache) {
    case kCodeCacheKindHot: Log(level, "hot "); break;
    case kCodeCacheKindCold: Log(level, "cold "); break;
    case kCodeCacheKindFrozen: Log(level, "frozen "); break;
    case kCodeCacheKindSubZero: Log(level, "sub zero "); break;
    case kCodeCacheKindEdge: Log(level, "edge "); break;
  }
  if (IsA<PartitionEntryFragment *>(frag)) {
    Log(level, "allocate space|");
  } else if (IsA<PartitionExitFragment *>(frag)) {
    Log(level, "deallocate space|");
  } else if (IsA<FlagEntryFragment *>(frag)) {
    Log(level, "save flags|");
  } else if (IsA<FlagExitFragment *>(frag)) {
    Log(level, "restore flags|");
  } else if (IsA<ExitFragment *>(frag)) {
    Log(level, "exit");

  } else if (auto code = DynamicCast<CodeFragment *>(frag)) {
    auto partition = code->partition.Value();
    Log(level, kFragmentKindApp == code->kind ? "app " : "inst ");
    if (partition) Log(level, "p%u ", partition->id);
    if (code->attr.is_compensation_frag) Log(level, "comp ");
    if (code->attr.modifies_flags) Log(level, "mflags ");
    if (!code->attr.can_add_succ_to_partition) Log(level, "!addsucc2p ");
    if (!code->attr.can_add_pred_to_partition) Log(level, "!add2predp ");
    if (kStackStatusInvalid == code->stack_status) Log(level, "badstack ");
    if (code->encoded_size) Log(level, "size=%lu ", code->encoded_size);
    if (code->branch_instr) {
      Log(level, "binstr=%s ", code->branch_instr->OpCodeName());
    }
    if (code->app_flags.entry_live_flags) {
      Log(level, "aflags=%x ", code->app_flags.entry_live_flags);
    }
    if (code->inst_flags.entry_live_flags) {
      Log(level, "iflags=%x ", code->inst_flags.entry_live_flags);
    }

    if (code->block_meta && code->attr.is_block_head) {
      auto meta = MetaDataCast<AppMetaData *>(code->block_meta);
      Log(level, "|%p", meta->start_pc);
    }
  }
}

static void LogEntryRegs(LogLevel level, const Fragment *frag) {
  if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
    auto sep = "|";
    for (auto vr_id : code_frag->entry_regs) {
      Log(level, "%s%%%d", sep, static_cast<int>(vr_id));
      sep = ",";
    }
  }
}

static void LogExitRegs(LogLevel level, const Fragment *frag) {
  if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
    auto sep = "|";
    for (auto vr_id : code_frag->exit_regs) {
      Log(level, "%s%%%d", sep, static_cast<int>(vr_id));
      sep = ",";
    }
  }
}

// Log info about a fragment, including its decoded instructions.
static void LogFragment(LogLevel level, const Fragment *frag) {
  Log(level, "f%p [fillcolor=%s label=<{",
      reinterpret_cast<const void *>(frag), FragmentBackground(frag));
  LogBlockHeader(level, frag);
  LogEntryRegs(level, frag);
  if (frag->instrs.First()) {
    Log(level, "|");
    LogInstructions(level, frag);
  }
  LogExitRegs(level, frag);
  Log(level, "}>];\n");
}
}  // namespace

// Log a list of fragments as a DOT digraph.
void Log(LogLevel level, FragmentList *frags) {
  Log(level, "digraph {\n"
             "node [fontname=courier shape=record "
             "nojustify=false labeljust=l style=filled];\n"
             "f0x0 [label=enter];\n");
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
    Instruction::Unlink(instr);  // Will self-destruct.
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
