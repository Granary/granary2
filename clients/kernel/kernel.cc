/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

GRANARY_DEFINE_string(attach_to_syscalls, "*",
    "Comma-separated list of specific system call numbers "
    "to which Granary should be attached. The default "
    "value is `*`, which means that Granary should attach to "
    "all system calls.",

    "kernel");

// TODO:
//  1) Make sure all exception tables are sorted.
//      --> might be able to enforce this in AllocModule, but be wary of where
//          the kernel sorts the tables, as it might be during a different
//          notifier state.
//      --> might be able to kmalloc and memcpy my own versions of the kernel's
//          extables. That could be best solution.
//  2) Look into the extable_ex or whatever. There were macros that used a
//     different fixup table which displaced the recovering address by a
//     different amount.
//  3) Work on only instrumenting a set of system calls.
//  4) Figure out why some extable entries point to weird code. Wtf is going
//     on with that?

static TinySet<int, 10> syscalls;

// Tool that implements several kernel-space special cases for instrumenting
// common binaries.
class KernelSpaceInstrumenter : public InstrumentationTool {
 public:
  virtual ~KernelSpaceInstrumenter(void) = default;

  void InstrumentSyscall(BlockFactory *factory, CompensationBasicBlock *block,
                         int syscall) {
    if ('*' == FLAG_attach_to_syscalls[0] || syscalls.Contains(syscall)) {
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
          if (1 == DeFormat(syscall_str, "%d", &syscall_num) &&
              0 <= syscall_num) {
            syscalls.Add(syscall_num);
          }
        });
  }
  RegisterInstrumentationTool<KernelSpaceInstrumenter>("kernel");
})
