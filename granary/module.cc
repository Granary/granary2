/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"
#include "granary/breakpoint.h"
#include "granary/module.h"

namespace granary {

typedef LinkedListIterator<const detail::ModuleAddressRange>
        ModuleAddressRangeIterator;

typedef LinkedListIterator<Module> ModuleIterator;

namespace {
static std::atomic<Module *> MODULES(ATOMIC_VAR_INIT(nullptr));

// Find the address range that contains a particular program counter. Returns
// `nullptr` if no such range exists in the specified list.
static const detail::ModuleAddressRange *FindRange(
    const detail::ModuleAddressRange *ranges, AppProgramCounter pc) {
  auto addr = reinterpret_cast<uintptr_t>(pc);
  for (auto range : ModuleAddressRangeIterator(ranges)) {
    if (detail::MODULE_EXECUTABLE & range->perms) {
      if (range->begin_addr <= addr && addr < range->end_addr) {
        return range;
      }
    }
  }
  return nullptr;
}

}  // namespace

// Return a module offset object for a program counter (that is expected to
// be contained inside of the module). If the program counter is not part of
// the module then the returned object is all nulled.
ModuleOffset Module::OffsetOf(AppProgramCounter pc) const {
  auto range = FindRange(&ranges, pc);
  if (!range) {
    return ModuleOffset(nullptr, 0);
  }
  auto addr = reinterpret_cast<uintptr_t>(pc);
  return ModuleOffset(this, range->begin_offset + (addr - range->begin_addr));
}

// Returns true if a module contains the code address `pc`, and if that code
// address is marked as executable.
bool Module::Contains(AppProgramCounter pc) const {
  return nullptr != FindRange(&ranges, pc);
}

// Returns the kind of this module.
ModuleKind Module::Kind(void) const {
  return kind;
}

// Returns the name of this module.
const char *Module::Name(void) const {
  return &(name[0]);
}

// Find a module given a program counter.
const Module *FindModule(AppProgramCounter pc) {
  for (auto module : ModuleIterator(MODULES.load(std::memory_order_relaxed))) {
    if (module->Contains(pc)) {
      return module;
    }
  }
  return nullptr;
}

// Register a module with the module tracker.
void RegisterModule(Module *module) {
  granary_break_on_fault_if(nullptr != module->next ||
                            MODULES.load(std::memory_order_relaxed) == module);
  Module *next(nullptr);
  do {
    next = MODULES.load(std::memory_order_relaxed);
    module->next = next;
  } while (!MODULES.compare_exchange_weak(next, module));
}

}  // namespace granary
