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
#include "granary/code/ssa.h"

#include "os/logging.h"

namespace granary {

// Forward declarations.
class BlockMetaData;

class SSASpillStorage;
class FlagZone;
class Fragment;
class SSAFragment;
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

enum EdgeKind {
  EDGE_KIND_INVALID,
  EDGE_KIND_DIRECT,
  EDGE_KIND_INDIRECT
};

// Edge information about a partition or fragment.
struct EdgeInfo {
 public:
  inline EdgeInfo(void)
      : kind(EDGE_KIND_INVALID),
        direct(nullptr) {}

  // Should this partition be allocated in some direct edge code location?
  EdgeKind kind;

  union {
    DirectEdge *direct;
  };
};

// Information about the partition to which a fragment belongs.
class PartitionInfo {
 public:
  explicit PartitionInfo(int id_);

  GRANARY_DEFINE_NEW_ALLOCATOR(PartitionInfo, {
    SHARED = false,
    ALIGNMENT = 1
  })

  const int id;

  // The number of slots allocated in this partition. This includes fragment-
  // local and partition-local slots.
  size_t num_slots;

  // For sanity checking: our stack analysis might yield undefined behavior of
  // a partition has more than one entry points.
  GRANARY_IF_DEBUG( int num_partition_entry_frags; )

  // Used to verify that a virtual register is not defined in one fragment
  // and used in another.
  GRANARY_IF_DEBUG( TinySet<VirtualRegister,
                            arch::NUM_GENERAL_PURPOSE_REGISTERS> used_vrs; )

  // Should we analyze the stack frames?
  bool analyze_stack_frame;
  int min_frame_offset;

  // The first fragment in this partition. This will either be a
  // `PartitionEntryFragment` or a `CodeFragment`.
  Fragment *entry_frag;

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

  LiveRegisterSet live_on_entry;
  LiveRegisterSet live_on_exit;

};

// Used to count the number of uses of each GPR within one or more fragments.
class RegisterUsageCounter {
 public:
  RegisterUsageCounter(void);

  // Clear out the number of usage count of registers in this fragment.
  void ClearGPRUseCounters(void);

  // Count the number of uses of the arch GPRs in this fragment.
  void CountGPRUses(Fragment *frag);

  size_t num_uses_of_gpr[arch::NUM_GENERAL_PURPOSE_REGISTERS];
};

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

enum FragmentType {
  // The code type of this fragment hasn't (yet) been decided.
  FRAG_TYPE_UNKNOWN,

  // Fragment containing application instructions and/or instrumentation
  // instructions that don't modify the flags state.
  FRAG_TYPE_APP,

  // Fragment containing instrumentation instructions, and/or application
  // instructions that don't read/write the flags state.
  //
  // Note: The extra condition of app instructions not *reading* the flags
  //       state is super important!
  FRAG_TYPE_INST
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

  // Where was this fragment encoded?
  size_t encoded_size;
  CachePC encoded_pc;

  // What kind of fragment is this? This is primarily used by `CodeFragment`
  // fragments, but it helps to be able to recognize all other kinds of
  // fragments as application fragments.
  FragmentType type;

  // List of instructions in the fragment.
  LabelInstruction *entry_label;
  InstructionList instrs;

  // The partition to which this fragment belongs.
  DisjointSet<PartitionInfo *> partition;

  // The "flag zone" to which this fragment belongs.
  DisjointSet<FlagZone *> flag_zone;

  // Tracks flag use within this fragment.
  FlagUsageInfo app_flags;
  FlagUsageInfo inst_flags;

  // Temporary, pass-specific data.
  TempData temp;

  // Tracks register usage across fragments.
  RegisterUsageInfo regs;

  // Tracks the successor fragments.
  Fragment *successors[2];
  NativeInstruction *branch_instr;

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

namespace os {

// Log a list of fragments as a DOT digraph.
void Log(LogLevel level, FragmentList *frags);
}  // namespace os

// Free all fragments, their instructions, etc.
void FreeFragments(FragmentList *frags);

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

  // Registers used anywhere within this flag zone.
  UsedRegisterSet used_regs;

  // Live registers on exit from this flags zone.
  LiveRegisterSet live_regs;

  GRANARY_DEFINE_NEW_ALLOCATOR(FlagZone, {
    SHARED = false,
    ALIGNMENT = 1
  })

 private:
  FlagZone(void) = delete;
};

enum StackStatus {
  kStackStatusUnknown,
  kStackStatusValid,
  kStackStatusInvalid
};

enum StackStatusInheritanceConstraint {
  STACK_STATUS_DONT_INHERIT = 0,  // Don't inherit.

  // Only inherit the status from successor fragments.
  STACK_STATUS_INHERIT_SUCC = (1 << 0),

  // Only inherit the status from predecessor fragments.
  STACK_STATUS_INHERIT_PRED = (1 << 1),

  // Inherit from either the successors or predecessors.
  STACK_STATUS_INHERIT_UNI  = (1 << 0) | (1 << 1)
};

// Tracks stack usage info.
struct StackUsageInfo {
  inline StackUsageInfo(void)
      : status(kStackStatusUnknown),
        inherit_constraint(STACK_STATUS_INHERIT_UNI) {}

