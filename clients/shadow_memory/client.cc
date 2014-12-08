/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#include "clients/shadow_memory/client.h"
#include "clients/util/instrument_memop.h"

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
  kUnscaledShadowMemSize = 1ULL << 32UL
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
class DirectMappedShadowMemory : public MemOpInstrumentationTool {
 public:
  virtual ~DirectMappedShadowMemory(void) = default;

  // Initialize the few things that we can. We can't initialize the shadow
  // memory up-front because dependent tools won't yet be initialized, and
  // therefore won't have added their shadow structure descriptions yet. We
  // need those shadow structure descriptions to determine the size of shadow
  // memory.
  static void Init(InitReason reason) {
    if (kInitThread == reason) return;
    gShiftAmountLong = static_cast<size_t>(
        __builtin_ctz(static_cast<unsigned>(FLAG_shadow_granularity)));
    gShiftAmount = static_cast<uint8_t>(gShiftAmountLong);
    GRANARY_ASSERT(0 != gShiftAmount);
  }

  // Reset all globals to their initial state.
  static void Exit(ExitReason reason) {
    if (kExitThread == reason) return;

    while (gDescriptions) {
      auto desc = gDescriptions;
      gDescriptions = desc->next;
      desc->next = nullptr;
      desc->instrumenter = nullptr;
      desc->is_registered = false;
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
    this->MemOpInstrumentationTool::InstrumentBlock(bb);
  }

 protected:
  virtual void InstrumentMemOp(DecodedBasicBlock *bb,
                               NativeInstruction *instr,
                               MemoryOperand &mloc,
                               const RegisterOperand &addr) {
    if (addr.IsStackPointer() || addr.IsVirtualStackPointer()) return;

    ImmediateOperand shift(gShiftAmount);
    ImmediateOperand scale(gScaleAmount);
    ImmediateOperand shadow_base(gShadowMem);
    lir::InlineAssembly asm_(shift, scale, shadow_base, addr);
    asm_.InlineBefore(instr, "MOV r64 %4, r64 %3;"
                             "MOV r64 %5, i64 %2;"_x86_64);
    // %0 is an i8 shift amount.
    // %1 is an i8 scale amount.
    // %2 is an i64 containing the value of `gShadowMem`.
    // %3 is an r64 native pointer.
    // %4 will be our shadow pointer (calculated based on %3).
    // %5 is our shadow base

    // Scale the native address by the granularity of the shadow memory.
    asm_.InlineBeforeIf(instr, 0 < gShiftAmount, "SHR r64 %4, i8 %0;"_x86_64);

    // Chop off the high-order 32 bits of the shadow offset, then scale the
    // offset by the size of the shadow structure.
    asm_.InlineBefore(instr, "MOV r32 %4, r32 %4;"_x86_64);
    asm_.InlineBeforeIf(instr, 1 < gAlignedSize, "SHL r64 %5, i8 %1;"_x86_64);

    // Add the shadow base to the offset, forming the shadow pointer.
    asm_.InlineBefore(instr, "ADD r64 %4, r64 %5;"_x86_64);
    auto native_addr_op = asm_.Register(bb, 4);
    auto shadow_addr_op = asm_.Register(bb, 6);

    auto old_offset = 0UL;
    char adjust_shadow_offset[32];
    for (auto desc : ShadowStructureIterator(gDescriptions)) {

      // Move `%4` (the offset/pointer) to point to this description's
      // structure.
      if (auto offset_diff = desc->offset - old_offset) {
        Format(adjust_shadow_offset, "ADD r64 %%4, i8 %lu;", offset_diff);
        asm_.InlineBefore(instr, adjust_shadow_offset);
      }

      ShadowedOperand op(bb, instr, mloc, shadow_addr_op, native_addr_op);
      desc->instrumenter(op);
    }
  }

  // Initialize the shadow memory if it has not yet been initialized.
  static void InitShadowMemory(void) {
    SpinLockedRegion locker(&gShadowMemLock);
    if (gShadowMem) return;  // Double-checked locking ;-)

#ifdef GRANARY_WHERE_user
    // Note: We don't use `os::AllocateDataPages` in user space because
    //       we want these page to be lazily mapped.
    gShadowMem = reinterpret_cast<char *>(mmap(
        nullptr, gShadowMemSize, PROT_READ | PROT_WRITE,  // Fault on first access.
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
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
                        void (*instrumenter)(const ShadowedOperand &)) {
  GRANARY_ASSERT(!gShadowMem);
  GRANARY_ASSERT(!desc->next);
  GRANARY_ASSERT(!desc->instrumenter);

  desc->instrumenter = instrumenter;
  desc->is_registered = true;

  *gNextDescription = desc;
  gNextDescription = &(desc->next);

  // Update the descriptions to more accurately represent the shadow unit
  // size.
  gUnalignedSize += GRANARY_ALIGN_FACTOR(gUnalignedSize, desc->align);
  desc->offset = gUnalignedSize;
  gUnalignedSize += desc->size;

  gScaleAmountLong = static_cast<size_t>(
      32 - __builtin_clz(static_cast<uint32_t>(gUnalignedSize)) - 1);

  // Adjust the aligned size of the shadow unit based on our newly added
  // description.
  gAlignedSize = 1UL << gScaleAmountLong;
  if (gUnalignedSize > gAlignedSize) {
    gScaleAmountLong -= 1;
    gAlignedSize = 1UL << gScaleAmountLong;
    GRANARY_ASSERT(gAlignedSize >= gUnalignedSize);
  }
  gScaleAmount = static_cast<uint8_t>(gScaleAmountLong);

  // Scale the size of shadow memory based on the new shadow unit size.
  gShadowMemSize = kUnscaledShadowMemSize * gAlignedSize;
  gShadowMemSize = GRANARY_ALIGN_TO(gShadowMemSize, arch::PAGE_SIZE_BYTES);
  gShadowMemNumPages = gShadowMemSize / arch::PAGE_SIZE_BYTES;
}

// Returns the address of some shadow object.
uintptr_t ShadowOf(const ShadowStructureDescription *desc, uintptr_t addr) {
  GRANARY_ASSERT(desc->is_registered);
  GRANARY_ASSERT(nullptr != gShadowMem);
  addr >>= gShiftAmountLong;
  addr <<= gScaleAmountLong;
  addr &= 0xFFFFFFFFUL;
  return reinterpret_cast<uintptr_t>(gShadowMem) + addr + desc->offset;
}

// Add the `shadow_memory` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<DirectMappedShadowMemory>("shadow_memory");
}
