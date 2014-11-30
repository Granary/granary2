/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#if 0 //def GRANARY_WHERE_user

#include "clients/user/syscall.h"
#include "clients/watchpoints/type_id.h"
#include "clients/wrap_func/client.h"

#include "generated/clients/data_reactor/offsets.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_positive_int(shadow_granularity, 4096,
    "The granularity (in bytes) of shadow memory. This must be a power of two. "
    "The default value is `4096`.",

    "data_collider");

namespace {

enum : uint64_t {
  ADDRESS_SPACE_SIZE = 1ULL << 47U,
  NUM_SAMPLE_SOURCES = MAX_TYPE_ID + 1
};

// Amount by which addresses should be shifted.
static uint64_t gShiftAmountLong = 0UL;
static uint8_t gShiftAmount = 0;

// Size (in bytes) of the shadow memory.
static auto gShadowMemSize = 0UL;

// Base and limit of shadow memory.
static void *gBeginShadowMem = nullptr;
static void *gEndShadowMem = nullptr;

// Tells us if we came across a `clone` system call.
static __thread bool tIsClone = false;

// Find `clone` system calls, which are used for spawning threads.
static void FindClone(void *, SystemCallContext context) {
  tIsClone = __NR_clone == context.Number();
}

struct SampleSource {
  // Number of additional shadow memory slots the sample object occupies.
  size_t num_additional_slots:16;

  // Shadow address of the sample object.
  uintptr_t address:48;
} __attribute__((packed));

static_assert(sizeof(SampleSource) == sizeof(uintptr_t),
    "Bad structure packing for type `SampleSource`.");

// Set of all addresses that can be sampled.
static SampleSource gSampleSources[NUM_SAMPLE_SOURCES] = {{0,0}};
static SpinLock gSampleSourcesLock;

// After a `clone` system call, set the `GS` segment base to point to shadow
// memory.
//
// There's a bit of duplication here in that we'll set the `GS` base on both
// the new thread and the old thread, but that doesn't matter.
static void SetupShadowSegment(void *, SystemCallContext) {
  if (!tIsClone) return;
  GRANARY_IF_DEBUG( auto ret = ) arch_prctl(ARCH_SET_GS, gBeginShadowMem);
  GRANARY_ASSERT(!ret);
  tIsClone = false;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"

static void InitShadowMemory(void) {
  gShiftAmountLong = static_cast<uint64_t>(__builtin_ctzl(
      static_cast<uint64_t>(FLAG_shadow_granularity)));

  gShiftAmount = static_cast<uint8_t>(gShiftAmountLong);
  gShadowMemSize = ADDRESS_SPACE_SIZE >> gShiftAmountLong;
  gShadowMemSize = GRANARY_ALIGN_TO(gShadowMemSize, arch::PAGE_SIZE_BYTES);

  // Allocate the shadow memory space. To reduce the scope of what we actually
  // want to sample, we'll lazily map the shadow memory on the first fault, and
  // record the mapped shadow memory in a simple data structure that GDB can
  // then inspect to choose taint targets.
  gBeginShadowMem = mmap(nullptr, gShadowMemSize,
                         PROT_READ | PROT_WRITE,  // Fault on first access.
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                         -1, 0);

  if (MAP_FAILED == gBeginShadowMem) {
    os::Log(os::LogDebug, "Failed to map shadow memory. Exiting.\n");
    exit(EXIT_FAILURE);
  }

  gEndShadowMem = reinterpret_cast<char *>(gBeginShadowMem) + gShadowMemSize;

  // Make it so that the `GS` segment points to our shadow memory.
  GRANARY_IF_DEBUG( auto ret = ) arch_prctl(ARCH_SET_GS, gBeginShadowMem);
  GRANARY_ASSERT(!ret);

  // Interpose on clone system calls so that we can setup the shadow memory.
  AddSystemCallEntryFunction(FindClone);
  AddSystemCallExitFunction(SetupShadowSegment);
}

#pragma clang diagnostic pop

// Add an address for sampling.
static void AddSampleAddress(uintptr_t type_id, void *addr, size_t size) {
  auto sample_addr = (reinterpret_cast<uintptr_t>(addr) >> gShiftAmountLong) +
                     reinterpret_cast<uintptr_t>(gBeginShadowMem);
  auto sample_size = (size >> gShiftAmountLong) & 0xFFFFULL;

  SpinLockedRegion locker(&gSampleSourcesLock);
  auto &source(gSampleSources[type_id]);
  source.address = sample_addr;
  source.num_additional_slots = sample_size;
}

#define GET_ALLOCATOR(name) \
  auto name = WRAPPED_FUNCTION; \
  auto ret_address = NATIVE_RETURN_ADDRESS

#define SAMPLE_AND_RETURN_ADDRESS \
  if (addr) { \
    auto type_id = TypeIdFor(ret_address, size); \
    AddSampleAddress(type_id, addr, size); \
  } \
  return addr

#define SAMPLE_ALLOCATOR(lib, name) \
  WRAP_INSTRUMENTED_FUNCTION(lib, name, (void *), (size_t size)) { \
    GET_ALLOCATOR(name); \
    auto addr = name(size); \
    SAMPLE_AND_RETURN_ADDRESS; \
  }

SAMPLE_ALLOCATOR(libc, malloc)
SAMPLE_ALLOCATOR(libc, valloc)
SAMPLE_ALLOCATOR(libc, pvalloc)
SAMPLE_ALLOCATOR(libstdcxx, _Znwm)
SAMPLE_ALLOCATOR(libstdcxx, _Znam)
SAMPLE_ALLOCATOR(libcxx, _Znwm)
SAMPLE_ALLOCATOR(libcxx, _Znam)

// Make a wrapper for an allocator.
WRAP_INSTRUMENTED_FUNCTION(libc, calloc, (void *), (size_t count,
                                                    size_t size)) {
  GET_ALLOCATOR(calloc);
  auto addr = calloc(count, size);
  size *= count;
  SAMPLE_AND_RETURN_ADDRESS;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, aligned_alloc, (void *), (size_t align,
                                                     size_t size)) {
  GET_ALLOCATOR(aligned_alloc);
  auto addr = aligned_alloc(align, size);
  SAMPLE_AND_RETURN_ADDRESS;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, memalign, (void *), (size_t align, size_t size)) {
  GET_ALLOCATOR(memalign);
  auto addr = memalign(align, size);
  SAMPLE_AND_RETURN_ADDRESS;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, posix_memalign, (int), (void **addr_ptr,
                                                   size_t align, size_t size)) {
  GET_ALLOCATOR(posix_memalign);
  auto ret = posix_memalign(addr_ptr, align, size);
  if (!ret) {
    auto type_id = TypeIdFor(ret_address, size);
    AddSampleAddress(type_id, *addr_ptr, size);
  }
  return ret;
}

// TODO(pag): Don't handle `realloc` at the moment because we have no idea what
//            type id it should be associated with.

static auto gCurrSourceTypeId = 0UL;

static uintptr_t GetSampleAddress(uintptr_t type_id) {
  SpinLockedRegion locker(&gSampleSourcesLock);
  return gSampleSources[type_id].address;
}

// Try to change what proxy memory address gets sampled.
static void ChangeSampleSource(int) {
  for (int num_attempts = NUM_SAMPLE_SOURCES; num_attempts-- > 0; ) {
    auto type_id = gCurrSourceTypeId++ % NUM_SAMPLE_SOURCES;
    if (auto addr = GetSampleAddress(type_id)) {
      granary_gdb_event1(addr);
      break;
    }
  }
  alarm(1);
}

// Add a `SIGALRM` handler, then start an alarm.
static void InitSampler(void) {
  struct kernel_sigaction sig;
  memset(&sig, 0, sizeof sig);
  memset(&(sig.sa_mask), 0xFF, sizeof sig.sa_mask);
  sig.k_sa_handler = &ChangeSampleSource;
  sig.sa_restorer = &rt_sigreturn;
  sig.sa_flags = SA_INTERRUPT | SA_RESTORER | SA_RESTART;
  GRANARY_IF_DEBUG( auto ret = ) rt_sigaction(SIGALRM, &sig, nullptr,
                                              _NSIG / 8);
  GRANARY_ASSERT(!ret);
  alarm(1);
}

}  // namespace

// Simple tool for static and dynamic basic block counting.
class DataReactor : public InstrumentationTool {
 public:
  virtual ~DataReactor(void) = default;

