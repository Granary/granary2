/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/builder.h"

#include "granary/base/option.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"

#include "granary/code/edge.h"
#include "granary/code/fragment.h"

// Append a non-native, created instruction to the fragment.
#define APP(...) \
  do { \
    __VA_ARGS__; \
    ni.is_sticky = true; \
    frag->instrs.Append(new NativeInstruction(&ni)); \
  } while (0)

GRANARY_DECLARE_bool(profile_direct_edges);

namespace granary {
namespace arch {
// Table of all implicit operands.
extern const Operand * const IMPLICIT_OPERANDS[];

// Number of implicit operands for each iclass.
extern const int NUM_IMPLICIT_OPERANDS[];

}  // namespace arch

namespace {

bool HintFragment(const arch::Operand &op) {
  if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
    if (op.is_compound) {
      return XED_REG_RAX == op.mem.reg_base ||
             XED_REG_RAX == op.mem.reg_index;
    }
  } else if (XED_ENCODER_OPERAND_TYPE_REG != op.type) {
    return false;
  }
  if (op.reg.IsGeneralPurpose()) {
    auto reg = op.reg;
    reg.Widen(arch::GPR_WIDTH_BYTES);
    return XED_REG_RAX == reg.EncodeToNative();
  }
  return false;
}

}  // namespace

// Try to add a flag split hint to a code fragment.
void TryAddFlagSplitHint(CodeFragment *frag, const NativeInstruction *instr) {
  auto &ainstr(instr->instruction);
  for (auto &op : ainstr.ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    }
    if (HintFragment(op)) {
      frag->attr.has_flag_split_hint = true;
      return;
    }
  }
  auto implicit_ops = arch::IMPLICIT_OPERANDS[ainstr.iclass];
  for (auto i = 0; i < arch::NUM_IMPLICIT_OPERANDS[ainstr.iclass]; ++i) {
    if (HintFragment(implicit_ops[i])) {
      frag->attr.has_flag_split_hint = true;
      return;
    }
  }
}

// Returns true if this instruction can change the interrupt enabled state on
// this CPU.
bool ChangesInterruptDeliveryState(const NativeInstruction *instr) {
  auto iclass = instr->instruction.iclass;

  // Note: We ignore `POPF/Q` because it will mark the stack as valid, and
  //       therefore virtual register allocation around a `POPF/Q` will use
  //       stack allocation, and not use something like per-CPU or per-thread
  //       data.
  return XED_ICLASS_STI == iclass || XED_ICLASS_CLI == iclass;
}


namespace {

enum {
  EDGE_SLOT_OFFSET = 0,
  EDGE_SLOT_ENTRY_TARGET = 1,
  EDGE_SLOT_ARG1 = 2,
  EDGE_SLOT_STACK_PTR = 3,
  EDGE_SLOT_LAST
};

struct EdgeSlotSet {
  intptr_t slots[EDGE_SLOT_LAST];
};

#if defined(GRANARY_WHERE_kernel) || !defined(GRANARY_WHERE_user)
# error "TODO(pag): Implement `EdgeSlot` and `EdgeSlotOffset` for kernel space."
#else

extern "C" {

// Get the base address of the current thread's TLS. We use this address to
// compute `FS`-based offsets from the TLS base. We assume that the base address
// returned by this function is the address associated with `FS:0`.
extern intptr_t granary_arch_get_segment_base(void);

// The direct edge entrypoint code.
extern void granary_arch_enter_direct_edge(void);
extern void granary_arch_enter_direct_edge_profiled(void);

}  // extern C

// Per-thread edge slots.
//
// Note: This depends on a load-time TLS implementation, as is the case on
//       systems like Linux.
static __thread __attribute__((tls_model("initial-exec"))) EdgeSlotSet EDGE;

#endif  // User-space implementation of edge spill slots.

// Returns the offset of one of the edge slots.
static intptr_t EdgeSlotOffset(int slot) {
  auto this_slot = &(EDGE.slots[slot]);
  auto this_slot_addr = reinterpret_cast<intptr_t>(this_slot);
  return this_slot_addr - granary_arch_get_segment_base();
}

// Allows us to easily access an edge slot.
static arch::Operand EdgeSlot(int slot) {
  arch::Operand op;

  op.type = XED_ENCODER_OPERAND_TYPE_PTR;
  op.segment = GRANARY_IF_USER_ELSE(XED_REG_FS, XED_REG_GS);  // Linux-specific.
  op.is_compound = true;
  op.addr.as_int = EdgeSlotOffset(slot);
  op.width = arch::GPR_WIDTH_BITS;
  return op;
}

}  // namespace

// Generates some edge code for a direct control-flow transfer between two
// basic block.
void GenerateDirectEdgeCode(DirectBasicBlock *block,
                            BlockMetaData *source_block_meta,
                            BlockMetaData *dest_block_meta,
                            CodeFragment *frag) {
  arch::Instruction ni;
  auto cfg = block->cfg;
  auto edge = cfg->AllocateDirectEdge(source_block_meta, dest_block_meta);
  frag->edge.direct = edge;

  // Ensure that the entry slot is initialized. This will allows us to later
  // restore from `FS` in a generic way using:
  //      mov   %fs:(%rsi)  --> %rdi
  //      xchg  %fs:(%rsi)  <-> %rsp
  //      xchg  %fs:(%rsi)  <-> %rsi
  if (!EDGE.slots[EDGE_SLOT_ENTRY_TARGET]) {
    EDGE.slots[EDGE_SLOT_OFFSET] = EdgeSlotOffset(EDGE_SLOT_OFFSET);
    EDGE.slots[EDGE_SLOT_ENTRY_TARGET] = UnsafeCast<intptr_t>(
        FLAG_profile_direct_edges ? granary_arch_enter_direct_edge_profiled
                                  : granary_arch_enter_direct_edge);
  }

  APP(XCHG_MEMv_GPRv(&ni, EdgeSlot(EDGE_SLOT_STACK_PTR), XED_REG_RSP));
  APP(MOV_MEMv_GPRv(&ni, EdgeSlot(EDGE_SLOT_ARG1), XED_REG_RDI));
  APP(MOV_GPRv_IMMz(&ni, XED_REG_RDI, reinterpret_cast<uintptr_t>(edge));
      if (16 >= ni.ops[1].width) { ni.ops[1].width = 32; }
      if (32 == ni.ops[1].width) {
        ni.ops[0].width = 32;
        ni.ops[0].reg.DecodeFromNative(XED_REG_EDI);
      } );
  APP(XCHG_MEMv_GPRv(&ni, EdgeSlot(EDGE_SLOT_OFFSET), XED_REG_RSI));
  APP(CALL_NEAR_MEMv(&ni, EdgeSlot(EDGE_SLOT_ENTRY_TARGET)));
  APP(XCHG_MEMv_GPRv(&ni, EdgeSlot(EDGE_SLOT_STACK_PTR), XED_REG_RSP));
  APP(JMP_MEMv(&ni, &(edge->cached_target)));
}

}  // namespace granary
