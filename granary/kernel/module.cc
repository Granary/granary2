/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"
#include "granary/kernel/module.h"
#include "granary/module.h"
#include "granary/tool.h"

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
    case KernelModule::KERNEL_MODULE_GRANARY:
      return ModuleKind::GRANARY;

    case KernelModule::KERNEL_MODULE_TOOL:
      return ModuleKind::GRANARY_TOOL;

    case KernelModule::KERNEL_MODULE:
      if (FindTool(mod->name)) {
        return ModuleKind::GRANARY_TOOL;
      } else {
        return ModuleKind::KERNEL_MODULE;
      }

    case KernelModule::KERNEL:
      return ModuleKind::KERNEL;
  }
}

}  // namespace

// Initialize the module tracker.
void InitModules(InitKind) {
  for (auto mod : KernelModuleIterator(GRANARY_KERNEL_MODULES)) {
    auto module = new Module(GetModuleKind(mod), mod->name);
    mod->seen_by_granary = 1;
    module->AddRange(
        mod->core_text_begin,
        mod->core_text_end,
        0,
        internal::MODULE_EXECUTABLE | internal::MODULE_READABLE);
    RegisterModule(module);
  }
}

}  // namespace granary
