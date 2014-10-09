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

// Different categories of probes used by whitebox debugging.
enum ProbeCategory : int32_t {
  PROBE_WATCHPOINT
};

// Different kinds of watchpoints.
enum WatchpointKind : int32_t {
  READ_WATCHPOINT       = 1 << 0,
  WRITE_WATCHPOINT      = 1 << 1,
  READ_WRITE_WATCHPOINT = READ_WATCHPOINT | WRITE_WATCHPOINT
};

// Defines a generic Granary probe used in whitebox debugging.
struct Probe {
  ProbeCategory category;
  union {
    WatchpointKind watchpoint;
  };
  AppPC callback;
};

static_assert((2 * sizeof(int64_t)) == sizeof(Probe),
    "Invalid structure packing of `struct Probe`.");

// Tells the tool about a new set of probes to monitor.
static void AddProbes(const Probe *probes, uint64_t num_probes) {
  os::Log(os::LogOutput, "Found %lu probes starting at %p!\n", num_probes,
          probes);
}

// Looks for Granary probes within an ELF file.
static void FindGranaryProbes(const Elf64_Ehdr *header, uint64_t obj_size) {
  GRANARY_ASSERT(header->e_shoff < obj_size);
  GRANARY_ASSERT(sizeof(Elf64_Shdr) == header->e_shentsize);
  GRANARY_ASSERT((header->e_ehsize + header->e_shentsize * header->e_shnum) <
                 obj_size);

  auto num_sections = static_cast<uint64_t>(header->e_shnum);
  auto section_headers = ELF_OFFSET(header->e_shoff, Elf64_Shdr);

  GRANARY_ASSERT(section_headers[header->e_shstrndx].sh_offset < obj_size);
  auto header_names = ELF_OFFSET(section_headers[header->e_shstrndx].sh_offset,
                                 const char);

  for (auto i = 0UL; i < num_sections; ++i) {
    auto section_name = header_names + section_headers[i].sh_name;
    if (!StringsMatch(".granary_probes", section_name)) continue;
    AddProbes(reinterpret_cast<Probe *>(section_headers[i].sh_addr),
              section_headers[i].sh_size / sizeof(Probe));
    break;
  }
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


