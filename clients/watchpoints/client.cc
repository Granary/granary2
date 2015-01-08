/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#include "clients/util/closure.h"
#include "clients/memop/client.h"
#include "clients/watchpoints/client.h"

GRANARY_USING_NAMESPACE granary;

extern void InitUserWatchpoints(void);

namespace {

// Hooks that other tools can use for interposing on memory operands that will
// be instrumented for watchpoints.
static ClosureList<const WatchedMemoryOperand &>
    gWatchpointHooks GRANARY_GLOBAL;

}  // namespace

// Registers a function that can hook into the watchpoints system to instrument
// code.
void AddWatchpointInstrumenter(void (*func)(const WatchedMemoryOperand &)) {
  gWatchpointHooks.Add(func);
}

// Implements the instrumentation needed to do address watchpoints.
//
// Address watchpoints is a mechanism that enables selective memory shadowing
// by tainting memory addresses. The 48th bit of an address distinguishes
// "watched" (i.e. tainted) addresses from "unwatched" addresses. The
// watchpoints instrumentation injects instructions to detect dereferences of
// tainted addresses and ensures that memory instructions don't raise faults
// when they are accessed.
class Watchpoints : public InstrumentationTool {
 public:
  virtual ~Watchpoints(void) = default;

  static void Init(InitReason reason) {
    if (kInitProgram == reason || kInitAttach == reason) {
      InitUserWatchpoints();
      AddMemOpInstrumenter(InstrumentMemOp);

      unwatched_addr[0] = AllocateVirtualRegister();
      unwatched_addr[1] = AllocateVirtualRegister();
    }
  }

  static void Exit(ExitReason reason) {
    if (kExitDetach == reason) {
      gWatchpointHooks.Reset();
    }
  }

 private:

  // Instrument an individual memory operand.
  static void InstrumentMemOp(const InstrumentedMemoryOperand &op) {
    // Ignore addresses stored in non-GPRs (e.g. accesses to the stack).
    auto watched_addr = op.native_addr_op.Register();
    if (watched_addr.IsStackPointerAlias()) return;

    RegisterOperand unwatched_addr_reg(unwatched_addr[op.operand_number]);
    RegisterOperand watched_addr_reg(op.native_addr_op);
    WatchedMemoryOperand client_op = {op.block, op.instr, op.native_mem_op,
                                      unwatched_addr_reg, watched_addr_reg};

    lir::InlineAssembly asm_(unwatched_addr_reg, watched_addr_reg);

    // Copy the original (%1).
    asm_.InlineBefore(op.instr,
        "MOV r64 %0, r64 %1;"_x86_64);

    // Might be accessing user space data.
    asm_.InlineBeforeIf(op.instr,
                        IsA<ExceptionalControlFlowInstruction *>(op.instr),
        "BT r64 %0, i8 47;"
        "JNB l %2;"_x86_64);

    asm_.InlineBefore(op.instr,
        "BT r64 %0, i8 48;"  // Test the discriminating bit (bit 48).
        GRANARY_IF_USER_ELSE("JNB", "JB") " l %2;"
        "  @COLD;"
        "  SHL r64 %0, i8 16;"
        "  SAR r64 %0, i8 16;"_x86_64);

    // Allow all hooked tools to see the watched (%1) and unwatched (%0)
    // address.
    gWatchpointHooks.ApplyAll(client_op);

    asm_.InlineBefore(op.instr,
        "@LABEL %2:"_x86_64);

    // If it's an implicit memory location then we need to change the register
    // being used by the instruction in place, while keeping a copy around
    // for later.
    asm_.InlineBeforeIf(op.instr, !op.native_mem_op.IsModifiable(),
        "XCHG r64 %0, r64 %1;"_x86_64);

    // Replace the original memory operand with the unwatched address.
    if (op.native_mem_op.IsModifiable()) {
      MemoryOperand unwatched_addr_mloc(unwatched_addr[op.operand_number],
                                        op.native_mem_op.ByteWidth());
      GRANARY_IF_DEBUG( auto ret = ) op.native_mem_op.TryReplaceWith(
          unwatched_addr_mloc);
      GRANARY_ASSERT(ret);

    // Restore the tainted bits if the memory operand was implicit, and if the
    // watched address was not overwritten by the instruction.
    } else if (!op.instr->MatchOperands(ExactWriteOnlyTo(watched_addr_reg))) {
      GRANARY_ASSERT(watched_addr.IsNative());
      asm_.InlineAfter(op.instr,
          "BSWAP r64 %1;"  // Swap bytes in unwatched address.
          "BSWAP r64 %0;"  // Swap bytes in watched address.
          "MOV r16 %1, r16 %0;"
          "BSWAP r64 %1;"_x86_64);
    }
  }

  static VirtualRegister unwatched_addr[2];
};

VirtualRegister Watchpoints::unwatched_addr[2];

namespace {
enum : uintptr_t {
  TAINT_BIT = GRANARY_IF_USER_ELSE(1ul, 0ul),
  TAINT_MASK = 0xFFFEull
};
}  // namespace

// Taints an address `addr` using the low 15 bits of the taint index `index`.
uintptr_t TaintAddress(uintptr_t addr, uintptr_t index) {
  if (!addr) return addr;
  auto taint = (((index << 1U) & TAINT_MASK) | TAINT_BIT) << 48UL;
  return ((addr << 16) >> 16) | taint;
}

// Untaints an address `addr`.
uintptr_t UntaintAddress(uintptr_t addr) {
  if (!(1UL & (addr >> 47))) {  // User space address.
    return addr & 0xFFFFFFFFFFFFULL;
  } else {
    return addr | (0xFFFFULL << 48);
  }
}

// Returns true if an address is tainted.
bool IsTaintedAddress(uintptr_t addr) {
  auto bit_47 = (addr >> 47U) & 1U;
  auto bit_48 = (addr >> 48U) & 1U;
  return bit_47 != bit_48;
}

// Returns the taint for an address.
uint16_t ExtractTaint(uintptr_t addr) {
  return static_cast<uint16_t>(addr >> 49);
}

// Initialize the `watchpoints` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<Watchpoints>("watchpoints", {"memop"});
}
