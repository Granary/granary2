/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_FRAGMENT_H_
#define GRANARY_CODE_ASSEMBLE_FRAGMENT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"

#include "granary/base/bitset.h"
#include "granary/base/list.h"
#include "granary/base/new.h"

#include "granary/code/register.h"

namespace granary {

// Forward declarations.
class Instruction;
class LabelInstruction;
class NativeInstruction;
class DecodedBasicBlock;
class LocalControlFlowGraph;
class BlockMetaData;
class FragmentBuilder;
class SSAVariableTracker;

// Defines the different categories of fragments.
enum FragmentKind : uint8_t {
  FRAG_KIND_INSTRUMENTATION,
  FRAG_KIND_APPLICATION,
  FRAG_KIND_FLAG_ENTRY,
  FRAG_KIND_FLAG_EXIT,
  FRAG_KIND_PARTITION_ENTRY,
  FRAG_KIND_PARTITION_EXIT
};

enum {
  MAX_NUM_LIVE_VIRTUAL_REGS = 56
};

// Represents a basic block in the true sense. Granary basic blocks can contain
// local control flow, so they need to be split into fragments of instructions
// that more closely represent the actual run-time control flow. This lower
// level model is needed for register allocation, etc.
class Fragment {
 public:
  // Initialize the fragment from a basic block.
  explicit Fragment(int id_);

  // Append an instruction into the fragment.
  void AppendInstruction(std::unique_ptr<Instruction> instr);

  // Remove an instruction.
  std::unique_ptr<Instruction> RemoveInstruction(Instruction *instr);

  // Insert an instruction before another instruction
  Instruction *InsertBefore(Instruction *insert_loc,
                            std::unique_ptr<Instruction> insert_instr);

  // Insert an instruction before another instruction
  Instruction *InsertAfter(Instruction *insert_loc,
                           std::unique_ptr<Instruction> insert_instr);

  GRANARY_DEFINE_NEW_ALLOCATOR(Fragment, {
    SHARED = false,
    ALIGNMENT = 1
  })

  // Next fragment in the fragment list. This is always associated with an
  // implicit control-flow instruction between two fragments.
  Fragment *fall_through_target;

  // Conditional branch target. This always associated with an explicit
  // control-flow instruction between two fragments.
  Fragment *branch_target;

  // All fragments are chained together into a list for simple iteration,
  // freeing, etc.
  Fragment *next;

  // Previous pointers. Chained together during register scheduling.
  Fragment *prev;

  // Allows for all fragments within a partition to point at some single
  // "sentinel" fragment that can be used to coordinate information that is
  // "global" to the partition.
  Fragment *partition_sentinel;

  union {
    // When adding flag entry/exit and partition entry/exit fragments, we often
    // want to avoid adding redundant fragments so we cache a back link to a
    // recently created partition/flags entry/exit fragment, and try to re-use
    // those fragments.
    Fragment *cached_back_link;

    // When doing flag saving and restoring, we designate a single flag entry/
    // exit fragment as tracking the flags that must be saved restored, and
    // ensure that all fragments within a flag region refer to this "sentinel"
    // fragment.
    Fragment *flag_sentinel;

    // The amount of space needed for virtual register allocation.
    struct {
      bool is_closed:1;

      // Number of spill slots. When doing fragment-local scheduling, we use
      // fragment-specific spill slots; however, when doing partition-local
      // scheduling, we use the `num_spill_slots` from a fragment's
      // `partition_sentinel`.
      uint8_t num_spill_slots:7;

      // Mask of which specific spill slots are allocated. This puts an upper-
      // bound of 32 simultaneously live fragment-local or partition-local
      // virtual registers.
      //
      // Note: Fragment-local and partition-local virtual register allocation
      //       is done separately, so in practice there is an upper bound of
      //       64 simultaneously live virtual registers, as the spill slot
      //       allocated mask is zeroed between the two allocation stages.
      uint64_t spill_slot_allocated_mask:MAX_NUM_LIVE_VIRTUAL_REGS;

    } __attribute__((packed));
  };

  // Tracks the general purpose architectural and virtual registers as-if they
  // were SSA variables. This is used for copy propagation and virtual register
  // allocation.
  SSAVariableTracker *ssa_vars;

  union {
    // Unique ID of this fragment. This roughly corresponds to a depth-first
    // order number of the fragment.
    //
    // Note: Used by `FRAG_KIND_INSTRUMENTATION` and `FRAG_KIND_APPLICATION`
    //       fragments.
    int id;

    // Virtual register used by the flag save/restore passes to
    //
    // Note: Used by `FRAG_KIND_FLAG_ENTRY` fragments.
    VirtualRegister flag_save_reg;

    // The current "round" of partition-global register allocation. This is
    // only tracked within a partition sentinel fragment.
    int reg_alloc_round;
  };

  // Identifier of a "stack region". This is a very coarse grained concept,
  // where we color fragments according to:
  //    -N:   The stack pointer doesn't point to a valid stack.
  //    0:    We don't know yet.
  //    N:    The stack pointer points to some valid stack.
  //
  // The numbering partitions fragments into two coarse grained groups:
  // invalid code execution on an unsafe stack (negative id), or code executing
  // on a safe stack (positive id). The numbering sub-divides fragments into
  // finer-grained colors based, where two or more fragments have the same
  // color if they are connected through control flow, and if there are no
  // changes to the stack pointer within the basic blocks.
  int partition_id;

  // Conservative set of flags that are live on entry to this basic block.
  unsigned app_live_flags;

  // Flags that are killed anywhere within a fragment that contains
  // instrumented instructions. If this is an application code fragment (kind =
  // `FRAG_KIND_APPLICATION`) then `killed_flags = 0`.
  unsigned inst_killed_flags;

  // Is this block the first fragment in a decoded basic block?
  bool is_decoded_block_head;

  // Is this a future basic block?
  bool is_future_block_head;

  // Is this an exit block? An exit block is a future block, or a block that
  // ends in some kind of return, or a native block.
  bool is_exit;

  // Does the last instruction in this fragment change the stack pointer? If so,
  // the we consider the stack to be valid in this fragment if the stack pointer
  // is also read during the operation. Otherwise, it's treated as a strict
  // stack switch, where the stack might not be valid.
  bool writes_to_stack_pointer;
  bool reads_from_stack_pointer;

  // Pseudo entry/exit fragment types.
  FragmentKind kind;

  // Source basic block info.
  BlockMetaData *block_meta;

  // Instruction list.
  Instruction *first;
  Instruction *last;

#if 0
  // Which physical registers are conservatively live on entry to and exit from
  // this block.
  LiveRegisterTracker entry_regs_live;
  LiveRegisterTracker exit_regs_live;

  // Which physical registers are conservatively dead on entry to and exit from
  // this block.
  DeadRegisterTracker entry_regs_dead;
  DeadRegisterTracker exit_regs_dead;
#endif

 private:
  Fragment(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Fragment);
} __attribute__((packed));

typedef LinkedListIterator<Fragment> FragmentIterator;
typedef ReverseLinkedListIterator<Fragment> ReverseFragmentIterator;

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_FRAGMENT_H_
