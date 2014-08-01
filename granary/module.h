/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_MODULE_H_
#define GRANARY_MODULE_H_

#include "granary/arch/base.h"

#include "granary/base/list.h"
#include "granary/base/lock.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"

#include "granary/metadata.h"

namespace granary {

// Forward declarations.
class ContextInterface;
class Module;
class AppMetaData;
class ModuleManager;

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

// Different kinds of recognized modules. For the most part, Granary only cares
// about modules that contain executable code.
enum class ModuleKind {
  GRANARY,
  GRANARY_CODE_CACHE,
  KERNEL,
  PROGRAM = KERNEL,
  KERNEL_MODULE,
  SHARED_LIBRARY = KERNEL_MODULE,
  DYNAMIC  // E.g. because of `mmap`.
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
    MAX_NAME_LEN = 256
  };

  // Initialize a new module with no ranges.
  GRANARY_INTERNAL_DEFINITION
  Module(ModuleKind kind_, const char *name_);

  GRANARY_INTERNAL_DEFINITION ~Module(void);

  // Return a module offset object for a program counter (that is expected to
  // be contained inside of the module). If the program counter is not part of
  // the module then the returned object is all nulled.
  ModuleOffset OffsetOf(AppPC pc) const;

  // Returns true if a module contains the code address `pc`, and if that code
  // address is marked as executable.
  bool Contains(AppPC pc) const;

  // Returns the kind of this module.
  ModuleKind Kind(void) const;

  // Returns the name of this module.
  const char *Name(void) const;

  // Sets the current context of the module.
  GRANARY_INTERNAL_DEFINITION
  void SetContext(ContextInterface *context_);

  // Add a range to a module. This will potentially split a single range into two
  // ranges, extend an existing range, add a new range, or do nothing if the new
  // range is fully subsumed by another one.
  GRANARY_INTERNAL_DEFINITION
  void AddRange(uintptr_t begin_addr, uintptr_t end_addr,
                uintptr_t begin_offset, unsigned perms);

  // Remove a range from a module.
  GRANARY_INTERNAL_DEFINITION
  void RemoveRange(uintptr_t begin_addr, uintptr_t end_addr);

  GRANARY_DEFINE_NEW_ALLOCATOR(Module, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

  GRANARY_CONST Module * GRANARY_CONST next;

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
  void RemoveRangeConflicts(uintptr_t begin_addr, uintptr_t end_addr);

  // Adds a range into the range list. This will no do conflict resolution.
  GRANARY_INTERNAL_DEFINITION
  void AddRangeNoConflict(ModuleAddressRange *range);

  // Context to which this module belongs.
  //
  // Note: We say that a module is shared if and only if `context` is non-
  //       null. Therefore, if `context` is null, then some locks need not be
  //       acquired because we don't consider the `Module` to be exposed to
  //       other threads/cores.
  GRANARY_INTERNAL_DEFINITION ContextInterface *context;

  // The kind of this module (e.g. granary, client, kernel, etc.).
  GRANARY_INTERNAL_DEFINITION ModuleKind const kind;

  // Name/path of this module.
  GRANARY_INTERNAL_DEFINITION char name[MAX_NAME_LEN];

  // The address ranges of this module.
  GRANARY_INTERNAL_DEFINITION ModuleAddressRange *ranges;

  // Lock for accessing and modifying ranges.
  GRANARY_INTERNAL_DEFINITION mutable ReaderWriterLock ranges_lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Module);
};

typedef LinkedListIterator<const Module> ConstModuleIterator;

// Application-specific meta-data that Granary maintains about all basic blocks.
class AppMetaData : public IndexableMetaData<AppMetaData> {
 public:
  // Default-initializes Granary's internal module meta-data.
  AppMetaData(void);

  // Compare two translation meta-data objects for equality.
  bool Equals(const AppMetaData *meta) const;

  // The native program counter where this block begins.
  GRANARY_CONST AppPC start_pc;
};

// Specify to Granary tools that the function to get the info about
// `AppMetaData` already exists.
GRANARY_SHARE_METADATA(AppMetaData)

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
  explicit ModuleManager(ContextInterface *context_);

  ~ModuleManager(void);

  // Find a module given a program counter.
  Module *FindByAppPC(AppPC pc);

  // Find a module given its name.
  Module *FindByName(const char *name);

  // Register a module with the module tracker.
  void Register(Module *module);

  // Find all built-in modules. In user space, this will go and find things
  // like libc. In kernel space, this will identify already loaded modules.
  //
  // This function should only be invoked once per `ModuleManager` instance.
  void RegisterAllBuiltIn(void);

  // Returns an iterator over all loaded modules.
  inline ConstModuleIterator Modules(void) const {
    return ConstModuleIterator(modules);
  }

 private:
  ModuleManager(void) = delete;

  // Context to which this manager belongs.
  ContextInterface *context;

  // Linked list of modules. Modules in the list are stored in no particular
  // order because they can have discontiguous segments.
  Module *modules;

  // Lock on updating the modules list.
  ReaderWriterLock modules_lock;
};
#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_MODULE_H_
