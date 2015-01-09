/* Copyright 2015 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_CONSTRUCTOR_H_
#define GRANARY_BASE_CONSTRUCTOR_H_

#include "granary/base/base.h"

namespace granary {

// Defines a dummy class that calls a specific constructor and destructor
// function. This is used in conjunction with `GRANARY_EARLY_GLOBAL` or
// `GRANARY_GLOBAL` to make sure something makes it into the `.init_array` and
// `.fini_array`.
//
// The intended usage is for initializing static member variables of classes
// that need an `init_priority` attribute, but cannot have one because they are
// not file-scoped. For example:
//
//    template <...>
//    class Foo {
//     private:
//
//      __attribute__((noinline, used))
//      static void Init(void) {
//        kBarConstructor.PreserveSymbols();
//        bar.Construct(...);  // Complex constructor.
//      }
//
//      __attribute__((noinline, used))
//      static void Exit(void) {
//        kBarConstructor.PreserveSymbols();
//        bar.Destroy();
//        GRANARY_USED();
//      }
//
//      static Container<Bar> gBar;  // Will end up placing `gBar` in `.bss`.
//      static Constructor<Init, Exit> kBarConstructor;
//
//    };
//
// Note: This very unusual seeming setup is needed because we use the following
//       compiler options: `-fno-threadsafe-statics` and
//       `-Xclang -fforbid-guard-variables`.
template <void (*constructor)(void), void (*destructor)(void)>
struct Constructor {
 public:
  __attribute__((noinline, used))
  void PreserveSymbols(void) const {
    GRANARY_USED(kInit[0]);
    GRANARY_USED(kFini[0]);
  }

  static void (* const kInit[])() __attribute__((section(".init_array.102"),
                                                 aligned(sizeof(void *))));
  static void (* const kFini[])() __attribute__((section(".fini_array.102"),
                                                 aligned(sizeof(void *))));
};

template <void (*constructor)(void), void (*destructor)(void)>
void (* const Constructor<constructor, destructor>::kInit[])()
    __attribute__((used)) = { constructor };

template <void (*constructor)(void), void (*destructor)(void)>
void (* const Constructor<constructor, destructor>::kFini[])()
    __attribute__((used)) = { destructor };

}  // namespace granary

#endif  // GRANARY_BASE_CONSTRUCTOR_H_