  inline explicit StackUsageInfo(StackStatus status_)
      : status(status_),
        inherit_constraint(STACK_STATUS_DONT_INHERIT) {
    GRANARY_ASSERT(kStackStatusUnknown != status_);
  }

  inline explicit StackUsageInfo(
      StackStatusInheritanceConstraint inherit_constraint_)
      : status(kStackStatusUnknown),
        inherit_constraint(inherit_constraint_) {
    GRANARY_ASSERT(STACK_STATUS_DONT_INHERIT != inherit_constraint_);
  }

  // Tells us whether or not the stack pointer in this block appears to
  // reference a valid thread (user or kernel space) stack.
  StackStatus status;

  // Should forward propagation of stack validity be disallowed into this
  // block?
  StackStatusInheritanceConstraint inherit_constraint;
};

// Attributes about a block of code.
class alignas(alignof(void *)) CodeAttributes {
 public:
  CodeAttributes(void);

  // The meta-data associated with the basic block that this code fragment
  // originates from.
  BlockMetaData *block_meta;

  // Does this fragment branch to direct edge code, native code, or an
  // existing basic block?
  bool branches_to_code:1;

  // Does this fragment use an indirect branch?
  bool branch_is_indirect:1;

  // Is the branch instruction a function call or a jump (direct or indirect)?
  bool branch_is_function_call:1;
  bool branch_is_jump:1;

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

  // Does this fragment represent the target of a return from a function call
  // or interrupt call?
  bool is_return_target:1;

  // Is this a "compensating" fragment. This is used during register allocation
  // when we have a case like: P -> S1, P -> S2, and the register R is live from
  // P -> S1 but dead from P -> S2. In this case, we add a compensating
  // fragment P -> C -> S2, wherein we treat R as list on entry to C and
  // explicit "kill" it in C with an annotation instruction.
  bool is_compensation_code:1;

  // Is this fragment some in-edge code? If so, that means that it will indirect
  // jump to some *incomplete* out-edge code, i.e. it will jump to a flag exit/
  // partition exit fragment. This tail will NOT be emitted along with the
  // rest of code, but will be emitted to a special cache.
  bool is_in_edge_code:1;

  // Does this fragment follow (via straight-line execution, e.g. through
  // fall-throughs) a `ControlFlowInstruction`?
  bool follows_cfi:1;

  // Is there an instruction in this fragment with an OS-specific annotation?
  bool has_os_annotation:1;

  // Count of the number of predecessors of this fragment (at fragment build
  // time).
  uint8_t num_predecessors;

} __attribute__((packed));

// Mapping of virtual registers to `SSARegisterWeb`s.
//
// TODO(pag): Need a better data structure.
typedef TinyMap<VirtualRegister, SSARegisterWeb *,
                arch::NUM_GENERAL_PURPOSE_REGISTERS + 7> SSARegisterWebMap;

// Using a vector is deliberate so that the *first* added entries relate to
// later definitions in a fragment.
typedef TinyVector<SSARegisterWeb *, arch::NUM_GENERAL_PURPOSE_REGISTERS>
        SSARegisterWebList;

// Set of spill slots.
typedef BitSet<arch::MAX_NUM_SPILL_SLOTS> SpillSlotSet;

// A fragment with associated SSA vars.
class SSAFragment : public Fragment {
 public:
  SSAFragment(void);
  virtual ~SSAFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, SSAFragment)

  struct SSAInfo {
    inline SSAInfo(void)
        : entry_reg_webs(),
          exit_reg_webs(),
          internal_reg_webs() {}

    SSARegisterWebMap entry_reg_webs;
    SSARegisterWebMap exit_reg_webs;

    // Webs for definitions are in reverse order of the instructions in a
    // fragment (last def to first def).
    SSARegisterWebList internal_reg_webs;
  } ssa;
};

// A fragment of native or instrumentation instructions.
class CodeFragment : public SSAFragment {
 public:
  CodeFragment(void);
  virtual ~CodeFragment(void);

  // Attributes relates to the code in this fragment.
  CodeAttributes attr;

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
  FRAG_EXIT_FUTURE_BLOCK_DIRECT,
  FRAG_EXIT_FUTURE_BLOCK_INDIRECT,
  FRAG_EXIT_EXISTING_BLOCK
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
  explicit ExitFragment(ExitFragmentKind kind_)
      : Fragment(),
        kind(kind_),
        block_meta(nullptr),
        edge() {}

  virtual ~ExitFragment(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Fragment, ExitFragment)
  GRANARY_DEFINE_NEW_ALLOCATOR(ExitFragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

  ExitFragmentKind kind;

  // Meta-data associated with the block targeted by this exit.
  BlockMetaData *block_meta;

  // Pointer to one of the edge structures associated with this fragment.
  EdgeInfo edge;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ExitFragment);
};

}  // namespace granary

#endif  // GRANARY_CODE_FRAGMENT_H_
