/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_MODULE_H_
#define GRANARY_MODULE_H_

#include "granary/arch/base.h"
#include "granary/base/lock.h"
#include "granary/base/new.h"
#include "granary/base/types.h"
#include "granary/metadata.h"

namespace granary {

// Forward declarations.
class Module;
class ModuleMetaData;
class ModuleManager;
GRANARY_INTERNAL_DEFINITION class CodeAllocator;

// Represents a location in a module. Note that not all segments within modules
// are necessarily contiguous, but in most cases they are.
class ModuleOffset {
 public:
  inline ModuleOffset(void)
      : module(nullptr),
        offset(0) {}

  ModuleOffset(const ModuleOffset &) = default;

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
  GRANARY_CLIENT,
  GRANARY_CODE_CACHE,
  KERNEL,
  PROGRAM = KERNEL,
  KERNEL_MODULE,
  SHARED_LIBRARY = KERNEL_MODULE,
  DYNAMIC  // E.g. because of `mmap`.
};

#ifdef GRANARY_INTERNAL
namespace internal {
struct ModuleAddressRange;
enum {
  MODULE_READABLE = (1 << 0),
  MODULE_WRITABLE = (1 << 1),
  MODULE_EXECUTABLE = (1 << 2),
  MODULE_COPY_ON_WRITE = (1 << 3)
};
}  // namespace internal
#endif  // GRANARY_INTERNAL

// Represents a loaded module. For example, in user space, the executable is a
// module, `libgranary.so` is a module, in the kernel, the kernel itself would
// be treated as module, `granary.ko` as another module, etc.
class Module {
 public:
  enum {
    MAX_NAME_LEN = 256
  };

  GRANARY_INTERNAL_DEFINITION
  Module(ModuleKind kind_, const char *name_);

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

  // Add a range to a module. This will potentially split a single range into two
  // ranges, extend an existing range, add a new range, or do nothing if the new
  // range is fully subsumed by another one.
  void AddRange(uintptr_t begin_addr, uintptr_t end_addr,
                uintptr_t begin_offset, unsigned perms);

  // Remove a range from a module.
  void RemoveRange(uintptr_t begin_addr, uintptr_t end_addr);

  GRANARY_DEFINE_NEW_ALLOCATOR(Module, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })

  GRANARY_INTERNAL_DEFINITION Module *next;

 private:
  friend class ModuleMetaData;
  friend class ModuleManager;

  Module(void) = delete;

  // Adds a range into the range list. Returns a range to free if that range is
  // no longer needed.
  GRANARY_INTERNAL_DEFINITION
  internal::ModuleAddressRange *AddRange(internal::ModuleAddressRange *range);

  // The kind of this module (e.g. granary, client, kernel, etc.).
  GRANARY_INTERNAL_DEFINITION ModuleKind const kind;

  // Name/path of this module.
  GRANARY_INTERNAL_DEFINITION char name[MAX_NAME_LEN];
  GRANARY_INTERNAL_DEFINITION char path[MAX_NAME_LEN];

  // The address ranges of this module.
  GRANARY_INTERNAL_DEFINITION internal::ModuleAddressRange *ranges;

  // Lock for accessing and modifying ranges.
  GRANARY_INTERNAL_DEFINITION mutable ReaderWriterLock ranges_lock;

  // Age of the data structure. Used as a heuristic to merge/split ranges.
  GRANARY_INTERNAL_DEFINITION std::atomic<unsigned> age;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Module);
};

// Module-specific meta-data that Granary maintains about all basic blocks.
class ModuleMetaData : public IndexableMetaData<ModuleMetaData> {
 public:
  // Default-initializes Granary's internal module meta-data.
  ModuleMetaData(void);

  // Initialize this meta-data for a given module offset and program counter.
  GRANARY_INTERNAL_DEFINITION
  void Init(ModuleOffset source_, AppPC start_pc_);

  // Returns the code cache allocator for this block.
  GRANARY_INTERNAL_DEFINITION
  CodeAllocator *CacheCodeAllocatorForBlock(void) const;

  // Returns true if one block's module metadata can be materialized alognside
  // another block's module metadata. For example, if two blocks all in
  // different modules then we can't materialize them together in the same
  // instrumentation session. Similarly, if two blocks fall into different
  // address ranges of the same module, then we also can't materialize them
  // in the same session.
  GRANARY_INTERNAL_DEFINITION
  bool CanMaterializeWith(const ModuleMetaData *that) const;

  // Hash the translation meta-data.
  void Hash(HashFunction *hasher) const;

  // Compare two translation meta-data objects for equality.
  bool Equals(const ModuleMetaData *meta) const;

  // The module from which this block originates.
  GRANARY_CONST ModuleOffset source;

  // The native program counter where this block begins.
  GRANARY_CONST AppPC start_pc;
} __attribute__((packed));

// Specify to Granary tools that the function to get the info about
// `ModuleMetaData` already exists.
GRANARY_SHARE_METADATA(ModuleMetaData)

// Manages a set of modules.
//
// TODO(pag): Track discovered module dependencies. For example, if there is
//            a direct jump / call from one module to another, mark it as a
//            dependency. This can be used during code cache flushing of
//            particular modules.
class ModuleManager {
 public:
  ModuleManager(void) GRANARY_EXTERNAL_DELETE;

  // Find a module given a program counter.
  GRANARY_CONST Module *FindByPC(AppPC pc);

  // Find a module given its name.
  GRANARY_CONST Module *FindByName(const char *name);

  // Register a module with the module tracker.
  GRANARY_INTERNAL_DEFINITION void Register(Module *module);

  // Find all built-in modules. In user space, this will go and find things like
  // libc. In kernel space, this will identify already loaded modules.
  //
  // This function should only be invoked once per `ModuleManager` instance.
  GRANARY_INTERNAL_DEFINITION void RegisterAllBuiltIn(void);

 private:
  // Linked list of modules. Modules in the list are stored in no particular
  // order because they can have discontiguous segments.
  GRANARY_INTERNAL_DEFINITION std::atomic<Module *> modules;
};

}  // namespace granary

#endif  // GRANARY_MODULE_H_
