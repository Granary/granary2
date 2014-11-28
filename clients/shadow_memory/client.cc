/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#include "clients/shadow_memory/shadow_memory.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_positive_int(shadow_granularity, 4096,
    "The granularity (in bytes) of shadow memory. This must be a power of two. "
    "The default value is `4096`, which means: 1 page of physical memory "
    "maps to one unit of shadow memory.",

    "direct_shadow_memory");

namespace {

enum : uint64_t {
  // TODO(pag): For kernel space, this really needs to be adjusted. While this
  //            is indeed the size of the address space, the usable size will
  //            ideally be much smaller (on the order of a few hundred
  //            megabytes, and probably only going into the gigabyte range if
  //            the buffer cache is heavily used).
  kAddressSpaceSize = 1ULL << 47U,
};

typedef LinkedListIterator<ShadowStructureDescription> ShadowStructureIterator;

// Descriptions of the meta-data structures.
static ShadowStructureDescription *gDescriptions = nullptr;
static ShadowStructureDescription **gNextDescription = &gDescriptions;
static size_t gUnalignedSize = 0;
static size_t gAlignedSize = 1;
static size_t gScaleAmountLong = 0;
static uint8_t gScaleAmount = 0;

// Defines the granularity of shadow memory in terms of a shift.
static size_t gShiftAmountLong = 0;
static uint8_t gShiftAmount = 0;

// Total size of shadow memory.
static size_t gShadowMemNumPages = 0;
static size_t gShadowMemSize = 0;

// Pointer to shadow memory.
static char *gShadowMem = nullptr;
static SpinLock gShadowMemLock GRANARY_GLOBAL;

}  // namespace

// Simple tool for direct-mapped shadow memory.
class DirectMappedShadowMemory : public InstrumentationTool {
 public:
  virtual ~DirectMappedShadowMemory(void) = default;

  // Initialize the few things that we can. We can't initialize the shadow
  // memory up-front because dependent tools won't yet be initialized, and
  // therefore won't have added their shadow structure descriptions yet. We
  // need those shadow structure descriptions to determine the size of shadow
  // memory.
  virtual void Init(InitReason) {
    gShiftAmountLong = static_cast<size_t>(
        __builtin_ctz(FLAG_shadow_granularity));
    gShiftAmount = static_cast<uint8_t>(gShiftAmountLong);
    GRANARY_ASSERT(0 != gShiftAmount);
  }

  // Reset all globals to their initial state.
  virtual void Exit(ExitReason) {
    while (gDescriptions) {
      auto desc = gDescriptions;
      gDescriptions = desc->next;
      desc->next = nullptr;
      desc->instrumenter = nullptr;
    }
    gNextDescription = &gDescriptions;
    gUnalignedSize = 0;
    gAlignedSize = 1;
    gScaleAmountLong = 0;
    gScaleAmount = 0;
    gShiftAmountLong = 0;
    gShiftAmount = 0;
    ExitShadowMemory();
    gShadowMemSize = 0;
    gShadowMemNumPages = 0;
    gShadowMem = nullptr;
  }

