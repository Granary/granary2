/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_MODULE_H_
#define GRANARY_MODULE_H_

#include "granary/arch/base.h"
#include "granary/base/new.h"
#include "granary/base/types.h"

#include "granary/init.h"

namespace granary {

// Forward declarations.
class Module;

// Represents a location in a module. Note that not all segments within modules
// are necessarily contiguous, but in most cases they are.
class ModuleOffset {
 public:
  ModuleOffset(void) = delete;
  ModuleOffset(const ModuleOffset &) = default;

  // Module containing searched-for program counter, or `nullptr` if the program
  // counter is not located in the module.
  const Module * const module;

  // The beginning of the module region containing the program counter.
  AppProgramCounter const region_pc;

  // The offset into the module region. If a search for `pc` returns a valid
  // `ModuleOffset` instance then `pc = region_pc + offset`.
  uintptr_t const offset;

 private:
  GRANARY_INTERNAL_DEFINITION friend class Module;

  // Initialize a `ModuleOffset` instances.
  GRANARY_INTERNAL_DEFINITION
  inline ModuleOffset(const Module *module_, AppProgramCounter region_pc_,
                      uint64_t const offset_)
      : module(module_),
        region_pc(region_pc_),
        offset(offset_) {}
};

// Different kinds of recognized modules. For the most part, Granary only cares
// about modules that contain executable code.
enum class ModuleKind {
  GRANARY,
  GRANARY_TOOL,
  GRANARY_CODE_CACHE,
  KERNEL,
  PROGRAM = KERNEL,
  KERNEL_MODULE,
  SHARED_LIBRARY = KERNEL_MODULE,
  DYNAMIC  // E.g. because of `mmap`.
};

#ifdef GRANARY_INTERNAL
namespace detail {

enum ModuleMemoryPerms {
  READABLE = (1 << 0),
  WRITABLE = (1 << 1),
  EXECUTABLE = (1 << 2),
  COPY_ON_WRITE = (1 << 3)
};

// Represents a range of code/data within a module.
struct ModuleAddressRange {
  ModuleAddressRange *next;
  uintptr_t begin_addr;
  uintptr_t end_addr;
  ModuleMemoryPerms perms;
};

}  // namespace detail
#endif

// Represents a loaded module. For example, in user space, the executable is a
// module, `libgranary.so` is a module, in the kernel, the kernel itself would
// be treated as module, `granary.ko` as another module, etc.
//
// TODO(pag): Track discovered module dependencies. For example, if there is
//            a direct jump / call from one module to another, mark it as a
//            dependency. This can be used during code cache flushing of
//            particular modules.
class Module {
 public:

  // Return a module offset object for a program counter (that is expected to
  // be contained inside of the module). If the program counter is not part of
  // the module then the returned object is all nulled.
  ModuleOffset OffsetOf(AppProgramCounter pc) const;

  // Returns true if a module contains the code address `pc`, and if that code
  // address is marked as executable.
  bool Contains(AppProgramCounter pc) const;

  // Returns the kind of this module.
  ModuleKind Kind(void) const;

  // Returns the name of this module.
  const char *Name(void) const;

  GRANARY_DEFINE_NEW_ALLOCATOR(Module, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })

  GRANARY_INTERNAL_DEFINITION Module *next;

 private:
  enum {
    MAX_NAME_LEN = 256
  };

  // The kind of this module (e.g. granary, tool, kernel, etc.).
  GRANARY_INTERNAL_DEFINITION ModuleKind const kind;

  // Name/path of this module.
  GRANARY_INTERNAL_DEFINITION char name[MAX_NAME_LEN];

  // The address ranges of this module.
  //
  // TODO(pag): For now we will assume that module segments are loaded into
  //            contiguous memory regions.
  GRANARY_INTERNAL_DEFINITION detail::ModuleAddressRange ranges;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Module);
};

// Find a module given a program counter.
const Module *FindModule(AppProgramCounter pc);

// Register a module with the module tracker.
void RegisterModule(Module *module);

// Initialize the module tracker.
GRANARY_INTERNAL_DEFINITION void InitModules(InitKind);

}  // namespace granary

#endif  // GRANARY_MODULE_H_
