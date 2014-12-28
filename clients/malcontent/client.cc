/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#ifdef GRANARY_WHERE_user

#include "clients/watchpoints/client.h"  // For type ID stuff.
#include "clients/wrap_func/client.h"
#include "clients/shadow_memory/client.h"
#include "clients/stack_trace/client.h"
#include "clients/util/instrument_memop.h"

#include "generated/clients/malcontent/offsets.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_positive_uint(sample_rate, 500,
    "The rate, in milliseconds, at which Malcontent changes its sample "
    "points. The default value is `500`, representing `500ms`.\n"
    "\n"
    "Note: This value is approximate, in that we do not guarantee that\n"
    "      sampling will indeed occur every N ms, but rather, approximately\n"
    "      every N ms, given a fair scheduler.",

    "data_collider");

GRANARY_DEFINE_positive_uint(num_sample_points, 64,
    "The number of addresses that will be sampled by Malcontent. By default "
    "this is `64`. The maximum number of active sample points is `2^16 - 2`.",

    "data_collider");

GRANARY_DECLARE_positive_uint(shadow_granularity);

namespace {
enum : size_t {
  // Stack size of monitor thread.
  kStackSize = arch::PAGE_SIZE_BYTES * 2,

  // Maximum number of sample points tha
  kNumSamplePoints = kMaxWatchpointTypeId + 1,

  // Not all of the sample points are usable because we reserve type id = 0 to
  // represent "unwatched" memory.
  kNumUsableSamplePoints = kNumSamplePoints - 1,

  // How big of a stack trace should be recorder per sample?
  kSampleStackTraceSize = 5
};

// Shadow memory for ownership tracking.
union OwnershipTracker {
  struct {
    uint64_t thread_base:48;
    uint64_t sample_id:16;
  };

  uint64_t value;

} __attribute__((packed));

static_assert(8 == sizeof(OwnershipTracker),
              "Error: Invalid structure packing of `struct OwnershipTracker`.");

// Represents a memory access operand in an application.
union MemoryOperandDescriptor {
  struct {
    uint16_t size:12;
    uint16_t op_num:1;
    bool is_read:1;
    bool is_write:1;
    bool is_atomic:1;
    uintptr_t accessing_pc:48;
  } __attribute__((packed));

  uint64_t value;
};

typedef AppPC StackTrace[kSampleStackTraceSize];

// Represents a summary of memory access information.
struct MemoryAccess {
  const void *address;
  MemoryOperandDescriptor location;
  StackTrace stack_trace;
};

// Represents
struct SamplePoint {
  uint64_t type_id;
  OwnershipTracker *tracker;
  uintptr_t base_address;
  uintptr_t limit_address;
  MemoryAccess accesses[2];
};

// The stack on which the monitor thread executes.
alignas(arch::PAGE_SIZE_BYTES) static char gMonitorStack[kStackSize];

// Set of all shadow locations that can be sampled. This corresponds to recent
// memory allocations.
static void *gRecentAllocations[kNumSamplePoints] = {nullptr};

// Set of active sample points.
static SamplePoint gSamplePoints[kNumSamplePoints];

// Lock guarding the `gSamplePoints` array.
static ReaderWriterLock gSamplePointsLock;

// The PID of the monitor thread.
static pid_t gMonitorThread = -1;

// Used to index into `gSamplePoints` when adding sample points. This goes
// round-robin through the sample points to make sure all types are sampled.
static size_t gCurrSourceIndex = 0;

// Add an address to our potential sample population.
static void AddRecentAllocation(uintptr_t type_id, void *ptr) {
  if (type_id < kNumUsableSamplePoints) {
    gRecentAllocations[type_id + 1] = ptr;
  }
}

#define GET_ALLOCATOR(name) \
  auto name = WRAPPED_FUNCTION; \
  auto ret_address = NATIVE_RETURN_ADDRESS

#define SAMPLE_AND_RETURN_ADDRESS \
  if (addr) { \
    auto type_id = TypeIdFor(ret_address, size); \
    AddRecentAllocation(type_id, addr); \
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
    AddRecentAllocation(type_id, *addr_ptr);
  }
  return ret;
}

// TODO(pag): Don't handle `realloc` at the moment because we have no idea what
//            type id it should be associated with.

static void ClearActiveSamplePoints(void) {
  memset(&(gSamplePoints[0]), 0, sizeof gSamplePoints);
}

