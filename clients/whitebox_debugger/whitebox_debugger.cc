/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>
#include <elf.h>

#ifdef GRANARY_WHERE_user
# include "clients/user/syscall.h"
#endif  // GRANARY_WHERE_user

using namespace granary;

#define ELF_OFFSET(offset, dest_type) \
  reinterpret_cast<dest_type *>( \
      reinterpret_cast<uint64_t>(header) + offset)

namespace {

// Tells the tool about a new set of probes to monitor.
static void AddProbes(const Probe *probes, uint64_t num_probes) {
  os::Log(os::LogOutput, "Found %lu probes starting at %p!\n", num_probes,
          probes);
}



#ifdef GRANARY_WHERE_user

static __thread bool is_mmap = false;
static __thread uint64_t mmap_size = 0;
static const auto kBadMmapAddr = static_cast<uintptr_t>(-1L);

static void FindMemoryMap(void *, SystemCallContext context) {
  if ((is_mmap = __NR_mmap == context.Number())) {
    mmap_size = context.Arg1();
  }
}

static void FindELFMMap(void *, SystemCallContext context) {
  if (!is_mmap) return;
  is_mmap = false;

  auto mmap_addr = context.ReturnValue();
  if (kBadMmapAddr == mmap_addr) return;

  auto str = reinterpret_cast<const char *>(mmap_addr);
  if (0 != memcmp(ELFMAG, str, SELFMAG)) return;

  FindGranaryProbes(reinterpret_cast<const Elf64_Ehdr *>(mmap_addr), mmap_size);
}

#endif  // GRANARY_WHERE_user

}  // namespace

// Tool that helps user-space instrumentation work.
class WhiteboxDebugger : public InstrumentationTool {
 public:
  virtual ~WhiteboxDebugger(void) = default;
  virtual void Init(InitReason) {
#ifdef GRANARY_WHERE_user
    AddSystemCallEntryFunction(FindMemoryMap);
    AddSystemCallExitFunction(FindELFMMap);
#endif  // GRANARY_WHERE_user

    for (auto module : os::LoadedModules()) {
      GRANARY_UNUSED(module);

      // TODO(pag): Need a module->BaseAddress method. Perhaps need an
      //            iterator over non-executable ranges of a module. That could
      //            be easier, but then would require the iterator to hold
      //            a read lock (not too bad with RAII).
      // TODO(pag): Need a module->HasExecutableMemory method, as a quick
      //            check?
      // TODO(pag): Need to start *properly* tracking all module memory, not
      //            just executable memory. That way, we can check if some
      //            (non-executable) base address of a module looks like it's
      //            an ELF, and we can find probes in already-loaded modules
      //            (e.g. the main executable, the kernel, etc.).
    }
  }
};

// Initialize the `whitebox_debugger` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<WhiteboxDebugger>(
      "whitebox_debugger", {"watchpoints"});
})


