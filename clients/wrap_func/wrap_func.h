/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_WRAP_FUNC_WRAP_FUNC_H_
#define CLIENTS_WRAP_FUNC_WRAP_FUNC_H_

#include <granary.h>

enum WrapperAction {
  // Replace a function-to-be-wrapped with the wrapper itself.
  REPLACE_WRAPPED_FUNCTION,

  // Pass the native version of the function through to the wrapper.
  PASS_NATIVE_WRAPPED_FUNCTION,

  // Pass the instrumented version of the function through to the wrapper.
  PASS_INSTRUMENTED_WRAPPED_FUNCTION
};

struct FunctionWrapper {
 public:
  FunctionWrapper *next;

  // Name of the symbol being wrapped.
  const char * const function_name;

  // Name of the module to which the function being wrapped belongs.
  const char * const module_name;

  // Offset of the function to be wrapped from within its module.
  //
  // Note: Not constant just in case we need to dynamically determine the
  //       offset of the symbol based on the module.
  uint64_t module_offset;

  // The wrapper function.
  const granary::AppPC wrapper_pc;

  // How should we handle the function being wrapped? Do we replace it,
  // pass it through (uninstrumented), or pass it through instrumented?
  const WrapperAction action;
};

// Register a function wrapper with the wrapper tool.
void RegisterFunctionWrapper(FunctionWrapper *wrapper);

#define WRAP_INSTRUMENTED_FUNCTION(lib_name, func_name, ret_type, param_types) \
    struct GRANARY_CAT(WRAPPER_OF_, func_name) { \
      static typename granary::Identity<GRANARY_SPLAT(ret_type)>::Type \
      wrap param_types; \
      \
      typedef decltype(wrap) WrappedFunctionType; \
    }; \
    static FunctionWrapper GRANARY_CAT(WRAP_, func_name) = { \
        .next = nullptr, \
        .function_name = GRANARY_TO_STRING(func_name), \
        .module_name = lib_name, \
        .module_offset = GRANARY_CAT(SYMBOL_OFFSET_, func_name), \
        .wrapper_pc = reinterpret_cast<granary::AppPC>( \
            GRANARY_CAT(WRAPPER_OF_, func_name)::wrap), \
        .action = PASS_INSTRUMENTED_WRAPPED_FUNCTION \
    }; \
    \
    typename granary::Identity<GRANARY_SPLAT(ret_type)>::Type \
    GRANARY_CAT(WRAPPER_OF_, func_name)::wrap param_types

#define WRAPPED_FUNCTION \
  reinterpret_cast<WrappedFunctionType *>(({ \
      register uintptr_t __r10 asm("r10"); \
      asm("movq %%r10, %0;" : "=r"(__r10)); \
      __r10; }))

#define WRAP_NATIVE_FUNCTION()

#define WRAP_REPLACE_FUNCTION()

#endif  // CLIENTS_WRAP_FUNC_WRAP_FUNC_H_
