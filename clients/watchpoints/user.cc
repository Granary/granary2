/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifdef GRANARY_WHERE_user

#include "clients/util/types.h"  // Needs to go first.
#include "clients/user/syscall.h"
#include "clients/watchpoints/watchpoints.h"

namespace {

static void UnwatchSyscallArg(uint64_t &arg) {
  if (IsTaintedAddress(arg)) {
    arg = UntaintAddress(arg);
  }
}

// Prevent watched addresses from being passed to system calls.
static void GenericWrapSyscallArgs(SystemCallContext ctx) {
  UnwatchSyscallArg(ctx.Arg0());
  UnwatchSyscallArg(ctx.Arg1());
  UnwatchSyscallArg(ctx.Arg2());
  UnwatchSyscallArg(ctx.Arg3());
  UnwatchSyscallArg(ctx.Arg4());
  UnwatchSyscallArg(ctx.Arg5());
}

// Create implementations of the wrappers.

#define WRAP_STRUCT_PFIELD(field_name) \
  if (IsTaintedAddress(s->field_name)) { \
    s->field_name = UntaintAddress(s->field_name); \
  }

#define WRAP_STRUCT(type_name, ...) \
  static void UnwatchStructFields(type_name *&s) { \
    if (IsTaintedAddress(s)) { \
      s = UntaintAddress(s); \
    } \
    __VA_ARGS__ ; \
  }

#define WRAP_SYSCALL_ARG_PSTRUCT(i, type_name) \
  UnwatchStructFields(reinterpret_cast<type_name *&>(ctx.Arg ## i()))

#define WRAP_SYSCALL_ARG_POINTER(i) \
  UnwatchSyscallArg(ctx.Arg ## i())

#define NO_WRAP_SYSCALL(name)
#define GENERIC_WRAP_SYSCALL(name)
#define WRAP_SYSCALL(name, ...) \
  static void name ## WrapSyscallArgs(SystemCallContext ctx) { \
    __VA_ARGS__ ; \
  }

#include "generated/clients/watchpoints/syscall.h"

#undef WRAP_STRUCT_PFIELD
#undef WRAP_STRUCT
#undef WRAP_SYSCALL_ARG_PSTRUCT
#undef WRAP_SYSCALL_ARG_POINTER
#undef NO_WRAP_SYSCALL
#undef GENERIC_WRAP_SYSCALL
#undef WRAP_SYSCALL

// Link the wrappers to the syscall numbers in a table.
typedef void (*SyscallWrapper)(SystemCallContext ctx);
static const SyscallWrapper kSyscallWrappers[] = {
#define WRAP_STRUCT(...)
#define NO_WRAP_SYSCALL(name) [__NR_ ## name] = nullptr,
#define GENERIC_WRAP_SYSCALL(name) [__NR_ ## name] = GenericWrapSyscallArgs,
#define WRAP_SYSCALL(name, ...) [__NR_ ## name] = name ## WrapSyscallArgs,
#include "generated/clients/watchpoints/syscall.h"
#undef WRAP_STRUCT
#undef NO_WRAP_SYSCALL
#undef GENERIC_WRAP_SYSCALL
#undef WRAP_SYSCALL
};

enum {
  NUM_SYSCALLS = sizeof kSyscallWrappers / sizeof kSyscallWrappers[0]
};

// Prevent watched addresses from being passed to system calls.
static void UnwatchSyscallArgs(void *, SystemCallContext ctx) {
  const auto nr = ctx.Number();
  if (nr < NUM_SYSCALLS) {
    if (auto wrapper = kSyscallWrappers[nr]) {
      wrapper(ctx);
    }
  }
}

}  // namespace

#endif  // GRANARY_WHERE_user

void InitUserWatchpoints(void) {
  GRANARY_IF_USER(AddSystemCallEntryFunction(UnwatchSyscallArgs));
}
