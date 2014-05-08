/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_FRAGMENT_H_
#define GRANARY_CODE_ASSEMBLE_FRAGMENT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"

#include "granary/base/bitset.h"
#include "granary/base/cast.h"
#include "granary/base/disjoint_set.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/tiny_map.h"

#include "granary/cfg/instruction.h"

#include "granary/code/register.h"

namespace granary {

// Forward declarations.
class DecodedBasicBlock;
class LocalControlFlowGraph;
class BlockMetaData;
class FragmentBuilder;
class SSAVariableTracker;
class FlagZone;
class Fragment;
class SSAFragment;
class PartitionEntryFragment;
class PartitionExitFragment;
class FlagEntryFragment;
class FlagExitFragment;
class SSANode;

class SpillInfo {
 public:
  enum {
    MAX_NUM_SPILL_SLOTS = 32
  };

  inline SpillInfo(void)
      : num_slots(0),
        used_slots(),
        gprs_holding_vrs() {
    gprs_holding_vrs.KillAll();
  }

  // Maximum number of slots allocated from this `SpillInfo` object.
  int num_slots;

  // Tracks which spill slots are allocated.
  BitSet<MAX_NUM_SPILL_SLOTS> used_slots;

  // If a GPR is live in `entry_gprs_holding_vrs`, then on entry to the current
  // fragment, the GPR contains the value of a VR.
  RegisterTracker gprs_holding_vrs;

  // Allocate a spill slot from this spill info. Takes an optional offset that
  // can be used to slide the allocated slot by some amount. The offset
  // parameter is used to offset partition-local slot allocations by the number
  // of fragment local slot allocations.
  int AllocateSpillSlot(int offset=0);

  // Free a spill slot from active use.
  void FreeSpillSlot(int slot, int offset=0);
};

// Information about the partition to which a fragment belongs.
class PartitionInfo {
 public:
  explicit PartitionInfo(int id_);

  GRANARY_DEFINE_NEW_ALLOCATOR(PartitionInfo, {
    SHARED = false,
    ALIGNMENT = 1
  })

  // Clear out the number of usage count of registers in this partition.
  void ClearGPRUseCounters(void);

  // Count the number of uses of the arch GPRs in this fragment.
  void CountGPRUses(Fragment *frag);

  // Returns the most preferred arch GPR for use by partition-local register
  // scheduling.
  int PreferredGPRNum(void);

  const int id;

  // The current "round" of partition-local scheduling.
  int scheduler_round;

  // Maximum number of spill slots used by fragments somewhere in this
  // partition.
  int num_local_slots;

  // Counts the number of uses of each GPR within the partition.
  int num_uses_of_gpr[arch::NUM_GENERAL_PURPOSE_REGISTERS];

  // What is the number/index of the preferred GPR for the current VR being
  // allocated. This is -1 if we haven't yet determined the next preferred GPR
  // number.
  int preferred_gpr_num;

  // Partition-local spill info.
  SpillInfo spill;

 private:
  PartitionInfo(void) = delete;
};

// Temporary data stored in a code fragment that's used by different stages
// of the assembly.
union TempData {
 public:
  inline TempData(void)
      : _(0) {}

  uint64_t _;

  Fragment *entry_exit_frag;
};

// Tracks registers used within fragments.
class RegisterUsageInfo {
 public:
  RegisterUsageInfo(void);

  LiveRegisterTracker live_on_entry;
  LiveRegisterTracker live_on_exit;
  int num_uses_of_gpr[arch::NUM_GENERAL_PURPOSE_REGISTERS];

  // Clear out the number of usage count of registers in this fragment.
  void ClearGPRUseCounters(void);

  // Count the number of uses of the arch GPRs in this fragment.
  void CountGPRUses(Fragment *frag);
};

// Targets in/out of this fragment.
enum {
  FRAG_SUCC_FALL_THROUGH = 0,
  FRAG_SUCC_BRANCH = 1
};

// Represents a fragment of instructions. Fragments are like basic blocks.
// Fragments are slightly more restricted than basic blocks, and track other
// useful properties as well.
class Fragment {
 public:
  Fragment(void);

