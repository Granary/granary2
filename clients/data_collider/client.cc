/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#ifdef GRANARY_WHERE_user

#include "clients/watchpoints/client.h"  // For type ID stuff.
#include "clients/wrap_func/client.h"
#include "clients/shadow_memory/client.h"
#include "clients/util/instrument_memop.h"

#include "generated/clients/data_collider/offsets.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_positive_uint(sample_rate, 100,
    "The rate, in milliseconds, at which DataCollider changes its sample "
    "points. The default value is `100`, representing `100ms`.\n"
    "\n"
    "Note: This value is approximate, in that we do not guarantee that\n"
    "      sampling will indeed occur every N ms, but rather, approximately\n"
    "      every N ms, given a fair scheduler.",

    "data_collider");

GRANARY_DEFINE_positive_uint(num_sample_points, 1,
    "The number of addresses that will be sampled by DataCollider. By default "
    "this is `1`. The maximum number of active sample points is `4096`.",

    "data_collider");

namespace {
enum : size_t {
  kStackSize = arch::PAGE_SIZE_BYTES * 2,
  kNumSamplePoints = kMaxWatchpointTypeId + 1,
  kMaxNumActiveSamplePoints = 4096
};

// The state of shadow memory.
enum ShadowState : uint8_t {
  kShadowUnwatched = 0,
  kShadowWatched = 1
};

// Shadow memory for ownership tracking.
union OnwershipTracker {
  struct {
    // State that tracks the ownership of shadow memory.
    ShadowState state:8;
    uint64_t thread_base:56;
  };

