/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"
#include "granary/base/string.h"
#include "granary/breakpoint.h"
#include "granary/module.h"

namespace granary {
namespace detail {

// Represents a range of code/data within a module.
struct ModuleAddressRange {
  ModuleAddressRange(uintptr_t begin_addr_, uintptr_t end_addr_,
                     uintptr_t begin_offset_, unsigned perms_,
                     unsigned age_)
      : next(nullptr),
        begin_addr(begin_addr_),
        end_addr(end_addr_),
        begin_offset(begin_offset_),
        end_offset(begin_offset + (end_addr - begin_addr)),
        perms(perms_),
        age(age_) {}

  ModuleAddressRange *next;
  uintptr_t begin_addr;  // Runtime offsets.
  uintptr_t end_addr;
  uintptr_t begin_offset;  // Static offsets in the module code.
  uintptr_t end_offset;
  unsigned perms;
  unsigned age;

  GRANARY_DEFINE_NEW_ALLOCATOR(Module, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })
};

}  // namespace detail

typedef LinkedListIterator<const detail::ModuleAddressRange>
        ModuleAddressRangeIterator;

typedef LinkedListIterator<Module> ModuleIterator;

namespace {

// Static list of loaded modules. Modules are not stored in any particular
// order as their segments can be discontiguous.
static std::atomic<Module *> MODULES(ATOMIC_VAR_INIT(nullptr));

// Find the address range that contains a particular program counter. Returns
// `nullptr` if no such range exists in the specified list.
static const detail::ModuleAddressRange *FindRange(
    const detail::ModuleAddressRange *ranges, AppProgramCounter pc) {
  auto addr = reinterpret_cast<uintptr_t>(pc);
  for (auto range : ModuleAddressRangeIterator(ranges)) {
    if (range->begin_addr <= addr && addr < range->end_addr) {
      return range;
    } else if (range->begin_addr > addr) {
      return nullptr;
    }
  }
  return nullptr;
}

}  // namespace

Module::Module(ModuleKind kind_, const char *name_)
    : next(nullptr),
      kind(kind_),
      ranges(nullptr),
      ranges_lock(),
      age(ATOMIC_VAR_INIT(0)) {
  CopyString(&(name[0]), MAX_NAME_LEN, name_);
}

// Return a module offset object for a program counter (that is expected to
// be contained inside of the module). If the program counter is not part of
// the module then the returned object is all nulled.
ModuleOffset Module::OffsetOf(AppProgramCounter pc) const {
  ReadLocked locker(&ranges_lock);
  auto range = FindRange(ranges, pc);
  if (!range) {
    return ModuleOffset(nullptr, 0);
  }
  auto addr = reinterpret_cast<uintptr_t>(pc);
  return ModuleOffset(this, range->begin_offset + (addr - range->begin_addr));
}

// Returns true if a module contains the code address `pc`, and if that code
// address is marked as executable.
bool Module::Contains(AppProgramCounter pc) const {
  ReadLocked locker(&ranges_lock);
  return nullptr != FindRange(ranges, pc);
}

// Returns the kind of this module.
ModuleKind Module::Kind(void) const {
  return kind;
}

// Returns the name of this module.
const char *Module::Name(void) const {
  return &(name[0]);
}

// Add a range to a module. This will potentially split a single range into two
// ranges, extend an existing range, add a new range, or do nothing if the new
// range is fully subsumed by another one.
void Module::AddRange(uintptr_t begin_addr, uintptr_t end_addr,
                      uintptr_t begin_offset, unsigned perms) {
  if (detail::MODULE_EXECUTABLE & perms) {
    auto range = new detail::ModuleAddressRange(
        begin_addr, end_addr, begin_offset, perms, age.fetch_add(1));
    do {
      WriteLocked locker(&ranges_lock);
      range = AddRange(range);
    } while (false);
    if (range) {
      delete range;
    }
  }
}

// Remove a range from a module.
void Module::RemoveRange(uintptr_t begin_addr, uintptr_t end_addr) {
  // TODO(pag): Synchronize me?
  GRANARY_UNUSED(begin_addr);
  GRANARY_UNUSED(end_addr);
}

// Adds a range into the range list. Returns a range to free if that range is
// no longer needed.
//
// TODO(pag): Test this code!!
detail::ModuleAddressRange *Module::AddRange(
    detail::ModuleAddressRange *range) {

  detail::ModuleAddressRange **next_ptr(&ranges);
  detail::ModuleAddressRange *curr(ranges);
  detail::ModuleAddressRange *remove(nullptr);

  // Find an insertion point.
  for (; curr; next_ptr = &(curr->next), curr = curr->next) {
    if (range->begin_addr < curr->begin_addr) {
      break;
    }
  }

  // Unconditionally add the new range into the range list.
  range->next = *next_ptr;
  *next_ptr = range;

  // Try to right collapse or left collapse an existing range, and potentially
  // find a range to remove.
  for (curr = ranges; curr; curr = curr->next) {
    if (!curr->next) {
      continue;
    }

    auto next_range = curr->next;
    if (!next_range || curr->end_addr < next_range->begin_addr) {
      continue;
    }

    // TODO(pag): Handle this case when it comes up.
    granary_break_on_fault_if(curr->end_addr > next_range->end_addr);

    if (curr->age < next_range->age) {  // Right collapse `curr`.
      curr->end_offset -= (next_range->begin_addr - curr->end_addr);
      curr->end_addr = next_range->begin_addr;
    } else {  // Left collapse `next`.
      next_range->begin_offset += curr->end_addr - next_range->begin_addr;
      next_range->begin_addr = curr->end_addr;
    }

    if (curr->end_offset != next_range->begin_offset ||
        curr->perms != next_range->perms) {
      continue;  // Not a merge candidate.
    }

    curr->end_addr = next_range->end_addr;
    curr->end_offset = next_range->end_offset;
    curr->next = next_range->next;

    if (GRANARY_LIKELY(!remove)) {
      remove = next_range;  // We'll delete this outside of the write lock.
    } else {
      delete next_range;  // Already have something to delete.
    }
  }
  return remove;
}

// Find a module given a program counter.
const Module *FindModuleByPC(AppProgramCounter pc) {
  for (auto module : ModuleIterator(MODULES.load(std::memory_order_relaxed))) {
    if (module->Contains(pc)) {
      return module;
    }
  }
  return nullptr;
}

// Find a module given its name.
Module *FindModuleByName(const char *name) {
  for (auto module : ModuleIterator(MODULES.load(std::memory_order_relaxed))) {
    if (StringsMatch(module->name, name)) {
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
