/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_FRAGMENT_H_
#define GRANARY_CODE_FRAGMENT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "arch/base.h"

#include "granary/base/bitset.h"
#include "granary/base/cast.h"
#include "granary/base/disjoint_set.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/tiny_map.h"
#include "granary/base/tiny_set.h"

#include "granary/cfg/instruction.h"

#include "granary/code/register.h"

#include "granary/cache.h"

#include "os/logging.h"

namespace granary {

// Forward declarations.
class BlockMetaData;

class Fragment;
class PartitionEntryFragment;
class PartitionExitFragment;
class FlagEntryFragment;
class FlagExitFragment;
class DirectEdge;

// Tracks the size of the stack frame within the current fragment/partition.
// We are guaranteed that the fragments within a partition form a DAG, so if
// the stack is valid, then we can set bounds on the stack's size, and then
// spill/fill virtual registers from the stack.
class StackFrameInfo {
 public:
  inline StackFrameInfo(void)
      : entry_offset(0),
        exit_offset(0) {}

  int entry_offset;
  int exit_offset;
};


// Maintains information about flags usage within a "zone" (a group of non-
// application fragments that are directly connected by control flow). Flag
// zones are delimited by `FlagEntry` and `FlagExit` fragments.
union FlagZone {
 public:
  inline FlagZone(void)
      : flags(0) {}

  inline bool operator==(const FlagZone &that) const {
    return flags == that.flags;
  }

  struct {
    // All flags killed by any instruction within this flag zone.
    uint32_t killed_flags;

    // Live flags on exit from this flags zone.
    uint32_t live_flags;
  } __attribute__((packed));

  uint64_t flags;
};

// Information about the partition to which a fragment belongs.
class PartitionInfo {
 public:
  explicit PartitionInfo(int id_);

  GRANARY_DEFINE_NEW_ALLOCATOR(PartitionInfo, {
    SHARED = false,
    ALIGNMENT = 1
  })

  // The first fragment in this partition. This will either be a
  // `PartitionEntryFragment` or a `CodeFragment`.
  Fragment *entry_frag;

  // Does this fragment use any virtual registers?
  bool uses_vrs;

  // The number of slots allocated in this partition. This includes fragment-
  // local and partition-local slots.
  size_t num_slots;

  const int id;

  // For sanity checking: our stack analysis might yield undefined behavior of
  // a partition has more than one entry points.
  GRANARY_IF_DEBUG( int num_partition_entry_frags; )

  // Should we analyze the stack frames?
  int min_frame_offset;
  bool analyze_stack_frame;

 private:
  PartitionInfo(void) = delete;
};

typedef DisjointSet<PartitionInfo *> PartitionId;

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

// Targets in/out of this fragment.
enum FragmentSuccessorSelector {
  kFragSuccFallThrough = 0,
  kFragSuccBranch = 1
};

enum FragmentKind {
  // The code type of this fragment hasn't (yet) been decided.
  kFragmentKindInvalid,

  // Fragment containing application instructions and/or instrumentation
  // instructions that don't modify the flags state.
  kFragmentKindApp,

  // Fragment containing instrumentation instructions, and/or application
  // instructions that don't read/write the flags state.
  //
  // Note: The extra condition of app instructions not *reading* the flags
  //       state is super important!
  kFragmentKindInst
};

typedef DisjointSet<FlagZone> FlagZoneId;

// By default, the stack status is considered valid, *unless* we see that any
// fragment has an invalid status, in which case all fragments are considered
// invalid.
enum StackStatus {
  kStackStatusValid,
  kStackStatusInvalid
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
  ListHead<Fragment> list;

  // Connects together fragments into an `EncodeOrderedFragmentList`.
  Fragment *next;
  int encoded_order;

  // Number of predecessor fragments. Doesn't actually need to be perfectly
  // accurate/consistent. We use to propagate code cache kinds. Here, we want
  // to propagate code cache kinds to successors when our successor only has
  // a single predecessor.
  int num_predecessors;

  // Where was this fragment encoded?
  size_t encoded_size;
  CachePC encoded_pc;

  // The meta-data associated with the basic block that this fragment
  // originates from.
  BlockMetaData *block_meta;

  // What kind of fragment is this? This is primarily used by `CodeFragment`
  // fragments, but it helps to be able to recognize all other kinds of
  // fragments as application fragments.
  FragmentKind kind;
  CodeCacheKind cache;

  // Tells us whether or not the stack pointer in this block appears to
  // reference a valid thread (user or kernel space) stack.
  StackStatus stack_status;

  // List of instructions in the fragment.
  LabelInstruction *entry_label;
  InstructionList instrs;

  // The partition to which this fragment belongs.
  PartitionId partition;

  // The "flag zone" to which this fragment belongs.
  FlagZoneId flag_zone;

  // Tracks flag use within this fragment.
  FlagUsageInfo app_flags;
  FlagUsageInfo inst_flags;

  // Temporary, pass-specific data.
  Fragment *entry_exit_frag;

  // Tracks the successor fragments.
  Fragment *successors[2];
  NativeInstruction *branch_instr;
  NativeInstruction *fall_through_instr;

  // Tracks information gathered about the current function's activation frame
  // within this fragment.
  StackFrameInfo stack_frame;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Fragment);
};

typedef ListOfListHead<Fragment> FragmentList;
typedef ListHeadIterator<Fragment> FragmentListIterator;
typedef ReverseListHeadIterator<Fragment> ReverseFragmentListIterator;
typedef LinkedListIterator<Fragment> EncodeOrderedFragmentIterator;


