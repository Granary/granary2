/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"

#include "os/module.h"
#include "os/linux/kernel/module.h"

namespace granary {
namespace os {
extern "C" {
extern LinuxKernelModule *GRANARY_KERNEL_MODULES;
}  // extern C

typedef LinkedListIterator<LinuxKernelModule> LinuxKernelModuleIterator;

namespace {

// Get the module kind based on information already present in `mod` or based on
// the module's name.
static ModuleKind GetModuleKind(LinuxKernelModule *mod) {
  switch (mod->kind) {
    case LinuxKernelModule::GRANARY_MODULE:
      return ModuleKind::GRANARY;

    case LinuxKernelModule::KERNEL_MODULE:
      if (StringsMatch(GRANARY_NAME_STRING, mod->name)) {
        return ModuleKind::GRANARY;
      } else {
        return ModuleKind::KERNEL_MODULE;
      }

    case LinuxKernelModule::KERNEL:
      return ModuleKind::KERNEL;
  }
}

}  // namespace

// Find and register all built-in modules.
void ModuleManager::RegisterAllBuiltIn(void) {
  for (auto mod : LinuxKernelModuleIterator(GRANARY_KERNEL_MODULES)) {
    if (!mod->seen_by_granary) {
      auto module = new Module(GetModuleKind(mod), mod->name);
      mod->seen_by_granary = 1;
      module->AddRange(mod->core_text_begin, mod->core_text_end,
                       0, MODULE_EXECUTABLE | MODULE_READABLE);
      Register(module);
    }
  }
}

}  // namespace os
}  // namespace granary
