/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#ifdef GRANARY_WHERE_user

#include "clients/data_reactor/arch/x86-64.h"
#include "clients/watchpoints/type_id.h"
#include "clients/wrap_func/client.h"
#include "clients/shadow_memory/client.h"
#include "clients/user/syscall.h"

#include "generated/clients/data_reactor/offsets.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_positive_int(proxy_sample_rate, 20,
    "Defines the rate, in milliseconds, at which DataReactor changes its "
    "sample points in proxy memory. The default value is `20`, representing "
    "`20ms`.\n"
    "\n"
    "Note: This value is approximate, in that we do not guarantee that\n"
    "      sampling will indeed occur every N ms, but rather, approximately\n"
    "      every N ms, given a fair scheduler.",

    "data_reactor");

namespace {
enum : size_t {
  kStackSize = arch::PAGE_SIZE_BYTES * 2,
  kNumSamplePoints = kMaxWatchpointTypeId + 1,
  kOffsetOfDR0 = offsetof(struct user, u_debugreg[0]),
  kOffsetOfDR7 = offsetof(struct user, u_debugreg[7]),
  kNumPtraceSeizeAttempts = 20,
  kNumPtracePokeAttempts = 20,
};

// Shadow memory type. By default, this will be 1 byte in size.
struct SamplePoint {};

// The stack on which the monitor thread executes.
alignas(arch::PAGE_SIZE_BYTES) static char gMonitorStack[kStackSize];

// Set of all addresses that can be sampled.
static SamplePoint *gSamplePoints[kNumSamplePoints] = {nullptr};
static SpinLock gSamplePointsLock;

// Current type ID being sampled.
static auto gCurrSourceTypeId = 0UL;

// Is the program multi-threaded?
static bool gIsMultithreaded = false;

// Has the program exited?
static bool gProgramExited = false;
static bool gMonitorExited = false;

// Process IDs of the instrumented program (`gProgramPid`) and the monitor
// process (`gMonitorPid`).
static pid_t gProgramPid = -1;
static pid_t gMonitorPid = -1;

// We use this to block the monitor process's execution until the parent
// process has set up the monitor process as its tracer.
static os::Lock gMonitorLock;

// Have we enabled `DR7` yet? We only set the values of `DR7` once.
static bool gEnabledDR7 = false;

// Small buffer used for `write`s by the monitor thread.
static char gMonitorBuff[256] = {'\0'};

template <typename... Args>
inline static void Output(const char *format, Args... args) {
  write(1, gMonitorBuff, Format(gMonitorBuff, format, args...));
}

// Interposes on system calls to detect the spawning of threads. If a thread is
// spawned then the sampler will turn on, otherwise it will never add
// watchpoints.
static void DetectMultiThreadedCode(void *, SystemCallContext ctx) {
  if (__NR_clone == ctx.Number() && 0 != (CLONE_THREAD & ctx.Arg0())) {
    gIsMultithreaded = true;
  }
}

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

// Kill the instrumented program and exit the monitor thread.
static void ExitMonitor(void) {
  gMonitorExited = true;
  //kill(gProgramPid, SIGKILL);
  exit(EXIT_FAILURE);
}

// Add the sampled address as a watched address.
static bool AddWatchpoint(SamplePoint *sample) {
  const auto addr = reinterpret_cast<uintptr_t>(sample);
  const timespec poke_time = {0, 1000000L};  // 1ms.
  for (auto i = 0UL; i < kNumPtracePokeAttempts; ++i) {
    if (!ptrace(PTRACE_POKEUSER, gProgramPid, kOffsetOfDR0, addr)) return true;
    nanosleep(&poke_time, nullptr);
  }
  return false;
}

// Enabled hardware watchpoints.
static void EnableWatchpoints(void) {
  if (!gEnabledDR7) {
    gEnabledDR7 = true;
    dr7_t dr7 = {0};
    dr7.l0 = 1;
    dr7.rw0 = DR7_BREAK_ON_RW;
    dr7.len0 = DR7_LEN_4;
    if (ptrace(PTRACE_POKEUSER, gProgramPid, kOffsetOfDR7, dr7.value)) {
      Output("ERROR: Couldn't set DR7 with value %lx.\n", dr7.value);
      ExitMonitor();
    }
    Output("Enabled hardware watchpoints.\n");
  }
}

// Monitors a single sample point.
static void MonitorSamplePoint(SamplePoint *&last_sample) {
  if (auto sample = NextSamplePoint()) {
    if (sample == last_sample) return;
    last_sample = sample;
    if (AddWatchpoint(sample)) {
      EnableWatchpoints();
      Output("Sampling address %p.\n", sample);
    }
  }
}

// Try to seize the instrumented program with ptrace.
static void SeizeInstrumentedProcess(void) {
  const timespec seize_time = {0, 1000000L};  // 1ms.
  auto seized = false;
  for (auto i = 0UL; i < kNumPtraceSeizeAttempts; ++i) {
    nanosleep(&seize_time, nullptr);
    if (!(seized = ptrace(PTRACE_SEIZE, gProgramPid))) continue;
  }
  if (gProgramExited) ExitMonitor();
  if (!seized) {
    Output("ERROR: Failed to seize process %d.\n", gProgramPid);
    ExitMonitor();
  }
  Output("Seized process %d.\n", gProgramPid);
}

// Monitor thread changes the sample point every FLAG_sample_rate milliseconds.
static void Monitor(void) {
  os::LockedRegion locker(&gMonitorLock);

  gEnabledDR7 = false;

  Output("Monitor PID: %d\n", gMonitorPid);
  SeizeInstrumentedProcess();

  auto sample_time_ms = FLAG_proxy_sample_rate * 1000000L;
  const timespec sample_time = {0, sample_time_ms};

  for (SamplePoint *last_sample(nullptr);;) {
    nanosleep(&sample_time, nullptr);
    if (!gIsMultithreaded) continue;
    MonitorSamplePoint(last_sample);
  }
}

// Initialize the monitoring process for DataReactor. This allows us to set
// hardware watchpoints.
static void CreateMonitorThread(void) {
  os::LockedRegion locker(&gMonitorLock);
  gProgramPid = getpid();
  gProgramExited = false;
  gMonitorExited = false;

  auto stack_ptr = &(gMonitorStack[kStackSize - arch::ADDRESS_WIDTH_BYTES]);
  *UnsafeCast<void(**)(void)>(stack_ptr) = Monitor;
  auto ret = sys_clone(CLONE_VM | CLONE_FILES | CLONE_FS | CLONE_UNTRACED,
                       stack_ptr, nullptr, nullptr, 0);
  if (0 >= ret) {
    os::Log("ERROR: Couldn't create monitor thread.\n");
    exit(EXIT_FAILURE);
  }
  gMonitorPid = static_cast<pid_t>(ret);
  prctl(PR_SET_PTRACER, gMonitorPid, 0, 0, 0);
}

static void KillMonitorThread(void) {
  if (!gMonitorExited && -1 != gMonitorPid) {
    kill(gMonitorPid, SIGKILL);
  }
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

    // Wrap system calls to make sure that we only monitor multi-threaded code.
    AddSystemCallEntryFunction(&DetectMultiThreadedCode);

    CreateMonitorThread();
  }

  // Exit; this kills off the monitor thread.
  virtual void Exit(ExitReason reason) {
    if (kExitProgram == reason) {
      KillMonitorThread();
    } else if (kExitDetach == reason) {
      gCurrSourceTypeId = 0;
      gIsMultithreaded = false;
      gProgramExited = true;
      memset(gSamplePoints, 0, sizeof gSamplePoints);
    }
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