  uint64_t value;

} __attribute__((packed));

static_assert(8 == sizeof(OnwershipTracker),
              "Error: Invalid structure packing of `struct OnwershipTracker`.");

// The stack on which the monitor thread executes.
alignas(arch::PAGE_SIZE_BYTES) static char gMonitorStack[kStackSize];

// Set of all shadow locations that can be sampled.
static OnwershipTracker *gSamplePoints[kNumSamplePoints] = {nullptr};

// Set of shadow locations that are actively being sampled.
static OnwershipTracker *
gActiveSamplePoints[kMaxNumActiveSamplePoints] = {nullptr};

// The PID of the monitor thread.
static pid_t gMonitorThread = -1;

// Used to index into `gSamplePoints` when adding sample points. This goes
// round-robin through the sample points to make sure all types are sampled.
static size_t gCurrSourceIndex = 0;

// Add an address for sampling.
static void AddSamplePoint(uintptr_t type_id, void *ptr) {
  gSamplePoints[type_id] = ShadowOf<OnwershipTracker>(ptr);
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
  WRAP_NATIVE_FUNCTION(lib, name, (void *), (size_t size)) { \
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
WRAP_NATIVE_FUNCTION(libc, calloc, (void *), (size_t count, size_t size)) {
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

static void ClearActiveSamplePoints(void) {
  for (auto i = 0U; i < FLAG_num_sample_points; ++i) {
    if (gActiveSamplePoints[i]) {
      gActiveSamplePoints[i]->value = 0;
      gActiveSamplePoints[i] = nullptr;
    }
  }
}

// Monitors a single sample point.
static void ActivateSamplePoints(void) {
  auto end_id = (gCurrSourceIndex + kNumSamplePoints - 1) % kNumSamplePoints;
  for (auto num_samples = 0U; num_samples < FLAG_num_sample_points; ) {
    auto type_id = gCurrSourceIndex++ % kNumSamplePoints;
    if (auto sample = gSamplePoints[type_id]) {
      gActiveSamplePoints[num_samples] = sample;
      sample->state = kShadowWatched;
      ++num_samples;
    }
    if (type_id == end_id) break;
  }
}

// Monitor thread changes the sample point every FLAG_sample_rate milliseconds.
static void Monitor(void) {
  const timespec sample_time = {0, FLAG_sample_rate * 1000000L};
  const timespec clear_time = {0, 1000000L};
  for (;;) {
    for (auto timer = sample_time; timer.tv_nsec >= 1000000L; ) {
      nanosleep(&timer, &timer);
    }

    // Potentiall race? Let's try to wait it out.
    ClearActiveSamplePoints();
    nanosleep(&clear_time, nullptr);
    ClearActiveSamplePoints();

    ActivateSamplePoints();
  }
}

// Initialize the monitoring process for DataCollider. This allows us to set
// hardware watchpoints.
//
// TODO(pag): The only thing that makes this actually work is luck...
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
class DataCollider : public InstrumentationTool {
 public:
  virtual ~DataCollider(void) = default;

  // Initialize the few things that we can. We can't initialize the shadow
  // memory up-front because dependent tools won't yet be initialized, and
  // therefore won't have added their shadow structure descriptions yet. We
  // need those shadow structure descriptions to determine the size of shadow
  // memory.
  static void Init(InitReason reason) {
    if (kInitThread == reason) return;

    if (FLAG_num_sample_points > kMaxNumActiveSamplePoints) {
      os::Log("Error: Too many sample points. The maximum is %lu\n.",
              kMaxNumActiveSamplePoints);
      FLAG_num_sample_points = kMaxNumActiveSamplePoints;
    }

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

    CreateMonitorThread();
    AddShadowStructure<OnwershipTracker>(InstrumentMemOp);
  }

  // Exit; this kills off the monitor thread.
  static void Exit(ExitReason reason) {
    if (kExitThread == reason) return;
    if (kExitProgram != reason && -1 != gMonitorThread) {
      kill(gMonitorThread, SIGKILL);
    }
    gMonitorThread = -1;
    gCurrSourceIndex = 0;
    memset(&(gSamplePoints[0]), 0, sizeof gSamplePoints);
    memset(&(gActiveSamplePoints[0]), 0, sizeof gActiveSamplePoints);
  }

 protected:
  static void InstrumentMemRead(const ShadowedOperand &op) {
    GRANARY_UNUSED(op);
  }

  static void InstrumentMemWrite(const ShadowedOperand &op) {
    lir::InlineAssembly asm_(op.shadow_addr_op);

    asm_.InlineBefore(op.instr,
        // Start with a racy read of `OnwershipTracker::state`.
        "CMP m8 [%0], i8 0;"

        // Common case: `OnwershipTracker::state == kShadowUnwatched`.
        "JZ l %1;"

        // Racy check that we don't own the cache line. Compare only the low
        // order 32 bits.
        "MOV r64 %4, m64 FS:[0];"
        "CMP m32 [%0 + 4], r32 %4;"
        "JZ l %1;"

        // Okay, we could be in `kShadowWatched` or `kShadowOwned`. We will
        // unwatched the location, then check what state it was in, then, if
        // it was `kShadowWatched`, we'll take ownership, otherwise we'll
        // report.
        "XCHG m64 [%0], r64 %4;"

        // Between the racy read and the `XCHG`, the state changed to unwatched
        // so go remove the watchpoint.
        "TEST r64 %4, r64 %4;"
        "JZ l %2;"

        // Now need to figure out if we're the owner or the contender. Test the
        // low-order 32 bits, which should be non-zero because they should store
        // part of an address.
        "TEST r32 %4, r32 %4;"
        "JZ l %3;"

        // We're the contender.
        "INT3;"  // Detected an issue!
        "JMP l %2;"

        // We're the owner; re-add the watchpoint.
        //
        // Note: There is a race here with contender threads. I am pretty sure
        //       it's benign, insofar as it shouldn't affect correctness, and
        //       should also potentially re-enabled the watchpoint. The trick
        //       is that one of the outcomes could be the contended turning
        //       into owner.
        "LABEL %3:"
        "MOV m8 [%0], i8 1;"

        "JMP l %1;"

        // Remove the watchpoint, and fall-through.
        "LABEL %2:"
        "MOV m64 [%0], i32 0;"

        // Done, fall-through to instruction.
        "LABEL %1:");
  }

  static void InstrumentMemOp(const ShadowedOperand &op) {
    InstrumentMemWrite(op);
    return;
    if (op.native_mem_op.IsWrite()) {
      InstrumentMemWrite(op);
    } else {
      InstrumentMemRead(op);
    }
  }
};

// Initialize the `data_collider` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<DataCollider>("data_collider", {"wrap_func",
                                                         "shadow_memory"});
}

#endif  // GRANARY_WHERE_user
