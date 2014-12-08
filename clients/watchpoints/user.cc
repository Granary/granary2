/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

#ifdef GRANARY_WHERE_user
#include "clients/user/client.h"
#include "clients/watchpoints/client.h"

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

#define WRAP_STRUCT_ASTRUCT(field_name,length_name) \
  WRAP_STRUCT_PFIELD(field_name) \
  for (decltype(s->length_name) i__ = 0; i__ < s->length_name; ++i__) { \
    UnwatchStructFields(&(s->field_name[i__])); \
  }

#define WRAP_STRUCT(type_name, ...) \
  static void UnwatchStructFields(type_name *s) { \
    __VA_ARGS__ ; \
  } \
  __attribute__((used)) \
  static void UnwatchStruct(type_name *&s) { \
    if (IsTaintedAddress(s)) { \
      s = UntaintAddress(s); \
    } \
    UnwatchStructFields(s); \
  }

#define WRAP_SYSCALL_ARG_PSTRUCT(i, type_name) \
  UnwatchStruct(reinterpret_cast<type_name *&>(ctx.Arg ## i()))

#define WRAP_SYSCALL_ARG_ASTRUCT(i, j, type_name, counter_type_name) \
  do { \
    counter_type_name index = 0; \
    auto max_index = static_cast<counter_type_name>(ctx.Arg ## j()); \
    auto &base_addr(ctx.Arg ## i()); \
    if (IsTaintedAddress(base_addr)) { \
      base_addr = UntaintAddress(base_addr); \
    } \
    auto &base(reinterpret_cast<type_name *&>(base_addr)); \
    for (; index < max_index; ++index) { \
      UnwatchStructFields(&(base[index])); \
    } \
  } while (0)

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
#undef WRAP_STRUCT_ASTRUCT
#undef WRAP_STRUCT
#undef WRAP_SYSCALL_ARG_PSTRUCT
#undef WRAP_SYSCALL_ARG_ASTRUCT
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
static void UnwatchSyscallArgs(SystemCallContext ctx) {
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
