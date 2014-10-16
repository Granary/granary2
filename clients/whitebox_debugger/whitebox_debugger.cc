/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifdef GRANARY_WHERE_user
#if 0
#include <granary.h>

using namespace granary;

namespace {

// Different categories of probes used by whitebox debugging.
enum ProbeCategory : int32_t {
  PROBE_WATCHPOINT
};

// Different kinds of watchpoints.
enum WatchpointKind : int32_t {
  READ_WATCHPOINT       = 1 << 0,
  WRITE_WATCHPOINT      = 1 << 1,
  READ_WRITE_WATCHPOINT = READ_WATCHPOINT | WRITE_WATCHPOINT,
  REMOVE_WATCHPOINT     = 1 << 2
};

// Defines a generic Granary probe used in whitebox debugging.
struct Probe {
  ProbeCategory category;
  union {
    WatchpointKind watchpoint;
  };
  AppPC probe_pc;

  bool operator<(const Probe &that) const {
    return probe_pc < that.probe_pc;
  }
};

static_assert((2 * sizeof(int64_t)) == sizeof(Probe),
    "Invalid structure packing of `struct Probe`.");

enum {
  MAX_NUM_PROBES = 1024
};
static struct Probe probes[MAX_NUM_PROBES];

extern "C" {

extern const Probe granary_begin_probes[] __attribute__((weak));
extern const Probe granary_end_probes[] __attribute__((weak));

}  // extern C

union AddressWatchpoint {
  struct {
    AppPC read_callback;
    AppPC write_callback;
    uintptr_t base_address;
    void *meta_data;
  };
  struct {
    AddressWatchpoint *next;
    uintptr_t index;
  };
};

enum {
  MAX_NUM_WATCHPOINTS = 0x7FFFU
};

// Next unallocated watchpoint. When this exceeds `MAX_NUM_WATCHPOINTS`, we
// switch over to
static std::atomic<uintptr_t> next_watchpoint_index(ATOMIC_VAR_INIT(0UL));

// Free list (and associated lock) for allocating watchpoints.
static AddressWatchpoint *free_list = nullptr;
static SpinLock free_list_lock;

// Global watchpoints table. Lazily allocated by Granary's allocators so that
// it's hopefully close to the code cache.
static AddressWatchpoint *watchpoints = nullptr;

// Allocate a new watchpoint.
static AddressWatchpoint *AllocateWatchpoint(uintptr_t *index) {
  if (MAX_NUM_WATCHPOINTS >
      next_watchpoint_index.load(std::memory_order_relaxed)) {
    auto id = next_watchpoint_index.fetch_add(1);
    if (MAX_NUM_WATCHPOINTS > id) {
      *index = id;
      return &(watchpoints[id]);
    }
  }

  SpinLockedRegion locker(&free_list_lock);
  auto wp = free_list;
  if (wp) {
    *index = wp->index;
    free_list = wp->next;
  }
  return wp;
}

static void AddWatchpoint(uintptr_t *addr_ptr, void *meta, AppPC callback) {
  uintptr_t index(0);
  if (auto wp = AllocateWatchpoint(&index)) {
    wp->base_address = *addr;
    wp->meta_data = meta;
    wp->callback = callback;
  }
}

// Free a watchpoint.
static void FreeWatchpoint(uintptr_t *addr_ptr) {

}


static void InitWatchpoints(void) {
  auto num_bytes = sizeof(AddressWatchpoint) * MAX_NUM_WATCHPOINTS;
  auto num_pages = GRANARY_ALIGN_TO(num_bytes, arch::PAGE_SIZE_BYTES) /
                   arch::PAGE_SIZE_BYTES;
  watchpoints = AllocatePages();
}

static void CopyProbes(void) {
  auto begin = granary_begin_probes;
  auto end = granary_end_probes;
  memcpy(&(probes[0]), begin, static_cast<size_t>(end - begin));
  std::sort(probes, probes + MAX_NUM_PROBES);
}

}  // namespace

// Tool that helps user-space instrumentation work.
class WhiteboxDebugger : public InstrumentationTool {
 public:
  virtual ~WhiteboxDebugger(void) = default;
  virtual void Init(InitReason) {
    if (granary_begin_probes) {
      InitWatchpoints();
      CopyProbes();
    }
  }
};

// Initialize the `whitebox_debugger` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<WhiteboxDebugger>(
      "whitebox_debugger", {"watchpoints"});
})

#endif
#endif  // GRANARY_WHERE_user
