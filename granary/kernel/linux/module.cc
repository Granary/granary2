/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"

#include "granary/kernel/linux/module.h"

#include "granary/client.h"
#include "granary/module.h"

namespace granary {

extern "C" {
extern KernelModule *GRANARY_KERNEL_MODULES;
}  // extern C

typedef LinkedListIterator<KernelModule> KernelModuleIterator;

namespace {

// Get the module kind based on information already present in `mod` or based on
// the module's name.
static ModuleKind GetModuleKind(KernelModule *mod) {
  switch (mod->kind) {
    case KernelModule::GRANARY_MODULE:
      return ModuleKind::GRANARY;

    case KernelModule::KERNEL_MODULE:
      if (StringsMatch(GRANARY_NAME_STRING, mod->name)) {
        return ModuleKind::GRANARY;
      } else if (ClientIsRegistered(mod->name)) {
        return ModuleKind::GRANARY_CLIENT;
      } else {
        return ModuleKind::KERNEL_MODULE;
      }

    case KernelModule::KERNEL:
      return ModuleKind::KERNEL;
  }
}

}  // namespace

// Find all built-in modules. In user space, this will go and find things like
// libc. In kernel space, this will identify already loaded modules.
void ModuleManager::RegisterAllBuiltIn(void) {
  for (auto mod : KernelModuleIterator(GRANARY_KERNEL_MODULES)) {
    auto module = new Module(GetModuleKind(mod), mod->name);
    mod->seen_by_granary = 1;
    module->AddRange(mod->core_text_begin, mod->core_text_end,
                     0, MODULE_EXECUTABLE | MODULE_READABLE);
    Register(module);
  }
}

}  // namespace granary
