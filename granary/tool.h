/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_TOOL_H_
#define GRANARY_TOOL_H_

#include "granary/base/base.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/operator.h"

#include "granary/code/inline_assembly.h"

#include "granary/entry.h"
#include "granary/metadata.h"

namespace granary {
namespace os {
class Module;
}  // namespace os

// Forward declarations.
class BlockFactory;
class CompensationBasicBlock;
class DecodedBasicBlock;
class LocalControlFlowGraph;
class InstrumentationTool;
class Operand;

GRANARY_INTERNAL_DEFINITION class ContextInterface;
GRANARY_INTERNAL_DEFINITION class InlineAssembly;

GRANARY_INTERNAL_DEFINITION enum {
  MAX_NUM_TOOLS = 32,
  MAX_TOOL_NAME_LEN = 32
};

// Describes the structure of tools that are used to instrument binary code.
class InstrumentationTool {
 public:
  InstrumentationTool(void);

  // Closes any open inline assembly scopes.
  virtual ~InstrumentationTool(void);

  // Initialize this tool.
  virtual void Init(void);

  // Tear down this tool.
  virtual void Exit(void);

  // Used to instrument code entrypoints.
  virtual void InstrumentEntryPoint(BlockFactory *factory,
                                    CompensationBasicBlock *entry_block,
                                    EntryPointKind kind, int category);

  // Used to instrument control-flow instructions and decide how basic blocks
  // should be materialized.
  //
  // This method is repeatedly executed until no more materialization
  // requests are made.
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg);

  // Used to implement more complex forms of instrumentation where tools need
  // to see the entire local control-flow graph.
  //
  // This method is executed once per tool per instrumentation session.
  virtual void InstrumentBlocks(const LocalControlFlowGraph *cfg);

  // Used to implement the typical JIT-based model of single basic-block at a
  // time instrumentation.
  //
  // This method is executed for each decoded BB in the local CFG,
  // but is never re-executed for the same (tool, BB) pair in the current
  // instrumentation session.
  virtual void InstrumentBlock(DecodedBasicBlock *block);

 GRANARY_PUBLIC:

  // Register some meta-data with Granary that will be used with this tool.
  // This is a convenience method around the `RegisterMetaData` method that
  // operates directly on a meta-data description.
  template <typename T>
  inline void RegisterMetaData(void) {
    RegisterMetaData(GetMetaDataDescription<T>::Get());
  }

 protected:

  // Returns a pointer to the module containing an application `pc`.
  const os::Module *ModuleContainingPC(AppPC pc);

  // Begin inserting some inline assembly. This takes in an optional scope
  // specifier, which allows tools to use the same variables in two or more
  // different contexts/scopes of instrumentation and not have them clash. This
  // specifies the beginning of some scope. Any virtual registers defined in
  // this scope will be live until the next `EndInlineAssembly` within the same
  // block, by the same tool, with the same `scope_id`.
  //
  // Note: `scope_id`s must be non-negative integers.
  void BeginInlineAssembly(std::initializer_list<Operand *> inputs,
                           int scope_id=0);

  inline void BeginInlineAssembly(int scope_id=0) {
    BeginInlineAssembly(std::initializer_list<Operand *>{}, scope_id);
  }

  // Switch to a different scope of inline assembly.
  void ContinueInlineAssembly(int scope_id);

  // End the current inline assembly scope.
  void EndInlineAssembly(void);

  // Inline some assembly code before `instr`, but only if `cond` is true.
  // Returns the inlined instruction, or `instr` if `cond` is false.
  template <typename... Strings>
  inline Instruction *InlineBeforeIf(Instruction *instr, bool cond,
                                     Strings... lines) {
    if (cond) {
      return InlineBefore(instr, {lines...});
    } else {
      return instr;
    }
  }

  // Inline some assembly code before `instr`. Returns the inlined instruction.
  template <typename... Strings>
  inline Instruction *InlineBefore(Instruction *instr, Strings... lines) {
    return InlineBefore(instr, {lines...});
  }

  // Inline some assembly code after `instr`, but only if `cond` is true.
  // Returns the inlined instruction, or `instr` if `cond` is false.
  template <typename... Strings>
  inline Instruction *InlineAfterIf(Instruction *instr, bool cond,
                                    Strings... lines) {
    if (cond) {
      return InlineAfter(instr, {lines...});
    } else {
      return instr;
    }
  }

  // Inline some assembly code after `instr`. Returns the inlined instruction.
  template <typename... Strings>
  Instruction *InlineAfter(Instruction *instr, Strings... lines) {
    return InlineAfter(instr, {lines...});
  }

  // TODO(pag): Need to expose static methods on a tool to flush a
  //            CachedBasicBlock.