  // Instrument all of the instructions in a basic block.
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    if (GRANARY_UNLIKELY(!gShadowMemSize)) return;
    if (GRANARY_UNLIKELY(!gDescriptions)) return;
    if (GRANARY_UNLIKELY(!gShadowMem)) InitShadowMemory();
    MemoryOperand mloc1, mloc2;
    for (auto instr : bb->AppInstructions()) {
      auto num_matched = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                     ReadOrWriteTo(mloc2));
      if (2 == num_matched) {
        InstrumentMemOp(bb, instr, mloc1);
        InstrumentMemOp(bb, instr, mloc2);
      } else if (1 == num_matched) {
        InstrumentMemOp(bb, instr, mloc1);
      }
    }
  }

 private:
  // Instrument a memory operation.
  static void InstrumentMemOp(DecodedBasicBlock *bb, NativeInstruction *instr,
                              const MemoryOperand &mloc) {
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

      InstrumentRegMemOp(bb, instr, mloc, addr_reg);

    } else if (mloc.MatchPointer(addr_ptr)) {
      InstrumentAddrMemOp(bb, instr, mloc, addr_ptr);

    } else if (mloc.IsCompound()) {
      InstrumentCompoundMemOp(bb, instr, mloc);
    }
  }

  // Instrument a memory operand that accesses some absolute memory address.
  static void InstrumentAddrMemOp(DecodedBasicBlock *bb,
                                  NativeInstruction *instr,
                                  const MemoryOperand &mloc,
                                  const void *addr) {
    auto ptr = reinterpret_cast<uintptr_t>(addr);
    // Ignore stuff in the vdso / vsyscall pages.
    GRANARY_IF_USER( if (GRANARY_UNLIKELY(ptr >= 0xFFFFFFFFFFFFULL)) return; )

    ImmediateOperand shift(gShiftAmount);
    ImmediateOperand scale(gScaleAmount);
    ImmediateOperand native_addr(ptr);
    lir::InlineAssembly asm_(shift, scale, native_addr);
    asm_.InlineBefore(instr, "MOV r64 %3, i64 %2;"_x86_64);
    InstrumentMemOp(bb, instr, mloc, asm_);
  }

  // Instrument a memory operand that accesses some memory address through a
  // register.
  static void InstrumentRegMemOp(DecodedBasicBlock *bb,
                                 NativeInstruction *instr,
                                 const MemoryOperand &mloc,
                                 VirtualRegister addr) {
    RegisterOperand reg(addr);
    ImmediateOperand shift(gShiftAmount);
    ImmediateOperand scale(gScaleAmount);
    lir::InlineAssembly asm_(shift, scale, reg, reg);
    InstrumentMemOp(bb, instr, mloc, asm_);
  }

  static void InstrumentCompoundMemOp(DecodedBasicBlock *bb,
                                      NativeInstruction *instr,
                                      const MemoryOperand &mloc) {
    ImmediateOperand shift(gShiftAmount);
    ImmediateOperand scale(gScaleAmount);
    lir::InlineAssembly asm_(shift, scale, mloc);
    asm_.InlineBefore(instr, "LEA r64 %3, m64 %2;"_x86_64);
    InstrumentMemOp(bb, instr, mloc, asm_);
  }

  // Instrument a memory operand that accesses some memory address through a
  // register.
  //
  // For `asm_`:
  //      %0 is an i8 shift amount.
  //      %1 is an i8 scale amount.
  //      %3 is an r64 native pointer.
  static void InstrumentMemOp(DecodedBasicBlock *bb,
                              NativeInstruction *instr,
                              const MemoryOperand &mloc,
                              lir::InlineAssembly &asm_) {
    asm_.InlineBefore(instr, "MOV r64 %4, r64 %3;"_x86_64);
    asm_.InlineBeforeIf(instr, 0 < gShiftAmount, "SHR r64 %4, i8 %0;"_x86_64);
    asm_.InlineBeforeIf(instr, 1 < gAlignedSize, "SHL r64 %4, i8 %1;"_x86_64);

    auto &native_addr_op(asm_.Register(bb, 3));
    auto &shadow_addr_op(asm_.Register(bb, 4));

    auto old_offset = 0;
    char adjust_shadow_offset[32];
    for (auto desc : ShadowStructureIterator(gDescriptions)) {

      // Move `%4` (the offset/pointer) to point to this description's
      // structure.
      if (auto offset_diff = desc->offset - old_offset) {
        Format(adjust_shadow_offset, "ADD r64 %%4, i8 %lu;", offset_diff);
        asm_.InlineBefore(instr, adjust_shadow_offset);
      }
      DirectShadowedOperand op(bb, instr, mloc, shadow_addr_op, native_addr_op);
      desc->instrumenter(&op);
    }
  }

  // Initialize the shadow memory if it has not yet been initialized.
  static void InitShadowMemory(void) {
    SpinLockedRegion locker(&gShadowMemLock);
    if (gShadowMem) return;  // Double-checked locking ;-)

#ifdef GRANARY_WHERE_user
    // Note: We don't use `os::AllocateDataPages` in user space because
    //       we want these page to be lazily mapped.
    gShadowMem = mmap(nullptr, gShadowMemSize,
                      PROT_READ | PROT_WRITE,  // Fault on first access.
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                      -1, 0);
#else
    gShadowMem = os::AllocateDataPages(gShadowMemNumPages);
#endif
  }

  static void ExitShadowMemory(void) {
    if (gShadowMem) {
      GRANARY_IF_USER_ELSE(munmap(gShadowMem, gShadowMemSize),
                           os::FreeDataPages(gShadowMemNumPages));
    }
  }
};

// Tells the shadow memory tool about a structure to be stored in shadow
// memory.
void AddShadowStructure(ShadowStructureDescription *desc,
                        void (*instrumenter)(DirectShadowedOperand *)) {
  GRANARY_ASSERT(!gShadowMem);
  GRANARY_ASSERT(!desc->next);
  GRANARY_ASSERT(!desc->instrumenter);

  desc->instrumenter = instrumenter;

  *gNextDescription = desc;
  gNextDescription = &(desc->next);

  // Update the descriptions to more accurately represent the shadow unit
  // size.
  gUnalignedSize += GRANARY_ALIGN_FACTOR(gUnalignedSize, desc->align);
  desc->offset = gUnalignedSize;
  gUnalignedSize += desc->size;

  gScaleAmountLong = static_cast<size_t>(
      32 - __builtin_clz(static_cast<int32_t>(gUnalignedSize)) - 1);

  // Adjust the aligned size of the shadow unit based on our newly added
  // description.
  gAlignedSize = 1UL << gScaleAmountLong;
  if (gUnalignedSize > gAlignedSize) {
    gScaleAmountLong -= 1;
    gAlignedSize = 1UL << gScaleAmountLong;
    GRANARY_ASSERT(gAlignedSize >= gUnalignedSize);
  }
  gScaleAmount = static_cast<uint8_t>(gScaleAmountLong);
  GRANARY_ASSERT(0 != gScaleAmount);

  // Scale the size of shadow memory based on the new shadow unit size.
  gShadowMemSize = kAddressSpaceSize >> gShiftAmountLong;
  gShadowMemSize *= gAlignedSize;
  gShadowMemSize = GRANARY_ALIGN_TO(gShadowMemSize, arch::PAGE_SIZE_BYTES);
  gShadowMemNumPages = gShadowMemSize / arch::PAGE_SIZE_BYTES;
}

// Add the `direct_shadow_memory` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<DirectMappedShadowMemory>("direct_shadow_memory");
}