  virtual ~Fragment(void) = default;

  GRANARY_DECLARE_BASE_CLASS(Fragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(Fragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

  // Connects together fragments into a `FragmentList`.
  ListHead list;

  // List of instructions in the fragment.
  InstructionList instrs;

  // The partition to which this fragment belongs.
  DisjointSet<PartitionInfo *> partition;

  // The "flag zone" to which this fragment belongs.
  DisjointSet<FlagZone *> flag_zone;

  // Temporary, pass-specific data.
  TempData temp;

  // Tracks register usage across fragments.
  RegisterUsageInfo regs;

  // Tracks the successor fragments.
  Fragment *successors[2];
  NativeInstruction *branch_instr;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Fragment);
};

typedef ListOfListHead<Fragment> FragmentList;
typedef ListHeadIterator<Fragment> FragmentListIterator;
typedef ReverseListHeadIterator<Fragment> ReverseFragmentListIterator;

// Tracks flag usage within a code fragment.
class FlagUsageInfo {
 public:
  inline FlagUsageInfo(void)
      : entry_live_flags(0),
        exit_live_flags(0),
        all_read_flags(0),
        all_written_flags(0) {}

  // Conservative set of flags that are live on entry to and exit from this
  // fragment.
  uint32_t entry_live_flags;
  uint32_t exit_live_flags;

  // Flags that are killed anywhere within this fragment.
  uint32_t all_read_flags;

  // Flags that are killed anywhere within this fragment.
  uint32_t all_written_flags;
};

// Maintains information about flags usage within a "zone" (a group of non-
// application fragments that are directly connected by control flow). Flag
// zones are delimited by `FlagEntry` and `FlagExit` fragments.
class FlagZone {
 public:
  FlagZone(VirtualRegister flag_save_reg_, VirtualRegister flag_killed_reg_);

  // All flags killed by any instruction within this flag zone.
  uint32_t killed_flags;

  // Live flags on exit from this flags zone.
  uint32_t live_flags;

  // Register used for holding the flags state.
  VirtualRegister flag_save_reg;

  // General-purpose register used in the process of storing the flags. Might
  // be invalid. Might also be a architectural GPR.
  VirtualRegister flag_killed_reg;

  // Live registers on exit from this flags zone.
  LiveRegisterTracker live_regs;

  // Number of fragments in this flag zone. If the number of fragments in a
  // flag zone is `1`
  int num_frags_in_zone;
  Fragment *only_frag;

  GRANARY_DEFINE_NEW_ALLOCATOR(FlagZone, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  FlagZone(void) = delete;
};

// Tracks stack usage info.
class StackUsageInfo {
 public:
  StackUsageInfo(void)
      : is_valid(false),
        is_checked(false),
        has_stack_changing_cfi(false),
        overall_change(0) {}

  // Tells us whether or not the stack pointer in this block appears to
  // reference a valid thread (user or kernel space) stack.
  bool is_valid;

  // Tells us whether or not we have decided on the value of `is_valid`.
  bool is_checked;

  // Does this fragment contain a control-flow instruction that modifies the
  // stack pointer?
  bool has_stack_changing_cfi;

  // Summarizes the overall change made to the stack pointer across this
  // fragment.
  //
  // TODO(pag): Track this!!
  int overall_change;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(StackUsageInfo);
};

// Attributes about a block of code.
class CodeAttributes {
 public:
  inline CodeAttributes(void)
      : has_native_instrs(false),
        modifies_flags(false),
        is_app_code(false),
        is_block_head(false),
        is_compensation_code(false),
        num_inst_preds(0),
        block_meta(nullptr) {}

  // Does this fragment have any native instructions in it, or is it just full
  // or annotations, labels, and other things? We use this to try to avoid
  // adding redundant fragments (e.g. if you had multiple labels in a row).
  bool has_native_instrs;

