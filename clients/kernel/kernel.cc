/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#ifdef GRANARY_WHERE_kernel

using namespace granary;

GRANARY_DEFINE_string(attach_to_syscalls, "*",
    "Comma-separated list of specific system call numbers "
    "to which Granary should be attached. The default "
    "value is `*`, which means that Granary should attach to "
    "all system calls.",

    "kernel");

static TinySet<int, 10> exclude_syscalls;
static TinySet<int, 10> include_syscalls;

// Tool that implements several kernel-space special cases for instrumenting
// common binaries.
class KernelSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~KernelSpaceInstrumenter(void) = default;

  void InstrumentSyscall(BlockFactory *factory, CompensationBasicBlock *block,
                         int syscall) {
    // Allow the user to specify `--attach_to_syscalls=*,-1,-2` to mean all
    // system calls except 1 and 2.
    if ('*' == FLAG_attach_to_syscalls[0]) {
      if (!exclude_syscalls.Contains(syscall)) return;

    // If the the specific system call is specified, then don't request that
    // execution go native.
    } else if (include_syscalls.Contains(syscall)) {
      return;
    }
    for (auto succ : block->Successors()) {
      factory->RequestBlock(succ.block, REQUEST_NATIVE);
    }
  }

  virtual void InstrumentEntryPoint(BlockFactory *factory,
                                    CompensationBasicBlock *block,
                                    EntryPointKind kind, int category) {
    if (ENTRYPOINT_KERNEL_SYSCALL == kind) {
      InstrumentSyscall(factory, block, category);
    }
  }
};

// Initialize the `kernel` tool.
GRANARY_CLIENT_INIT({

  // TODO(pag): Distinguish between client load and tool init.
  if (HAS_FLAG_attach_to_syscalls) {
    ForEachCommaSeparatedString<4>(
        FLAG_attach_to_syscalls,
        [] (const char *syscall_str) {
          int syscall_num(-1);
          if ('-' == syscall_str[0]) {
            if (1 == DeFormat(&(syscall_str[1]), "%d", &syscall_num))
              exclude_syscalls.Add(syscall_num);
          } else if (1 == DeFormat(syscall_str, "%d", &syscall_num)) {
            include_syscalls.Add(syscall_num);
          }
        });
  }
  RegisterInstrumentationTool<KernelSpaceInstrumenter>("kernel");
})

#endif  // GRANARY_WHERE_kernel