  virtual void Init(InitReason) {
    InitShadowMemory();

    // Wrap libc.
    AddFunctionWrapper(&WRAP_FUNC_libc_malloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_valloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_pvalloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_aligned_alloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_memalign);
    AddFunctionWrapper(&WRAP_FUNC_libc_posix_memalign);
    AddFunctionWrapper(&WRAP_FUNC_libc_calloc);

    // Wrap GNU's C++ standard library.
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__Znwm);
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__Znam);

    // Wrap clang's C++ standard library.
    AddFunctionWrapper(&WRAP_FUNC_libcxx__Znwm);
    AddFunctionWrapper(&WRAP_FUNC_libcxx__Znam);

    InitSampler();
  }

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
#ifdef GRANARY_WHERE_user
    // A kernel pointer from the vdso.
    if (GRANARY_UNLIKELY(ptr >= 0xFFFFFFFFFFFFULL)) {
      ptr &= 0xFFFFFFFFULL;
    }
#endif
    ImmediateOperand shadow_offset(ptr >> gShiftAmountLong);
    lir::InlineAssembly asm_(shadow_offset);
    asm_.InlineBefore(instr, "MOV r64 %2, i64 %0;");
    TouchShadow(instr, mloc, asm_);
  }

  // Instrument a memory operand that accesses some memory address through a
  // register.
  void InstrumentRegMemOp(NativeInstruction *instr, const MemoryOperand &mloc,
                          VirtualRegister addr) {
    RegisterOperand reg(addr);
    ImmediateOperand shift(gShiftAmount);
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
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<DataReactor>("data_reactor", {"gdb", "wrap_func"});
}

#endif  // GRANARY_WHERE_user