static void AddSamplesForType(uint64_t type_id, size_t &num_sample_points) {
  auto alloc_addr = gRecentAllocations[type_id];
  if (!alloc_addr) return;

  auto tracker = ShadowOf<OwnershipTracker>(alloc_addr);
  auto base_address = reinterpret_cast<uintptr_t>(tracker);
  auto limit_address = base_address + SizeOfType(type_id);

  for (; num_sample_points < FLAG_num_sample_points; ) {
    const auto tracker_addr = reinterpret_cast<uintptr_t>(tracker);
    if (tracker_addr >= limit_address) return;

    auto sample_tracker = tracker;
    auto sample_id = num_sample_points++;
    auto &sample(gSamplePoints[sample_id]);

    sample.type_id = type_id;
    sample.tracker = tracker++;
    sample.base_address = reinterpret_cast<uintptr_t>(alloc_addr);
    sample.limit_address = sample.base_address + FLAG_shadow_granularity;

    // We'll enable the sample later. We want to avoid the case of adding two
    // samples to a given object; our approach will be that the last sample
    // wins.
    sample_tracker->value = sample_id;
  }
}

// Samples up to `FLAG_num_sample_points` object trackers.
static void ActivateSamplePoints(void) {

  // Figure out where the "end" of the sampling should be.
  auto end_id = (gCurrSourceIndex + kNumSamplePoints - 1) % kNumSamplePoints;
  if (!end_id) end_id++;  // Never have a zero type id.

  // Add the sample points.
  auto num_samples = 0UL;
  for (; num_samples < FLAG_num_sample_points; ) {
    auto type_id = gCurrSourceIndex++ % kNumSamplePoints;
    if (!type_id) continue;  // Type ID 0 means unwatched.
    AddSamplesForType(type_id, num_samples);
    if (type_id == end_id) break;
  }

  // Activate the sample points.
  for (auto sample_id = 0UL; sample_id < num_samples; ++sample_id) {
    const auto &sample(gSamplePoints[sample_id]);
    auto tracker = sample.tracker;
    if (tracker->value == sample_id) {
      tracker->value = 0;
      std::atomic_thread_fence(std::memory_order_acquire);
      tracker->sample_id = sample_id;
    }
  }
}

// Log a program counter.
static void LogPC(AppPC pc) {
  auto offset = os::ModuleOffsetOfPC(pc);
  if (offset.module) {
    os::Log("    %s:%lu\n", offset.module->Name(), offset.offset);
  } else {
    os::Log("    %p\n", pc);
  }
}

// Log a stack trace.
static void LogStackTrace(const StackTrace &trace) {
  for (auto pc : trace) {
    if (pc) LogPC(pc);
  }
}

// Log an individual memory access.
static void LogMemoryAccess(const MemoryAccess &access) {
  auto is_atomic = access.location.is_atomic ? " atomic" : "";
  auto is_read = access.location.is_read ? " read" : "";
  auto is_write = access.location.is_write ? " write" : "";

  os::Log("  Operand %u accessing %u bytes at %p using%s%s%s:\n",
          access.location.op_num, access.location.size, access.address,
          is_atomic, is_read, is_write);
  LogPC(reinterpret_cast<AppPC>(access.location.accessing_pc));
  LogStackTrace(access.stack_trace);
}

// Reports stack reports for found issues.
static void ReportSamplePoints(void) {
  for (auto &sample : gSamplePoints) {
    if (!sample.tracker) continue;

    // Incomplete.
    if (!sample.accesses[0].address || !sample.accesses[1].address) continue;

    // Read/read, assume no contention.
    if (!sample.accesses[0].location.is_write &&
        !sample.accesses[1].location.is_write) {
      continue;
    }

    // Different cache lines.
    auto cl0 = reinterpret_cast<uintptr_t>(sample.accesses[0].address) >> 6UL;
    auto cl1 = reinterpret_cast<uintptr_t>(sample.accesses[1].address) >> 6UL;
    if (cl0 != cl1) continue;

    os::Log("Contention detected in watched range [%p,%p)\n\n",
            sample.base_address, sample.limit_address);
    LogMemoryAccess(sample.accesses[0]);
    os::Log("\n");
    LogMemoryAccess(sample.accesses[1]);
    os::Log("\n");
  }
}

// Monitor thread changes the sample point every FLAG_sample_rate milliseconds.
static void Monitor(void) {
  const timespec sample_time = {0, FLAG_sample_rate * 1000000L};
  const timespec pause_time = {0, 1000000L};
  for (;;) {
    for (auto timer = sample_time; ; ) {
      if (!nanosleep(&timer, &timer)) break;
    }
    while (!gSamplePointsLock.TryWriteAcquire()) {
      nanosleep(&pause_time, nullptr);
    }
    ReportSamplePoints();
    ClearActiveSamplePoints();
    gSamplePointsLock.WriteRelease();
    ActivateSamplePoints();
  }
}

// Initialize the monitoring process for Malcontent. This allows us to set
// hardware watchpoints.
//
// TODO(pag): The only thing that makes this actually work is luck...
static void CreateMonitorThread(void) {
  auto ret = sys_clone(CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|
                       CLONE_THREAD|CLONE_SYSVSEM,
                       &(gMonitorStack[kStackSize]), nullptr, nullptr, 0,
                       Monitor);
  if (0 >= ret) {
    os::Log("ERROR: Couldn't create monitor thread.\n");
    exit(EXIT_FAILURE);
  }
}

}  // namespace

