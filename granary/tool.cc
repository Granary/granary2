/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"
#include "granary/context.h"
#include "granary/tool.h"

namespace granary {

// Iterator to loop over tool instances.
typedef LinkedListIterator<ToolDescription> ToolDescriptionIterator;

namespace {

// Linked list of all tool descriptions.
static std::atomic<ToolDescription *> descriptions(ATOMIC_VAR_INIT(nullptr));

// Unique ID assigned to a tool.
static std::atomic<int> next_tool_id(ATOMIC_VAR_INIT(0));

// Dependency graph between tools. If `depends_on[t1][t2]` is `true` then `t2`
// must be run before `t1` when instrumenting code.
static const ToolDescription *depends_on[MAX_NUM_MANAGED_TOOLS]
                                        [MAX_NUM_MANAGED_TOOLS] = {{nullptr}};

// Find a tool's description given its name.
static ToolDescription *FindDescByName(const char *name) {
  auto descs = descriptions.load(std::memory_order_acquire);
  for (auto desc : ToolDescriptionIterator(descs)) {
    if (StringsMatch(name, desc->name)) {
      return desc;
    }
  }
  return nullptr;
}

// Registers a tool description with Granary. This assignes the tool an ID if
// it hasn't already got an ID, and then adds the tool into the global list of
// all registered tools.
static void RegisterToolDescription(ToolDescription *desc, const char *name) {
  if (-1 == desc->id) {
    auto next_id = next_tool_id.fetch_add(1);
    GRANARY_ASSERT(MAX_NUM_MANAGED_TOOLS > next_id);
    desc->id = next_id;
    desc->next = descriptions.load(std::memory_order_acquire);
    desc->name = name;
    descriptions.store(desc, std::memory_order_release);
  }
}

}  // namespace

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
// Dummy implementations of the tool API, so that tools don't need to define
// every API function.
//
// Note: This uses a hack to make sure that the `metadata_manager` field is
//       initialized with whatever its current value is. The
//       `ToolManager::Allocate` makes sure these field is initialized before
//       a `Tool` derived class constructor is invoked, so that the derived
//       tool class can register tool-specific meta-data.
Tool::Tool(void)
    : next(nullptr),
      context(context),
      curr_scope(-1) {
  for (auto &scope : scopes) {
    scope = nullptr;
  }
}
#pragma clang diagnostic pop

// Closes any open inline assembly scopes.
Tool::~Tool(void) {
  curr_scope = 0;
  for (auto scope : scopes) {
    if (scope) {
      EndInlineAssembly();
    }
    ++curr_scope;
  }
}

// Used to instrument control-flow instructions and decide how basic blocks
// should be materialized.
//
// This method is repeatedly executed until no more materialization
// requests are made.
void Tool::InstrumentControlFlow(BlockFactory *, LocalControlFlowGraph *) {}

// Used to implement more complex forms of instrumentation where tools need to
// see the entire local control-flow graph.
//
// This method is executed once per tool per instrumentation session.
void Tool::InstrumentBlocks(const LocalControlFlowGraph *) {}

// Used to implement the typical JIT-based model of single basic-block at a
// time instrumentation.
//
// This method is executed for each decoded BB in the local CFG,
// but is never re-executed for the same (tool, BB) pair in the current
// instrumentation session.
void Tool::InstrumentBlock(DecodedBasicBlock *) {}

// Returns a pointer to the module containing an application `pc`.
const Module *Tool::ModuleContainingPC(AppPC pc) {
  return context->FindModuleContainingPC(pc);
}

// Begin inserting some inline assembly. This takes in an optional scope
// specifier, which allows tools to use the same variables in two or more
// different contexts/scopes of instrumentation and not have them clash. This
// specifies the beginning of some scope. Any virtual registers defined in
// this scope will be live until the next `EndInlineAssembly` within the same
// block, by the same tool, with the same `scope_id`.
//
// Note: `scope_id`s must be non-negative integers.
void Tool::BeginInlineAssembly(std::initializer_list<Operand *> inputs,
                               int scope_id) {
  ContinueInlineAssembly(scope_id);
  EndInlineAssembly();
  curr_scope = scope_id;
  scopes[scope_id] = new InlineAssemblyScope(inputs);
}

// Switch to a different scope of inline assembly.
void Tool::ContinueInlineAssembly(int scope_id) {
  GRANARY_ASSERT(0 <= scope_id && scope_id < MAX_NUM_INLINE_ASM_SCOPES);
  curr_scope = scope_id;
}

// End the current inline assembly scope.
void Tool::EndInlineAssembly(void) {
  if (-1 != curr_scope && scopes[curr_scope]) {
    auto &scope(scopes[curr_scope]);
    if (scope->CanDestroy()) {
      delete scope;
    }
    scope = nullptr;
    curr_scope = -1;
  }
}

namespace {
// Make a new inline assembly instruction.
static Instruction *MakeInlineAssembly(InlineAssemblyScope *scope,
                                       const char *line) {
  auto block = new InlineAssemblyBlock(scope, line);
  return new AnnotationInstruction(IA_INLINE_ASSEMBLY, block);
}
}  // namespace

// Inline some assembly code before `instr`. Returns the inlined instruction.
Instruction *Tool::InlineBefore(Instruction *instr,
                                std::initializer_list<const char *> lines) {
  GRANARY_ASSERT(-1 != curr_scope);
  auto scope = scopes[curr_scope];
  GRANARY_ASSERT(nullptr != scope);
  for (auto line : lines) {
    if (line) {
      instr = instr->InsertBefore(
          std::unique_ptr<Instruction>(MakeInlineAssembly(scope, line)));
    }
  }
  return instr;
}

// Inline some assembly code after `instr`. Returns the inlined instruction.
Instruction *Tool::InlineAfter(Instruction *instr,
                               std::initializer_list<const char *> lines) {
  GRANARY_ASSERT(-1 != curr_scope);
  auto scope = scopes[curr_scope];
  for (auto line : lines) {
    if (line) {
      instr = instr->InsertAfter(
          std::unique_ptr<Instruction>(MakeInlineAssembly(scope, line)));
    }
  }
  return instr;
}

// Register some meta-data with the meta-data manager associated with this
// tool.
void Tool::RegisterMetaData(const MetaDataDescription *desc) {
  context->RegisterMetaData(const_cast<MetaDataDescription *>(desc));
}

// Initialize an empty tool manager.
ToolManager::ToolManager(ContextInterface *context_)
    : max_align(0),
      max_size(0),
      is_finalized(false),
      num_registed(0),
      allocator(),
      context(context_) {
  memset(&(is_registered[0]), 0, sizeof is_registered);
  memset(&(descriptions[0]), 0, sizeof descriptions);
}

ToolManager::~ToolManager(void) {
  allocator.Destroy();
}

// Register a tool given its description.
void ToolManager::Register(const char *name) {
  GRANARY_ASSERT(!is_finalized);
  auto desc = FindDescByName(name);
  if (desc) {
    is_registered[desc->id] = true;
    Register(desc);
    descriptions[num_registed++] = desc;
    max_size = GRANARY_MAX(max_size, desc->size);
    max_align = GRANARY_MAX(max_align, desc->align);
  }
}

// Register a tool with this manager using the tool's description.
void ToolManager::Register(const ToolDescription *desc) {
  if (!is_registered[desc->id]) {
    is_registered[desc->id] = true;
    for (auto required_desc : depends_on[desc->id]) {
      if (required_desc) {
        Register(required_desc);
      }
    }
    descriptions[num_registed++] = desc;
  }
}

// Allocate all the tools managed by this `ToolManager` instance, and chain
// then into a linked list.
Tool *ToolManager::AllocateTools(void) {
  if (GRANARY_UNLIKELY(!is_finalized)) {
    is_finalized = true;
    InitAllocator();
  }
  Tool *tools(nullptr);
  if (max_size) {
    Tool **next_tool(&tools);
    for (auto desc : descriptions) {
      if (!desc) {
        break;
      } else {
        auto mem = allocator->Allocate();
        auto tool = reinterpret_cast<Tool *>(mem);
        tool->context = context;  // Initialize before constructing!
        desc->initialize(mem);
        GRANARY_ASSERT(context == tool->context);
        VALGRIND_MALLOCLIKE_BLOCK(tool, desc->size, 0, 0);

        *next_tool = tool;
        next_tool = &(tool->next);
      }
    }
  }
  return tools;
}

// Free a tool.
void ToolManager::FreeTools(Tool *tool) {
  for (auto next_tool = tool; tool; tool = next_tool) {
    next_tool = tool->next;
    tool->~Tool();
    allocator->Free(tool);
  }
}

// Initialize the allocator for meta-data managed by this manager.
void ToolManager::InitAllocator(void) {
  if (max_size) {
    auto size = GRANARY_ALIGN_TO(max_size, max_align);
    auto offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList), size);
    auto remaining_size = arch::PAGE_SIZE_BYTES - offset;
    auto max_num_allocs = remaining_size / size;
    allocator.Construct(max_num_allocs, offset, size, size);
  }
}

// Register a tool with Granary given its description.
void RegisterTool(ToolDescription *desc,
                  const char *name,
                  std::initializer_list<const char *> required_tools) {
  RegisterToolDescription(desc, name);
  for (auto tool_name : required_tools) {
    if (auto required_desc = FindDescByName(tool_name)) {
      GRANARY_ASSERT(!depends_on[required_desc->id][desc->id]);
      depends_on[desc->id][required_desc->id] = required_desc;
    }
  }
}

}  // namespace granary
