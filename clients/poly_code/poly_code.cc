/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/wrap_func/wrap_func.h"

#include "generated/clients/poly_code/offsets.h"

using namespace granary;

// Associate types with memory.
WRAP_NATIVE_FUNCTION(libc, malloc, (void *), (size_t num_bytes)) {
  auto malloc = WRAPPED_FUNCTION;
  auto ret_address = NATIVE_RETURN_ADDRESS;

  os::Log("malloc(%lu) at %p\n", num_bytes, ret_address);

  return malloc(num_bytes);
}

// Simple tool for static and dynamic basic block counting.
class PolyCode : public InstrumentationTool {
 public:
  virtual ~PolyCode(void) = default;

  virtual void Init(InitReason) {
    RegisterFunctionWrapper(&WRAP_FUNC_libc_malloc);
  }
};

// Initialize the `poly_code` tool.
GRANARY_ON_CLIENT_INIT() {
  RegisterInstrumentationTool<PolyCode>("poly_code",
                                        {"wrap_func", "watchpoints"});
}
