/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

GRANARY_USING_NAMESPACE granary;

#include "clients/wrap_func/client.h"
#include "generated/clients/count_malloc_free/offsets.h"

#ifdef GRANARY_WHERE_user

namespace {
// Counter for malloc.
std::atomic<uint64_t> gCountMalloc(ATOMIC_VAR_INIT(0));
// Counter for free.
std::atomic<uint64_t> gCountFree(ATOMIC_VAR_INIT(0));

#define GET_ALLOCATOR(name) \
  auto name = WRAPPED_FUNCTION

// Make a wrapper for an allocator.
#define ALLOC_WRAPPER(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void *), (size_t size)) { \
      GET_ALLOCATOR(name); \
      gCountMalloc.fetch_add(1); \
      return name(size); \
    }

ALLOC_WRAPPER(libc, malloc)
ALLOC_WRAPPER(libc, valloc)
ALLOC_WRAPPER(libc, pvalloc)
ALLOC_WRAPPER(libstdcxx, _Znwm)
ALLOC_WRAPPER(libstdcxx, _Znam)
ALLOC_WRAPPER(libcxx, _Znwm)
ALLOC_WRAPPER(libcxx, _Znam)

// Make a wrapper fow an allocator with two parameters.
#define ALLOC_WRAPPER2(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void *), (size_t a, size_t b)) { \
      GET_ALLOCATOR(name); \
      gCountMalloc.fetch_add(1); \
      return name(a, b); \
    }

ALLOC_WRAPPER2(libc, calloc);
ALLOC_WRAPPER2(libc, realloc);
ALLOC_WRAPPER2(libc, aligned_alloc);
ALLOC_WRAPPER2(libc, memalign);

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, posix_memalign, (int), (void **addr_ptr,
                                                   size_t align, size_t size)) {
  GET_ALLOCATOR(posix_memalign); \
  gCountMalloc.fetch_add(1); \
  return posix_memalign(addr_ptr, align, size); \
}

// Deallocate some memory.
#define FREE_WRAPPER(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void), (void *ptr)) { \
      GET_ALLOCATOR(name); \
      gCountFree.fetch_add(1); \
      return name(ptr); \
    }

FREE_WRAPPER(libc, free)
FREE_WRAPPER(libstdcxx, _ZdlPv)
FREE_WRAPPER(libstdcxx, _ZdaPv)
FREE_WRAPPER(libcxx, _ZdlPv)
FREE_WRAPPER(libcxx, _ZdaPv)

#endif  // GRANARY_WHERE_user
}  // namespace

// Simple tool for memory malloc and free counting.
class Count_Malloc_Free : public InstrumentationTool {
 public:
  virtual ~Count_Malloc_Free(void) = default;

  static void Init(InitReason reason) {
    if (kInitThread == reason) return;

#ifdef GRANARY_WHERE_user
    // Wrap libc.
    AddFunctionWrapper(&WRAP_FUNC_libc_malloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_valloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_pvalloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_aligned_alloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_memalign);
    AddFunctionWrapper(&WRAP_FUNC_libc_posix_memalign);
    AddFunctionWrapper(&WRAP_FUNC_libc_calloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_realloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_free);

    // Wrap GNU's C++ standard library.
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__Znwm);
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__Znam);
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__ZdlPv);
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__ZdaPv);

    // Wrap clang's C++ standard library.
    AddFunctionWrapper(&WRAP_FUNC_libcxx__Znwm);
    AddFunctionWrapper(&WRAP_FUNC_libcxx__Znam);
    AddFunctionWrapper(&WRAP_FUNC_libcxx__ZdlPv);
    AddFunctionWrapper(&WRAP_FUNC_libcxx__ZdaPv);
#endif  // GRANARY_WHERE_user
  }

  static void Exit(ExitReason reason) {
    if (kExitThread == reason) return;

    os::Log("Counter for malloc: %lu\nCounter for free: %lu\n",
            gCountMalloc.load(), gCountFree.load());
  }
};

// Initialize the `count_malloc_free` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<Count_Malloc_Free>("count_malloc_free", {"wrap_func"});
}
