/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#ifdef GRANARY_WHERE_user

#include "clients/watchpoints/type_id.h"
#include "clients/wrap_func/client.h"
#include "clients/shadow_memory/client.h"
#include "clients/user/syscall.h"
#include "clients/util/instrument_memop.h"

#include "generated/clients/data_collider/offsets.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_positive_int(sample_rate, 20,
    "Defines the rate, in milliseconds, at which DataCollider changes its "
    "sample points in proxy memory. The default value is `20`, representing "
    "`20ms`.\n"
    "\n"
    "Note: This value is approximate, in that we do not guarantee that\n"
    "      sampling will indeed occur every N ms, but rather, approximately\n"
    "      every N ms, given a fair scheduler.",

    "data_collider");

GRANARY_DEFINE_positive_int(watchpoint_granularity, 4096,
    "The granularity (in bytes) of the software watchpoint. This must be a "
    "power of two. The default value is `4096`, which means: 1 page of "
    "physical memory maps to one unit of shadow memory.",

    "data_collider");

namespace {
enum : size_t {
  kStackSize = arch::PAGE_SIZE_BYTES * 2,
  kNumSamplePoints = kMaxWatchpointTypeId + 1
};

// The stack on which the monitor thread executes.
alignas(arch::PAGE_SIZE_BYTES) static char gMonitorStack[kStackSize];

// Defines the granularity of the software watchpoint.
static size_t gShiftAmount = 0;

// Set of all addresses that can be sampled.
static uintptr_t gSamplePoints[kNumSamplePoints] = {0ULL};
static SpinLock gSamplePointsLock;

// The current address being sample.
static uintptr_t gSamplePoint = 0;

// Current type ID being sampled.
static auto gCurrSourceTypeId = 0UL;

// Is the program multi-threaded?
static bool gIsMultithreaded = false;

// The PID of the monitor thread.
static pid_t gMonitorThread = -1;

// Interposes on system calls to detect the spawning of threads. If a thread is
// spawned then the sampler will turn on, otherwise it will never add
// watchpoints.
static void DetectMultiThreadedCode(void *, SystemCallContext ctx) {
  if (__NR_clone == ctx.Number() && 0 != (CLONE_THREAD & ctx.Arg0())) {
    gIsMultithreaded = true;
  }
}

// Add an address for sampling.
static void AddSamplePoint(uintptr_t type_id, void *ptr) {
  auto addr = reinterpret_cast<uintptr_t>(ptr) >> gShiftAmount;
  gSamplePoints[type_id] = addr;
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

// Get the next sample point to return.
static uintptr_t NextSamplePoint(void) {
  for (int num_attempts = kNumSamplePoints; num_attempts-- > 0; ) {
    auto type_id = gCurrSourceTypeId++ % kNumSamplePoints;
    SpinLockedRegion locker(&gSamplePointsLock);
    if (auto sample = gSamplePoints[type_id]) {
      return sample;
    }
  }
  return 0;
}

// Monitors a single sample point.
static void MonitorSamplePoint(uintptr_t &last_sample) {
  if (auto sample = NextSamplePoint()) {
    if (sample == last_sample) return;
    last_sample = sample;

    os::Log("Sampling address %lx.\n", sample);
  }
}

// Monitor thread changes the sample point every FLAG_sample_rate milliseconds.
static void Monitor(void) {
  auto sample_time_ms = FLAG_sample_rate * 1000000L;
  const timespec sample_time = {0, sample_time_ms};
  for (uintptr_t last_sample(0);;) {
    nanosleep(&sample_time, nullptr);
    if (!gIsMultithreaded) continue;
    MonitorSamplePoint(last_sample);
  }
}

// Initialize the monitoring process for DataCollider. This allows us to set
// hardware watchpoints.
static void CreateMonitorThread(void) {
  auto stack_ptr = &(gMonitorStack[kStackSize - arch::ADDRESS_WIDTH_BYTES]);
  *UnsafeCast<void(**)(void)>(stack_ptr) = Monitor;
  auto ret = sys_clone(CLONE_VM | CLONE_FILES | CLONE_FS | CLONE_UNTRACED |
                       CLONE_THREAD | CLONE_SIGHAND,
                       stack_ptr, nullptr, nullptr, 0);
  if (0 >= ret) {
    os::Log("ERROR: Couldn't create monitor thread.\n");
    exit(EXIT_FAILURE);
  }
}

}  // namespace

// Simple tool for static and dynamic basic block counting.
class DataCollider : public MemOpInstrumentationTool {
 public:
  virtual ~DataCollider(void) = default;

  // Initialize the few things that we can. We can't initialize the shadow
  // memory up-front because dependent tools won't yet be initialized, and
  // therefore won't have added their shadow structure descriptions yet. We
  // need those shadow structure descriptions to determine the size of shadow
  // memory.
  static void Init(InitReason reason) {
    if (kInitThread == reason) return;

    gShiftAmount = static_cast<size_t>(
        __builtin_ctz(static_cast<unsigned>(FLAG_watchpoint_granularity)));

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

    // Wrap system calls to make sure that we only monitor multi-threaded code.
    AddSystemCallEntryFunction(&DetectMultiThreadedCode);

    CreateMonitorThread();
  }

  // Exit; this kills off the monitor thread.
  static void Exit(ExitReason reason) {
    if (kExitThread == reason) return;
    if (kExitProgram != reason && -1 != gMonitorThread) {
      kill(gMonitorThread, SIGKILL);
    }
    gMonitorThread = -1;
    gCurrSourceTypeId = 0;
    gIsMultithreaded = false;
    gShiftAmount = 0;
    gSamplePoint = 0;
    memset(&(gSamplePoints[0]), 0, sizeof gSamplePoints);
  }

 protected:
  virtual void InstrumentMemOp(DecodedBasicBlock *, NativeInstruction *instr,
                               MemoryOperand &, const RegisterOperand &addr) {
    if (addr.IsStackPointer() || addr.IsVirtualStackPointer()) return;

    MemoryOperand sample_point(&gSamplePoint);
    ImmediateOperand shift_amount(gShiftAmount, 1);
    lir::InlineAssembly asm_(addr, shift_amount, sample_point);
    asm_.InlineBefore(instr, "MOV r64 %3, r64 %0;"_x86_64);
    asm_.InlineBeforeIf(instr, 0 < gShiftAmount, "SHR r64 %3, i8 %1;");
    asm_.InlineBefore(instr, "CMP r64 %3, m64 %2;"
                             "JNZ l %4;"_x86_64);
    asm_.InlineBefore(instr, "LABEL %4:"_x86_64);
  }
};

// Initialize the `data_collider` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<DataCollider>("data_collider", {"wrap_func"});
}

#endif  // GRANARY_WHERE_user
