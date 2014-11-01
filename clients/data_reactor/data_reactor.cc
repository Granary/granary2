/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"

#include <granary.h>

#ifdef GRANARY_WHERE_user
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"

#include "clients/user/syscall.h"
#include "clients/wrap_func/wrap_func.h"

#include "generated/clients/data_reactor/offsets.h"

using namespace granary;

GRANARY_DEFINE_positive_int(shadow_granularity, 4096,
    "The granularity (in bytes) of shadow memory. This must be a power of two. "
    "The default value is `4096`.",

    "data_collider");

namespace {

enum : uint64_t {
  ADDRESS_SPACE_SIZE = 1ULL << 47U
};

// Amount by which addresses should be shifted.
static uint64_t shift_amount_long = 0UL;
static uint8_t shift_amount = 0;

// Size (in bytes) of the shadow memory.
static auto shadow_mem_size = 0UL;

// Base and limit of shadow memory.
static void *begin_shadow_memory = nullptr;
static void *end_shadow_memory = nullptr;

// Tells us if we came across a `clone` system call.
static __thread bool is_clone = false;

// Find `clone` system calls, which are used for spawning threads.
static void FindClone(void *, SystemCallContext context) {
  is_clone = __NR_clone == context.Number();
}

// After a `clone` system call, set the `GS` segment base to point to shadow
// memory.
//
// There's a bit of duplication here in that we'll set the `GS` base on both
// the new thread and the old thread, but that doesn't matter.
static void SetupShadowSegment(void *, SystemCallContext) {
  if (!is_clone) return;
  GRANARY_IF_DEBUG( auto ret = ) arch_prctl(ARCH_SET_GS, begin_shadow_memory);
  GRANARY_ASSERT(!ret);
  is_clone = false;
}

static void InitShadowMemory(void) {
  shift_amount_long = static_cast<uint64_t>(__builtin_ctzl(
      static_cast<uint64_t>(FLAG_shadow_granularity)));

  shift_amount = static_cast<uint8_t>(shift_amount_long);
  shadow_mem_size = ADDRESS_SPACE_SIZE >> shift_amount_long;
  shadow_mem_size = GRANARY_ALIGN_TO(shadow_mem_size, arch::PAGE_SIZE_BYTES);


  // Allocate the shadow memory space. To reduce the scope of what we actually
  // want to sample, we'll lazily map the shadow memory on the first fault, and
  // record the mapped shadow memory in a simple data structure that GDB can
  // then inspect to choose taint targets.
  begin_shadow_memory = mmap(nullptr, shadow_mem_size,
                             PROT_READ | PROT_WRITE,  // Fault on first access.
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                             -1, 0);

  if (MAP_FAILED == begin_shadow_memory) {
    os::Log(os::LogDebug, "Failed to map shadow memory. Exiting.\n");
    exit(EXIT_FAILURE);
  }

  end_shadow_memory = reinterpret_cast<char *>(begin_shadow_memory) +
                      shadow_mem_size;

  // Make it so that the `GS` segment points to our shadow memory.
  GRANARY_IF_DEBUG( auto ret = ) arch_prctl(ARCH_SET_GS, begin_shadow_memory);
  GRANARY_ASSERT(!ret);

  // Interpose on clone system calls so that we can setup the shadow memory.
  AddSystemCallEntryFunction(FindClone);
  AddSystemCallExitFunction(SetupShadowSegment);
}

// Wrap `malloc` so that we can associate "types" with shadow memory. The idea
// here is that we want to apply watchpoints uniformly across the heap. This
// is challenging in practice because what we really mean is that we want to
// apply them uniformly across objects, with an understand of object types.
// Therefore, if 90% of all heap memory has one type, we will still apply
// watchpoints evenly across those objects that belong to the 90%, as well as
// those that don't, and not just accidentally add 90% of all watchpoints to
// the 90% of objects.
WRAP_INSTRUMENTED_FUNCTION("libc", malloc, (void *), (size_t num_bytes)) {
  auto malloc = WRAPPED_FUNCTION;
  granary_curiosity();
  return malloc(num_bytes);
}

}  // namespace

// Simple tool for static and dynamic basic block counting.
class DataReactor : public InstrumentationTool {
 public:
  virtual void Init(InitReason) {
    InitShadowMemory();
    RegisterFunctionWrapper(&WRAP_FUNC_malloc);
  }

  virtual ~DataReactor(void) = default;

  // Implements the actual touching (reading or writing) of shadow memory.
  void TouchShadow(NativeInstruction *instr, const MemoryOperand &mloc,
                   lir::InlineAssembly &asm_) {
    if (mloc.IsReadWrite()) {
      asm_.InlineBefore(instr, "AND m8 GS:[%2], i8 0;");
    } else if (mloc.IsWrite()) {
      asm_.InlineBefore(instr, "MOV m8 GS:[%2], i8 0;");
    } else {
      asm_.InlineBefore(instr, "TEST m8 GS:[%2], i8 0;");
    }
  }

  // Instrument a memory operand that accesses some absolute memory address.
  void InstrumentAddrMemOp(NativeInstruction *instr, const MemoryOperand &mloc,
                           const void *addr) {
    auto ptr = reinterpret_cast<uintptr_t>(addr);
    ImmediateOperand shadow_offset((ptr >> shift_amount_long) & 0xFFFFFFFFUL);
    lir::InlineAssembly asm_(shadow_offset);
    asm_.InlineBefore(instr, "MOV r64 %2, i64 %0;");
    TouchShadow(instr, mloc, asm_);
  }

  // Instrument a memory operand that accesses some memory address through a
  // register.
  void InstrumentRegMemOp(NativeInstruction *instr, const MemoryOperand &mloc,
                          VirtualRegister addr) {
    RegisterOperand reg(addr);
    ImmediateOperand shift(shift_amount);
    lir::InlineAssembly asm_(reg, shift);
    asm_.InlineBefore(instr, "MOV r64 %2, r64 %0;"
                             "SHR r64 %2, i8 %1;");
    TouchShadow(instr, mloc, asm_);
  }

  void InstrumentMemOp(NativeInstruction *instr, const MemoryOperand &mloc) {
    // Doesn't read from or write to memory.
    if (mloc.IsEffectiveAddress()) return;

    // Reads or writes from an absolute address, not through a register.
    VirtualRegister addr_reg;
    const void *addr_ptr(nullptr);

    if (mloc.MatchRegister(addr_reg)) {

      // Ignore addresses stored in non-GPRs (e.g. accesses to the stack).
      if (!addr_reg.IsGeneralPurpose()) return;
      if (addr_reg.IsVirtualStackPointer()) return;
      if (addr_reg.IsSegmentOffset()) return;

      InstrumentRegMemOp(instr, mloc, addr_reg);

    } else if (mloc.MatchPointer(addr_ptr)) {
      InstrumentAddrMemOp(instr, mloc, addr_ptr);

    } else if (mloc.IsCompound()) {
      // TODO(pag): Implement me!
    }
  }

  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    MemoryOperand mloc1, mloc2;
    for (auto instr : bb->AppInstructions()) {
      auto num_matched = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                     ReadOrWriteTo(mloc2));
      if (2 == num_matched) {
        InstrumentMemOp(instr, mloc1);
        InstrumentMemOp(instr, mloc2);
      } else if (1 == num_matched) {
        InstrumentMemOp(instr, mloc1);
      }
    }
  }
};

// Initialize the `data_reactor` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<DataReactor>("data_reactor",
                                           {"gdb", "wrap_func"});
})

#pragma clang diagnostic pop
#endif  // GRANARY_WHERE_user
