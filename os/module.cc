/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"

#include "granary/breakpoint.h"
#include "granary/context.h"

#include "os/module.h"

namespace granary {
namespace os {

// Represents a range of code/data within a module.
class ModuleAddressRange {
 public:
  // Initialize a new module address range. Assumes the invariant
  // `begin_addr_ < end_addr_`, which is checked before a range is added to a
  // module.
  ModuleAddressRange(uintptr_t begin_addr_, uintptr_t end_addr_,
                     uintptr_t begin_offset_, unsigned perms_)
      : next(nullptr),
        begin_addr(begin_addr_),
        end_addr(end_addr_),
        begin_offset(begin_offset_),
        end_offset(begin_offset + (end_addr - begin_addr)),
        perms(perms_) {}

  // Next range. Module ranges are arranged in a sorted linked list such that
  // for two adjacent ranges `r1` and `r2` in the list, the following
  // relationships hold:
  //
  //    r1.begin_addr < r1.end_addr <= r2.begin_addr < r2.end_addr
  ModuleAddressRange *next;

  // Runtime offsets in the virtual address space.
  uintptr_t begin_addr;
  uintptr_t end_addr;

  // Static offsets within the module's code segments.
  uintptr_t begin_offset;
  uintptr_t end_offset;

  // Permissions (e.g. readable, writable, executable).
  unsigned perms;