  // TODO(pag): Should there be a static methods for begin/end everything? Or
  //            something to signify an environment switch? Or maybe just
  //            something to signify a takeover event?
  //                --> Perhaps a takeover event on a module, and a release
  //                    event on a module.

  // Inline some assembly code before `instr`. Returns the inlined instruction.
  Instruction *InlineBefore(Instruction *instr,
                            std::initializer_list<const char *> lines);

  // Inline some assembly code after `instr`. Returns the inlined instruction.
  Instruction *InlineAfter(Instruction *instr,
                           std::initializer_list<const char *> lines);

 GRANARY_PUBLIC:

  // Register some meta-data with the meta-data manager associated with this
  // tool.
  void RegisterMetaData(const MetaDataDescription *desc);

  // Next tool used to instrument code.
  GRANARY_POINTER(InstrumentationTool) *next;

  // Context into which this tool has been instantiated.
  GRANARY_POINTER(ContextInterface) *context;

 private:
  GRANARY_CONST int curr_scope;
  GRANARY_POINTER(InlineAssemblyScope) *scopes[MAX_NUM_INLINE_ASM_SCOPES];

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstrumentationTool);
};

// Describes a generic tool.
struct ToolDescription {
  // Globally unique ID for this tool description.
  GRANARY_CONST int id;

  // Next tool.
  GRANARY_CONST ToolDescription *next;

  // Name of this tool.
  const char * GRANARY_CONST name;

  const size_t size;
  const size_t align;

  // Virtual table of operations on tools.
  void (* const initialize)(void *);
};

// Creates a description for a tool.
template <typename T>
struct ToolDescriptor {
  static ToolDescription kDescription;
};

// Descriptor for some tool.
template <typename T>
ToolDescription ToolDescriptor<T>::kDescription = {
  -1,
  nullptr,
  nullptr,
  sizeof(T),
  alignof(T),
  &(Construct<T>)
};

#ifdef GRANARY_INTERNAL
typedef LinkedListIterator<InstrumentationTool> ToolIterator;

// Manages a set of tools.
class InstrumentationManager {
 public:
  // Initialize an empty tool manager.
  explicit InstrumentationManager(ContextInterface *context);
  ~InstrumentationManager(void);

  // Register a tool with this manager using the tool's name. This will look
  // up the tool in the global list of all registered Granary tools.
  void Register(const char *name);

  // Allocate all the tools managed by this `ToolManager` instance, and chain
  // then into a linked list. The returns list can be used to instrument code.
  //
  // This ensures that tools are allocated and inserted into the list according
  // to the order of their dependencies, whilst also trying to preserve the
  // tool order specified at the command-line.
  InstrumentationTool *AllocateTools(void);

  // Free all allocated tool objects. This expects a list of `Tool` objects, as
  // allocated by `ToolManager::AllocateTools`.
  void FreeTools(InstrumentationTool *tool);

 private:
  InstrumentationManager(void) = delete;

  // Register a tool with this manager using the tool's description.
  void Register(const ToolDescription *desc);

  // Initialize the allocator for meta-data managed by this manager.
  void InitAllocator(void);

  // Maximum alignment and size (in bytes) of all registered tools.
  size_t max_align;
  size_t max_size;

  // Has this manager been finalized?
  bool is_finalized;

  // All tools registered with this manager.
  int num_registed;
  bool is_registered[MAX_NUM_TOOLS];
  const ToolDescription *descriptions[MAX_NUM_TOOLS];

  // TODO(pag): Have an ordered array of tool descriptions that represent the
  //            tools ordered according to how they are specified at the command
  //            line or according to internal dependencies.

  // Slab allocator for allocating tool instrumentation objects.
  Container<internal::SlabAllocator> allocator;

  // Context to which thios tool manager belongs.
  ContextInterface *context;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstrumentationManager);
};

#endif  // GRANARY_INTERNAL

// Register a tool with Granary given its description.
void RegisterInstrumentationTool(
    ToolDescription *desc, const char *name,
    std::initializer_list<const char *> required_tools);

// Register a binary instrumenter with Granary.
template <typename T>
inline static void RegisterInstrumentationTool(const char *tool_name) {
  RegisterInstrumentationTool(&(ToolDescriptor<T>::kDescription),
                              tool_name, {});
}

// Register a binary instrumenter with Granary.
template <typename T>
inline static void RegisterInstrumentationTool(
    const char *tool_name, std::initializer_list<const char *> required_tools) {
  RegisterInstrumentationTool(&(ToolDescriptor<T>::kDescription), tool_name,
                             required_tools);
}

}  // namespace granary

#endif  // GRANARY_TOOL_H_