  // Does this fragment have any instructions that write to the flags?
  bool modifies_flags;

  // Is this a fragment of application instructions? If this is false, then all
  // instructions are either injected from instrumentation, or they could be
  // some application instructions that don't read or write the flags.
  bool is_app_code;

  // Does this fragment represent the beginning of a basic block?
  bool is_block_head;

  // Is this a "compensating" fragment. This is used during register allocation
  // when we have a case like: P -> S1, P -> S2, and the register R is live from
  // P -> S1 but dead from P -> S2. In this case, we add a compensating
  // fragment P -> C -> S2, wherein we treat R as list on entry to C and
  // explicit "kill" it in C with an annotation instruction.
  bool is_compensation_code;

  // The number of non-application (instrumentation) predecessors.
  //
  // Note: We don't care if this value overflows or goes out of sync, as it is
  //       used as a heuristic in step `4_add_entry_exit_fragments`.
  uint8_t num_inst_preds;

  // The meta-data associated with the basic block that this code fragment
  // originates from.
  BlockMetaData *block_meta;

} __attribute__((packed));

typedef TinyMap<VirtualRegister, SSANode *,
                arch::NUM_GENERAL_PURPOSE_REGISTERS * 2> SSANodeMap;

// A fragment with associated SSA vars.
class SSAFragment : public Fragment {
 public:
  virtual ~SSAFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, SSAFragment)

  struct {
    SSANodeMap entry_nodes;
    SSANodeMap exit_nodes;
  } ssa;

  SpillInfo spill;
};

// A fragment of native or instrumentation instructions.
class CodeFragment : public SSAFragment {
 public:
  inline CodeFragment(void)
      : SSAFragment(),
        attr(),
        flags(),
        stack() {}

  virtual ~CodeFragment(void);

  // Attributes relates to the code in this fragment.
  CodeAttributes attr;

  // Tracks flag use within this fragment.
  FlagUsageInfo flags;

  // Tracks the stack usage in this code fragment.
  StackUsageInfo stack;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, CodeFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(CodeFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeFragment);
};

// A fragment where space for virtual registers can be allocated.
class PartitionEntryFragment : public Fragment {
 public:
  PartitionEntryFragment(void) = default;
  virtual ~PartitionEntryFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, PartitionEntryFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(PartitionEntryFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(PartitionEntryFragment);
};

// A fragment where space for virtual registers can be deallocated.
class PartitionExitFragment : public Fragment {
 public:
  PartitionExitFragment(void) = default;
  virtual ~PartitionExitFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, PartitionExitFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(PartitionExitFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(PartitionExitFragment);
};

// A fragment where the native flags state might need to be save.
class FlagEntryFragment : public SSAFragment {
 public:
  FlagEntryFragment(void) = default;
  virtual ~FlagEntryFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, FlagEntryFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(FlagEntryFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(FlagEntryFragment);
};

// A fragment where the native flags state might need to be restored.
class FlagExitFragment : public SSAFragment {
 public:
  FlagExitFragment(void) = default;
  virtual ~FlagExitFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, FlagExitFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(FlagExitFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(FlagExitFragment);
};

enum ExitFragmentKind {
  FRAG_EXIT_NATIVE,
  FRAG_EXIT_FUTURE_BLOCK,
  FRAG_EXIT_EXISTING_BLOCK
};

// A fragment representing either a native basic block, a future basic block
// (either directly or indirectly targeted), or a cached basic block. Exit
// fragments have no successors, and can be treated as exit nodes of the
// fragment control-flow graph.
class ExitFragment : public Fragment {
 public:
  explicit ExitFragment(ExitFragmentKind kind_)
      : Fragment(),
        kind(kind_) {}

  virtual ~ExitFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, ExitFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(ExitFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

  ExitFragmentKind kind;

  union {
    BlockMetaData *block_meta;
  };

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ExitFragment);
};

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_FRAGMENT_H_
