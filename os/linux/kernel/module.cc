/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/container.h"
#include "granary/base/list.h"

#include "os/module.h"
#include "os/linux/kernel/module.h"

namespace granary {
namespace os {
extern "C" {
LinuxKernelModule *granary_kernel_modules(nullptr);
}  // extern C

extern Container<ModuleManager> global_module_manager;

typedef LinkedListIterator<LinuxKernelModule> LinuxKernelModuleIterator;

namespace {

// Get the module kind based on information already present in `mod` or based on
// the module's name.
static ModuleKind ModuleKind(LinuxKernelModule *mod) {
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

// Update a module manager with a new module.
static void UpdateModule(LinuxKernelModule *mod) {
  auto module = reinterpret_cast<Module *>(mod->module);
  if (!module) {
    GRANARY_ASSERT(nullptr == ModuleByName(mod->name));
    module = new Module(ModuleKind(mod), mod->name);
    if (mod->exception_tables.start && mod->exception_tables.stop &&
        mod->exception_tables.start <= mod->exception_tables.stop) {
      module->where_data = &(mod->exception_tables);
    }
    mod->module = module;
    manager->Register(module);
  } else {
    // TODO(pag): Address issue #16 as it relates to unloading of module
    //            `.init` sections.
    module->RemoveRanges();
  }
  if (mod->core_text_begin && mod->core_text_end) {
    module->AddRange(mod->core_text_begin, mod->core_text_end,
                     0, MODULE_EXECUTABLE | MODULE_READABLE);
  }
  if (mod->init_text_begin && mod->init_text_end) {
    module->AddRange(mod->init_text_begin, mod->init_text_end,
                     0, MODULE_EXECUTABLE | MODULE_READABLE);
  }
}

}  // namespace

// Find and register all built-in modules.
void ModuleManager::RegisterAllBuiltIn(void) {
  for (auto mod : LinuxKernelModuleIterator(granary_kernel_modules)) {
    UpdateModule(this, mod);
  }
}

// Find and register all built-in modules.
void ModuleManager::ReRegisterAllBuiltIn(void) {
  RegisterAllBuiltIn();
}

extern "C" {
void NotifyModuleStateChange(LinuxKernelModule *mod) {
  UpdateModule(mod);
}
}  // extern C
}  // namespace os
}  // namespace granary
