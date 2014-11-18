/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/util/types.h"  // Needs to go first.

#include <granary.h>

GRANARY_USING_NAMESPACE granary;

#include "clients/wrap_func/client.h"
#include "generated/clients/valgrind/offsets.h"

#ifdef GRANARY_WHERE_user
#ifdef GRANARY_WITH_VALGRIND

#define WRAP_ALLOCATOR(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void *), (size_t size)) { \
      auto name = WRAPPED_FUNCTION; \
      return name(size); \
    }

WRAP_ALLOCATOR(libc, malloc)
WRAP_ALLOCATOR(libc, valloc)
WRAP_ALLOCATOR(libc, pvalloc)
WRAP_ALLOCATOR(libstdcxx, _Znwm)
WRAP_ALLOCATOR(libstdcxx, _Znam)
WRAP_ALLOCATOR(libcxx, _Znwm)
WRAP_ALLOCATOR(libcxx, _Znam)

#define WRAP_ALLOCATOR2(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void *), (size_t a, size_t b)) { \
      auto name = WRAPPED_FUNCTION; \
      return name(a, b); \
    }

WRAP_ALLOCATOR2(libc, calloc);
WRAP_ALLOCATOR2(libc, realloc);
WRAP_ALLOCATOR2(libc, aligned_alloc);
WRAP_ALLOCATOR2(libc, memalign);

WRAP_NATIVE_FUNCTION(libc, posix_memalign, (int), (void **addr_ptr,
                                                   size_t align, size_t size)) {
  auto posix_memalign = WRAPPED_FUNCTION;
  return posix_memalign(addr_ptr, align, size);
}

#define WRAP_DEALLOCATOR(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void), (void *addr)) { \
      auto name = WRAPPED_FUNCTION; \
      name(addr); \
    }

WRAP_DEALLOCATOR(libc, free)
WRAP_DEALLOCATOR(libstdcxx, _ZdlPv)
WRAP_DEALLOCATOR(libstdcxx, _ZdaPv)
WRAP_DEALLOCATOR(libcxx, _ZdlPv)
WRAP_DEALLOCATOR(libcxx, _ZdaPv)

// Tool that helps user-space instrumentation work.
class ValgrindHelper : public InstrumentationTool {
 public:
  virtual ~ValgrindHelper(void) = default;

  virtual void Init(InitReason) {
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
  }
};

// Initialize the `valgrind` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<ValgrindHelper>("valgrind", {"wrap_func"});
}

#endif  // GRANARY_WITH_VALGRIND
#endif  // GRANARY_WHERE_user
