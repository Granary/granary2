/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_MODULE_H_
#define OS_MODULE_H_

#include "arch/base.h"

#include "granary/base/list.h"
#include "granary/base/lock.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"

#include "granary/app.h"

namespace granary {

// Forward declarations.
class Context;

namespace os {
class ModuleManager;
class Module;

// Represents a location in a module. Note that not all segments within modules
// are necessarily contiguous, but in most cases they are.
class ModuleOffset {
 public:
  inline ModuleOffset(void)
      : module(nullptr),
        offset(0) {}

  // Module containing searched-for program counter, or `nullptr` if the program
  // counter is not located in the module.
  const Module * GRANARY_CONST module;

  // The offset into the module region. If a search for `pc` returns a valid
  // `ModuleOffset` instance then `pc = region_pc + offset`.
  uintptr_t GRANARY_CONST offset;

  // Returns true if this is a valid module offset.
  inline bool IsValid(void) const {
    return nullptr != module;
  }

  // Returns true if one module offset is the same as another.
  inline bool operator==(const ModuleOffset &that) const {
    return module == that.module && offset == that.offset;
  }

 private:
  friend class Module;

  // Initialize a `ModuleOffset` instances.
  GRANARY_INTERNAL_DEFINITION
  inline ModuleOffset(const Module *module_, uintptr_t const offset_)
      : module(module_),
        offset(offset_) {}
};

#ifdef GRANARY_INTERNAL
class ModuleAddressRange;
enum {
  MODULE_READABLE = (1 << 0),
  MODULE_WRITABLE = (1 << 1),
  MODULE_EXECUTABLE = (1 << 2),
  MODULE_COPY_ON_WRITE = (1 << 3)
};
#endif  // GRANARY_INTERNAL

// Represents a loaded module. For example, in user space, the executable is a
// module, `libgranary.so` is a module, in the kernel, the kernel itself would
// be treated as module, `granary.ko` as another module, etc.
class Module {
 public:
  enum {
    kMaxModulePathLength = 256
  };

  // Initialize a new module with no ranges.
  GRANARY_INTERNAL_DEFINITION
  explicit Module(const char *path_);

  GRANARY_INTERNAL_DEFINITION ~Module(void);

  // Return a module offset object for a program counter (that is expected to
  // be contained inside of the module). If the program counter is not part of
  // the module then the returned object is all nulled.
  ModuleOffset OffsetOfPC(AppPC pc) const;

  // Returns true if a module contains the code address `pc`, and if that code
  // address is marked as executable.
  bool Contains(AppPC pc) const;

  // Returns the path of this module.
  const char *Path(void) const;

  // Returns the name of this module.
  const char *Name(void) const;

  // Add a range to a module. This will potentially split a single range into two
  // ranges, extend an existing range, add a new range, or do nothing if the new
  // range is fully subsumed by another one.
  GRANARY_INTERNAL_DEFINITION
  void AddRange(uintptr_t begin_addr, uintptr_t end_addr,
                uintptr_t begin_offset, unsigned perms);

  // Remove a range from a module.
  GRANARY_INTERNAL_DEFINITION
  bool RemoveRange(uintptr_t begin_addr, uintptr_t end_addr);

  // Remove all ranges from this module.
  GRANARY_INTERNAL_DEFINITION void RemoveRanges(void);

  GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(Module, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

  GRANARY_CONST Module * GRANARY_CONST next;

  // Pointer to an opaque, kernel/user-space specific data structure.
  //
  // In the case of the Linux kernel, this points to the exception table
  // information of a module.
  //
  // TODO(pag): How to eventually garbage collect this kind of data?
  GRANARY_INTERNAL_DEFINITION void *where_data;

 private:
  friend class AppMetaData;
  friend class ModuleManager;

  Module(void) = delete;

  // Adds a range into the range list. If there is a conflict when adding a
  // range then some ranges might be removed (and some parts of those ranges
  // might be altered or removed). If ranges are removed then these will result
  // in code cache flushing events.
  GRANARY_INTERNAL_DEFINITION
  void AddRange(ModuleAddressRange *range);

  // Adds a range into the range list. This handles conflicts and performs
  // conflict resolution, which typically results in some code cache flushing
  // events.
  //
  // Note: This must be invoked with the module's `ranges_lock` held as
  //       `WriteLocked`.
  GRANARY_INTERNAL_DEFINITION
  bool RemoveRangeConflicts(uintptr_t begin_addr, uintptr_t end_addr);

  // Adds a range into the range list. This will no do conflict resolution.
  GRANARY_INTERNAL_DEFINITION
  void AddRangeNoConflict(ModuleAddressRange *range);

  // Name/path of this module.
  GRANARY_INTERNAL_DEFINITION char name[kMaxModulePathLength];
  GRANARY_INTERNAL_DEFINITION char path[kMaxModulePathLength];

  // The address ranges of this module.
  GRANARY_INTERNAL_DEFINITION ModuleAddressRange *ranges;

  // Lock for accessing and modifying ranges.
  GRANARY_INTERNAL_DEFINITION mutable ReaderWriterLock ranges_lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Module);
};

typedef LinkedListIterator<const Module> ConstModuleIterator;

#ifdef GRANARY_INTERNAL
// Manages a set of modules.
//
// TODO(pag): Track discovered module dependencies. For example, if there is
//            a direct jump / call from one module to another, mark it as a
//            dependency. This can be used during code cache flushing of
//            particular modules.
class ModuleManager {
 public:
  // Intiailize the module manager to know about its context. The context is
  // passed through to modules so that modules can notify the context when
  // code cache flushes must occur.
  ModuleManager(void);

  ~ModuleManager(void);

  // Find a module given a program counter.
  Module *FindByAppPC(AppPC pc);

  // Find the module and offset associated with a given program counter.
  ModuleOffset FindOffsetOfPC(AppPC pc);

  // Find a module given its path.
  Module *FindByPath(const char *path);

  // Find a module given its name.
  Module *FindByName(const char *name);

  // Register a module with the module tracker.
  void Register(Module *module);

  // Find all built-in modules. In user space, this will go and find things
  // like libc. In kernel space, this will identify already loaded modules.
  //
  // This function should only be invoked once per `ModuleManager` instance.
  void RegisterAllBuiltIn(void);

  // Find all built-in modules. In user space, this will go and find things
  // like libc. In kernel space, this will identify already loaded modules.
  //
  // This function should only be invoked once per `ModuleManager` instance.
  void ReRegisterAllBuiltIn(void);

  // Remove a range of addresses that may be part of one or more modules.
  // Returns `true` if changes were made.
  bool RemoveRange(uintptr_t begin_addr, uintptr_t end_addr);

  // Returns an iterator over all loaded modules.
  inline ConstModuleIterator Modules(void) const {
    return ConstModuleIterator(modules);
  }

 private:
  // Linked list of modules. Modules in the list are stored in no particular
  // order because they can have discontiguous segments.
  Module *modules;

  // Lock on updating the modules list.
  ReaderWriterLock modules_lock;
};

// Initializes the module manager.
void InitModuleManager(void);

// Exits the module manager.
void ExitModuleManager(void);

#endif  // GRANARY_INTERNAL

// Returns a pointer to the module containing some program counter.
const Module *ModuleContainingPC(AppPC pc);

// Find the module and offset associated with a given program counter.
ModuleOffset ModuleOffsetOfPC(AppPC pc);

// Returns a pointer to the first module whose name matches `name`.
const Module *ModuleByName(const char *name);

// Returns an iterator to all currently loaded modules.
ConstModuleIterator LoadedModules(void);

// Invalidate all cache code related belonging to some module code. Returns
// true if any module code was invalidated as a result of this operation.
bool InvalidateModuleCode(void *context, AppPC start_pc, uintptr_t num_bytes);

}  // namespace os
}  // namespace granary

#endif  // OS_MODULE_H_