  GRANARY_DEFINE_NEW_ALLOCATOR(ModuleAddressRange, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

 private:
  ModuleAddressRange(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ModuleAddressRange);
};

typedef LinkedListIterator<const ModuleAddressRange>
        ConstModuleAddressRangeIterator;

typedef LinkedListIterator<ModuleAddressRange>
        ModuleAddressRangeIterator;

typedef LinkedListZipper<ModuleAddressRange> ModuleAddressRangeZipper;

typedef LinkedListIterator<Module> ModuleIterator;

namespace {

// Find the address range that contains a particular address. Returns
// `nullptr` if no such range exists in the specified list.
static const ModuleAddressRange *FindRange(const ModuleAddressRange *ranges,
                                           uintptr_t addr) {
  for (auto range : ConstModuleAddressRangeIterator(ranges)) {
    if (range->begin_addr <= addr && addr < range->end_addr) {
      return range;
    } else if (range->begin_addr > addr) {
      return nullptr;
    }
  }
  return nullptr;
}

// Find the address range that contains a particular program counter. Returns
// `nullptr` if no such range exists in the specified list.
static const ModuleAddressRange *FindRange(const ModuleAddressRange *ranges,
                                           AppPC addr) {
  return FindRange(ranges, reinterpret_cast<uintptr_t>(addr));
}

}  // namespace

// Initialize a new module with no ranges.
Module::Module(ModuleKind kind_, const char *name_)
    : next(nullptr),
      context(nullptr),
      kind(kind_),
      ranges(nullptr),
      ranges_lock() {
  memset(&(name[0]), 0, MAX_NAME_LEN);
  CopyString(&(name[0]), MAX_NAME_LEN, name_);
}

Module::~Module(void) {
  for (ModuleAddressRange *next_range(nullptr); ranges; ranges = next_range) {
    next_range = ranges->next;
    delete ranges;
  }
  context = nullptr;
}

// Return a module offset object for a program counter (that is expected to
// be contained inside of the module). If the program counter is not part of
// the module then the returned object is all nulled.
ModuleOffset Module::OffsetOf(AppPC pc) const {
  ReadLocked locker(&ranges_lock);
  auto range = FindRange(ranges, pc);
  if (!range) {
    return ModuleOffset(nullptr, 0);
  } else {
    auto addr = reinterpret_cast<uintptr_t>(pc);
    return ModuleOffset(this, range->begin_offset + (addr - range->begin_addr));
  }
}

// Returns true if a module contains the code address `pc`, and if that code
// address is marked as executable.
bool Module::Contains(AppPC pc) const {
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

// Sets the current context of the module.
void Module::SetContext(ContextInterface *context_) {
  context = context_;
}

// Add a range to a module. This will potentially split a single range into two
// ranges, extend an existing range, add a new range, or do nothing if the new
// range is fully subsumed by another one.
void Module::AddRange(uintptr_t begin_addr, uintptr_t end_addr,
                      uintptr_t begin_offset, unsigned perms) {
  if (begin_addr < end_addr) {
    auto range = new ModuleAddressRange(begin_addr, end_addr,
                                        begin_offset, perms);
    ConditionallyWriteLocked locker(&ranges_lock, nullptr != context);
    AddRange(range);
  } else {
    AddRange(end_addr, begin_addr, begin_offset, perms);
  }
}

// Remove a range from a module.
void Module::RemoveRange(uintptr_t begin_addr, uintptr_t end_addr) {
  ConditionallyWriteLocked locker(&ranges_lock, nullptr != context);
  RemoveRangeConflicts(begin_addr, end_addr);
}

// Adds a range into the range list. If there is a conflict when adding a range
// then some ranges might be removed (and some parts of those ranges might be
// re-added). If ranges are removed then these will result in code cache
// flushing events.
//
// Note: This method is invoked within the context of a `WriteLocked` of the
//       `ranges_lock`.
void Module::AddRange(ModuleAddressRange *range) {
  RemoveRangeConflicts(range->begin_addr, range->end_addr);
  AddRangeNoConflict(range);
}

// Adds a range into the range list. This handles conflicts and performs
// conflict resolution, which typically results in some code cache flushing
// events.
//
// Note: This must be invoked with the module's `ranges_lock` held as
//       `WriteLocked`.
void Module::RemoveRangeConflicts(uintptr_t begin_addr, uintptr_t end_addr) {
  for (auto curr_elem : ModuleAddressRangeZipper(&ranges)) {
    auto curr = curr_elem.Get();
    if (curr->begin_addr < end_addr &&
        curr->end_addr > begin_addr) {

      if (curr->begin_addr < begin_addr) {
        if (end_addr < curr->end_addr) {  // `range` is contained in `curr`.
          auto offset = curr->begin_offset + (end_addr - curr->begin_addr);
          auto after_curr = new ModuleAddressRange(end_addr, curr->end_addr,
                                                   offset, curr->perms);
          curr_elem.InsertAfter(after_curr);
        }
        curr->end_offset -= curr->end_addr - begin_addr;
        curr->end_addr = begin_addr;  // `curr` overlaps on RHS.

      } else if (end_addr < curr->end_addr) {  // `curr` overlaps on LHS.
        curr->begin_offset += end_addr - curr->begin_addr;
        curr->begin_addr = end_addr;

      } else {  // `curr` is contained in `range`.
        curr->end_addr = curr->begin_addr;
      }

      if (curr->begin_addr >= curr->end_addr) {
        curr_elem.Unlink();  // Reap a range.
      }
    } else if (end_addr < curr->begin_addr) {
      break;
    }
  }
}

// Adds a range into the range list. This will no do conflict resolution.
void Module::AddRangeNoConflict(ModuleAddressRange *range) {
  ModuleAddressRange **next_ptr(&ranges);
  ModuleAddressRange *curr(ranges);
  for (; curr; next_ptr = &(curr->next), curr = curr->next) {
    if (range->begin_addr < curr->begin_addr) {
      break;  // Found the insertion point.
    }
  }
  range->next = *next_ptr;
  *next_ptr = range;  // Insert.
}

// Initialize the module tracker.
ModuleManager::ModuleManager(ContextInterface *context_)
    : context(context_),
      modules(nullptr),
      modules_lock() {}

ModuleManager::~ModuleManager(void) {
  Module *next_module(nullptr);
  for (; modules; modules = next_module) {
    next_module = modules->next;
    delete modules;
  }
  context = nullptr;
}

// Find a module given a program counter.
GRANARY_CONST Module *ModuleManager::FindByAppPC(AppPC pc) {
  for (auto num_attempts = 0; num_attempts < 2; ++num_attempts) {
    do {
      ReadLocked locker(&modules_lock);
      for (auto module : ModuleIterator(modules)) {
        if (module->Contains(pc)) {
          return module;
        }
      }
    } while (false);
    if (!num_attempts) RegisterAllBuiltIn();
  }
  return nullptr;
}

// Find a module given its name.
GRANARY_CONST Module *ModuleManager::FindByName(const char *name) {
  ReadLocked locker(&modules_lock);
  for (auto module : ModuleIterator(modules)) {
    if (StringsMatch(module->name, name)) {
      return module;
    }
  }
  return nullptr;
}

// Register a module with the module tracker.
void ModuleManager::Register(Module *module) {
  GRANARY_ASSERT(nullptr == module->context);
  GRANARY_ASSERT(!FindByName(module->name));
  module->SetContext(context);
  WriteLocked locker(&modules_lock);
  module->next = modules;
  modules = module;
}

}  // namespace os
}  // namespace granary
