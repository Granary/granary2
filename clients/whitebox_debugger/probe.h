/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_WHITEBOX_DEBUGGER_PROBE_H_
#define CLIENTS_WHITEBOX_DEBUGGER_PROBE_H_

#include <granary.h>

namespace wdb {

// Different categories of probes used by whitebox debugging.
enum ProbeCategory : int32_t {
  PROBE_WATCHPOINT
};

// Different kinds of watchpoint probes.
enum WatchpointKind : int32_t {
  READ_WATCHPOINT       = 1 << 0,
  WRITE_WATCHPOINT      = 1 << 1,
  READ_WRITE_WATCHPOINT = READ_WATCHPOINT | WRITE_WATCHPOINT
};

// Defines a generic Granary probe used in whitebox debugging.
struct Probe {
  const ProbeCategory category;
  union {
    const WatchpointKind watchpoint;
  };
  const granary::AppPC callback;
  const Probe * const next;
};

static_assert((2 * sizeof(int64_t)) == sizeof(Probe),
    "Invalid structure packing of `struct Probe`.");

// List of probes.
typedef granary::LinkedListSlice<const Probe> ProbeList;

// Tells WDB about some new probes.
void AddProbes(ProbeList probes);

// Returns a list of probes that should apply to the range `(prev_pc, pc]` of
// program counters. A range is used to find probes because Granary will
// sometimes elide certain instructions (e.g. NO-OPs).
const ProbeList FindProbes(granary::AppPC prev_pc, granary::AppPC pc);

}  // namespace wdb

#endif  // CLIENTS_WHITEBOX_DEBUGGER_PROBE_H_
