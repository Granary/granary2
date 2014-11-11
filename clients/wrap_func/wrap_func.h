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

  // The IF of this wrapper. This exists to distinguish between multiple
  // wrappers of the same instrumented function. Wrapper IDs are not unique
  // across different functions.
  uint8_t id;

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

// Gives access to the function being wrapped. This assumes that the wrapper
// action is either `PASS_INSTRUMENTED_WRAPPED_FUNCTION` or that it is
// `PASS_NATIVE_WRAPPED_FUNCTION`.
#define WRAPPED_FUNCTION \
  reinterpret_cast<WrappedFunctionType *>(({ \
      register uintptr_t __r10 asm("r10"); \
      asm("movq %%r10, %0;" : "=r"(__r10)); \
      __r10; }))

// Gives access to the return (native) address associated with the function
// being wrapped.
#define NATIVE_RETURN_ADDRESS \
  (({ register uintptr_t __r11 asm("r11"); \
      asm("movq %%r11, %0;" : "=r"(__r11)); \
      __r11; }))

// True definition of the wrappers. It is mostly designed to be easy on the
// eyes and the macro expander, and so goes extra far to allow the function
// body of the wrapper to be *outside* of the wrapper.
//
// What we do here:
//    1)  Define a struct that has a single static method (the wrapper) and
//        a typedef for `WrappedFunctionType`, that will be visible from within
//        the static method, thus allowing our `WRAPPED_FUNCTION` macro to
//        do the right type cast when we want access to the native/instrumented
//        function being wrapped.
//    2)  Define a static `WRAP_FUNC_foo` for function `foo` variable that
//        represents our wrapper.
//
//        Note: Eclipse has trouble syntax highlighting the inline definition
//              of the struct so we elide that in the IDE.
//
//    3)  Defines the beginning of the implementation of the static method
//        of the struct declared in (1). The body of the function has to
//        appear immediately after the macro.
//
// Note: `granary::Identity` is used for return types in the event that a
//       function being wrapped returns a function pointer.
#define __WRAP_FUNCTION(act, lib_name, func_name, ret_type, param_types) \
    struct WRAPPER_OF_ ## lib_name ## _ ## func_name { \
      static typename granary::Identity<GRANARY_SPLAT(ret_type)>::Type \
      wrap param_types; \
      \
      typedef decltype(wrap) WrappedFunctionType; \
    }; \
    \
    static FunctionWrapper WRAP_FUNC_ ## lib_name ## _ ## func_name \
    GRANARY_IF_NOT_ECLIPSE( = { \
        .next = nullptr, \
        .id = 0, \
        .function_name = GRANARY_TO_STRING(func_name), \
        .module_name = GRANARY_TO_STRING(lib_name), \
        .module_offset = SYMBOL_OFFSET_ ## lib_name ## _ ## func_name, \
        .wrapper_pc = reinterpret_cast<granary::AppPC>( \
            WRAPPER_OF_ ## lib_name ## _ ## func_name::wrap), \
        .action = act \
    }); \
    \
    GRANARY_EXPORT_TO_INSTRUMENTATION \
    typename granary::Identity<GRANARY_SPLAT(ret_type)>::Type \
    WRAPPER_OF_ ## lib_name ## _ ## func_name::wrap param_types

// Action-specific wrapper macros.
#define WRAP_REPLACE_FUNCTION() \
    __WRAP_FUNCTION(REPLACE_WRAPPED_FUNCTION, lib_name, \
                    func_name, ret_type, param_types)

#define WRAP_INSTRUMENTED_FUNCTION(lib_name, func_name, ret_type, param_types) \
    __WRAP_FUNCTION(PASS_INSTRUMENTED_WRAPPED_FUNCTION, lib_name, \
                    func_name, ret_type, param_types)

#define WRAP_NATIVE_FUNCTION(lib_name, func_name, ret_type, param_types) \
    __WRAP_FUNCTION(PASS_NATIVE_WRAPPED_FUNCTION, lib_name, \
                    func_name, ret_type, param_types)

#endif  // CLIENTS_WRAP_FUNC_WRAP_FUNC_H_