// Used to count the number of uses of each GPR within one or more fragments.
class RegisterUsageCounter {
 public:
  RegisterUsageCounter(void);

  // Clear out the number of usage count of registers in this fragment.
  void ClearGPRUseCounters(void);

  // Count the number of uses of the arch GPRs in all fragments.
  void CountGPRUses(FragmentList *frags);

  // Count the number of uses of the arch GPRs in this fragment.
  void CountGPRUses(Fragment *frag);

  void CountGPRUse(VirtualRegister reg);

  // Returns the number of uses of a particular GPR.
  size_t NumUses(VirtualRegister reg) const;

  // Returns the number of uses of a particular GPR.
  size_t NumUses(size_t reg_num) const;

  // Count the number of uses of the arch GPRs in a particular instruction.
  //
  // Note: This function has an architecture-specific implementation.
  void CountGPRUses(const NativeInstruction *instr);

  // Count the number of uses of the arch GPRs in a particular instruction.
  void CountGPRUses(const AnnotationInstruction *instr);

 private:
  size_t num_uses_of_gpr[arch::NUM_GENERAL_PURPOSE_REGISTERS];
};

namespace os {

// Log a list of fragments as a DOT digraph.
void Log(LogLevel level, FragmentList *frags);
}  // namespace os

// Free all fragments, their instructions, etc.
void FreeFragments(FragmentList *frags);

// Attributes about a block of code.
class alignas(alignof(void *)) CodeAttributes {
 public:
  CodeAttributes(void);

  // Is the branch instruction a function call or a jump (direct or indirect)?
  bool branch_is_function_call:1;

  // Can this fragment be added into another partition? We use this to prevent
  // fragments that only contain things like IRET, RET, etc. from being added
  // into an existing partition. This would be bad because we lose control at
  // things like IRET and unspecialized RETs.
  //
  // If we have F1 -> F2, and !F1.attr.can_add_succ_to_partition, then don't
  // place F1 and F2 into the same partition (in the forward direction). If
  // there is an edge such that F2 -> .. -> F1, then F1 and F2 might be added
  // to the same partition. Therefore, this is a local constraint only.
  bool can_add_succ_to_partition:1;

  // Can this fragment be added into its successor's partition? This is similar
  // to `can_add_succ_to_partition`. The major concern is that we don't want
  // the same partition to span across something like a function or system
  // call. One reason this is the case is because we can't prove that a register
  // that we save before a function/system call should unconditionally hold the
  // saved value after the function/system call. In the case of a system call,
  // we could make a stronger assumption based on the ABI; however, the current
  // approach to tracking register liveness is not prepared to handle such
  // assumptions, as it is a backward-only data-flow problem. For example, if
  // we say that RCX is dead after a syscall, then:
  //
  //       F1
  //      /  \
  //     F2  syscall -->
  //      \  /
  //       F3
  //
  // We would see that RCX is dead in F1, but it's not clear if it is live or
  // dead in F2 because the system doesn't propagate that "death" to F3.
  bool can_add_pred_to_partition:1;

  // Does this fragment have any native instructions in it, or is it just full
  // or annotations, labels, and other things? We use this to try to avoid
  // adding redundant fragments (e.g. if you had multiple labels in a row).
  bool has_native_instrs:1;

  // Does this fragment have any instructions that read/write to the flags?
  bool reads_flags:1;
  bool modifies_flags:1;

  // Does this fragment represent the beginning of a basic block?
  bool is_block_head:1;

  // Is this a compensation fragment?
  bool is_compensation_frag:1;

} __attribute__((packed));

// Set of spill slots.
typedef BitSet<arch::MAX_NUM_SPILL_SLOTS> SpillSlotSet;

// Set of virtual registers.
typedef TinySet<uint16_t, arch::NUM_GENERAL_PURPOSE_REGISTERS> VRIdSet;

// Count of how many times some register is used / updated / etc.
typedef TinyMap<uint16_t, uint16_t, arch::NUM_GENERAL_PURPOSE_REGISTERS>
        VRIdCountSet;

// A fragment of native or instrumentation instructions.
class CodeFragment : public Fragment {
 public:
  CodeFragment(void);
  virtual ~CodeFragment(void);

  // Attributes relates to the code in this fragment.
  CodeAttributes attr;

  // Set of live *virtual* registers on entry. We assume that all native
  // registers are live on entry.
  VRIdSet entry_regs;
  VRIdSet exit_regs;

  // Number of times virtual registers are defined in this fragment. This
  // includes read/write operations that modify the value in-place.
  VRIdCountSet def_regs;

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
class FlagEntryFragment : public CodeFragment {
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
class FlagExitFragment : public CodeFragment {
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

// Special class of fragment for "straggler" fragments / instructions.
class NonLocalEntryFragment : public Fragment {
 public:
  NonLocalEntryFragment(void) = default;
  virtual ~NonLocalEntryFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, NonLocalEntryFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(NonLocalEntryFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(NonLocalEntryFragment);
};

// A fragment representing either a native basic block, a future basic block
// (either directly or indirectly targeted), or a cached basic block. Exit
// fragments have no successors, and can be treated as exit nodes of the
// fragment control-flow graph.
class ExitFragment : public Fragment {
 public:
  ExitFragment(void)
      : Fragment(),
        direct_edge(nullptr) {
    this->kind = kFragmentKindApp;
  }

  virtual ~ExitFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, ExitFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(ExitFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

  // Pointer to one of the edge structures associated with this fragment.
  DirectEdge *direct_edge;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ExitFragment);
};

}  // namespace granary

#endif  // GRANARY_CODE_FRAGMENT_H_
