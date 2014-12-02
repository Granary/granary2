/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#ifdef GRANARY_WHERE_user

#include "clients/watchpoints/type_id.h"
#include "clients/wrap_func/client.h"
#include "clients/shadow_memory/client.h"

#include "generated/clients/data_reactor/offsets.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_positive_int(sample_rate, 10,
    "Defines the rate, in milliseconds, at which DataReactor changes its "
    "sample points. The default value is `10`, representing `10ms`.\n"
    "\n"
    "Note: This value is approximate, in that we do not guarantee that\n"
    "      sampling will indeed occur every N ms, but rather, approximately\n"
    "      every N ms, given a fair scheduler.",

    "data_reactor");

namespace {
enum : size_t {
  kStackSize = arch::PAGE_SIZE_BYTES * 8,
  kNumSamplePoints = kMaxWatchpointTypeId + 1
};

struct SamplePoint {};

// The stack on which the monitor thread executes.
alignas(arch::PAGE_SIZE_BYTES) static char gMonitorStack[kStackSize];

// Set of all addresses that can be sampled.
static SamplePoint *gSamplePoints[kNumSamplePoints] = {nullptr};
static SpinLock gSamplePointsLock;

// Current type ID being sampled.
static auto gCurrSourceTypeId = 0UL;

// Add an address for sampling.
static void AddSamplePoint(uintptr_t type_id, void *addr) {
  SpinLockedRegion locker(&gSamplePointsLock);
  gSamplePoints[type_id] = ShadowOf<SamplePoint>(addr);
}

#define GET_ALLOCATOR(name) \
  auto name = WRAPPED_FUNCTION; \
  auto ret_address = NATIVE_RETURN_ADDRESS

#define SAMPLE_AND_RETURN_ADDRESS \
  if (addr) { \
    auto type_id = TypeIdFor(ret_address, size); \
    AddSamplePoint(type_id, addr); \
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
    AddSamplePoint(type_id, *addr_ptr);
  }
  return ret;
}

// TODO(pag): Don't handle `realloc` at the moment because we have no idea what
//            type id it should be associated with.

static pid_t gProgramPid;

// Get the next sample point to return.
static SamplePoint *NextSamplePoint(void) {
  for (int num_attempts = kNumSamplePoints; num_attempts-- > 0; ) {
    auto type_id = gCurrSourceTypeId++ % kNumSamplePoints;
    SpinLockedRegion locker(&gSamplePointsLock);
    if (auto sample = gSamplePoints[type_id]) {
      return sample;
    }
  }
  return nullptr;
}

// Monitor thread changes the sample point every FLAG_sample_rate milliseconds.
static void Monitor(void) {
  const timespec sample_10ms = {0, FLAG_sample_rate * 1000000L};
  for (SamplePoint *last_sample(nullptr);;) {
    nanosleep(&sample_10ms, nullptr);
    if (auto sample = NextSamplePoint()) {
      if (sample == last_sample) continue;
      last_sample = sample;
    }
  }
}

// Initialize the monitoring process for DataReactor. This allows us to set
// hardware watchpoints.
static void InitMonitor(void) {
  unsigned long flags = CLONE_VM | CLONE_FILES | CLONE_THREAD | CLONE_UNTRACED;
  gProgramPid = getpid();
  auto stack_ptr = &(gMonitorStack[kStackSize - arch::ADDRESS_WIDTH_BYTES]);
  *UnsafeCast<void(**)(void)>(stack_ptr) = Monitor;
  sys_clone(flags, stack_ptr, nullptr, nullptr, 0);
}

}  // namespace

// Simple tool for static and dynamic basic block counting.
class DataReactor : public InstrumentationTool {
 public:
  virtual ~DataReactor(void) = default;

  virtual void Init(InitReason) {
    AddShadowStructure<SamplePoint>(AccessProxyMem);

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

    InitMonitor();
  }

 private:
  // Implements the actual touching (reading or writing) of shadow memory.
  static void AccessProxyMem(const ShadowedOperand &op) {
    lir::InlineAssembly asm_(op.shadow_addr_op);
    if (op.native_mem_op.IsReadWrite()) {
      asm_.InlineBefore(op.instr, "AND m8 [%0], i8 0;");
    } else if (op.native_mem_op.IsWrite()) {
      asm_.InlineBefore(op.instr, "MOV m8 [%0], i8 0;");
    } else {
      asm_.InlineBefore(op.instr, "TEST m8 [%0], i8 0;");
    }
  }
};

// Initialize the `data_reactor` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<DataReactor>(
      "data_reactor", {"gdb", "wrap_func", "shadow_memory"});
}

#endif  // GRANARY_WHERE_user