// Simple tool for static and dynamic basic block counting.
class Malcontent : public InstrumentationTool {
 public:
  virtual ~Malcontent(void) = default;

  // Initialize the few things that we can. We can't initialize the shadow
  // memory up-front because dependent tools won't yet be initialized, and
  // therefore won't have added their shadow structure descriptions yet. We
  // need those shadow structure descriptions to determine the size of shadow
  // memory.
  static void Init(InitReason reason) {
    if (kInitThread == reason) return;

    if (FLAG_num_sample_points > kNumUsableSamplePoints) {
      os::Log("Error: Too many sample points. The maximum is %lu\n.",
              kNumUsableSamplePoints);
      FLAG_num_sample_points = kNumUsableSamplePoints;
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
    AddShadowStructure<OwnershipTracker>(InstrumentMemOp,
                                         ShouldInstrumentMemOp);
  }

  // Exit; this kills off the monitor thread.
  static void Exit(ExitReason reason) {
    if (kExitThread == reason) return;
    if (kExitProgram != reason && -1 != gMonitorThread) {
      kill(gMonitorThread, SIGKILL);
    }
    gMonitorThread = -1;
    gCurrSourceIndex = 0;
    memset(&(gRecentAllocations[0]), 0, sizeof gRecentAllocations);
    ClearActiveSamplePoints();
  }

 protected:
  static void InstrumentContention(const OwnershipTracker tracker,
                                   const MemoryOperandDescriptor location,
                                   const void *address) {

    // Race happened and we missed it. This case comes up when someone just
    // took ownership of the line, and a contender also tried to take
    // ownership. If we've reached here, then we're the contender.
    if (!tracker.sample_id) return;

    ReadLockedRegion locker(&gSamplePointsLock);

    auto &sample_point(gSamplePoints[tracker.sample_id]);
    if (!sample_point.type_id) return;

    const int trace = !!tracker.thread_base;

    // We just took ownership; re-add the watchpoint.
    if (!trace) {
      sample_point.tracker->sample_id = tracker.sample_id;

    // We're the contender, remove the watchpoint and all info.
    } else {
      sample_point.tracker->value = 0;
    }

    // Copy our stack trace.
    auto &access(sample_point.accesses[trace]);
    access.address = address;
    access.location = location;
    CopyStackTrace(access.stack_trace);
  }

  static bool ShouldInstrumentMemOp(const InstrumentedMemoryOperand &op) {
    return !op.native_addr_op.IsStackPointerAlias();
  }

  static void InstrumentMemOp(const ShadowedMemoryOperand &op) {
    // Summary of this particular memory operand.
    MemoryOperandDescriptor mem_access{{
      static_cast<uint16_t>(op.native_mem_op.ByteWidth()),
      static_cast<uint16_t>(op.operand_number),
      op.native_mem_op.IsRead(),
      op.native_mem_op.IsWrite(),
      op.instr->IsAtomic(),
      reinterpret_cast<uintptr_t>(op.instr->DecodedPC())
    }};

    ImmediateOperand mem_access_op(mem_access.value);
    RegisterOperand tracker(op.block->AllocateVirtualRegister());
    lir::InlineAssembly asm_(op.shadow_addr_op, tracker);

    asm_.InlineBefore(op.instr,
        // Start with a racy read of `OwnershipTracker::type_id`. This allows
        // us to optimize the common case, which is that type = 0 (which is
        // reserved for unwatched memory).
        "CMP m16 [%0 + 6], i8 0;"
        "JZ l %2;"

        // Racy check that we don't own the cache line. Compare only the low
        // order 32 bits.
        "MOV r64 %1, m64 FS:[0];"
        "CMP m32 [%0], r32 %1;"
        "JZ l %2;"

        // Okay, we might be taking ownership, or detecting contention. So,
        // we'll add ourselves to the shadow and pull out the old value.
        // Because of user space addresses have all 16 high order bits as 0,
        // we'll end up marking the shadow as unwatched. If in
        // `InstrumentContention` we detect that we should take ownership,
        // then we'll re-watch the memory.
        "XCHG m64 [%0], r64 %1;"_x86_64);

    op.instr->InsertBefore(
        // We've detected some kind of contention; we'll call out to a generic
        // routine to instrument it.
        lir::InlineFunctionCall(op.block, InstrumentContention,
                                tracker, mem_access_op, op.native_addr_op));

    asm_.InlineBefore(op.instr,
        // Done, fall-through to instruction.
        "LABEL %2:"_x86_64);
  }
};

// Initialize the `data_collider` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<Malcontent>("malcontent", {"wrap_func", "stack_trace",
                                                    "shadow_memory"});
}

#endif  // GRANARY_WHERE_user
