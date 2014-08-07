/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"
#include "granary/context.h"
#include "granary/tool.h"

#include "os/module.h"

namespace granary {

// Iterator to loop over tool instances.
typedef LinkedListIterator<ToolDescription> ToolDescriptionIterator;

namespace {

// Unique ID assigned to a tool.
static std::atomic<int> next_tool_id(ATOMIC_VAR_INIT(0));

// Dependency graph between tools. If `depends_on[t1][t2]` is `true` then `t2`
// must be run before `t1` when instrumenting code.
static bool depends_on[MAX_NUM_TOOLS][MAX_NUM_TOOLS] = {{false}};

// Tools names.
static char tool_names[MAX_NUM_TOOLS][MAX_TOOL_NAME_LEN] = {{'\0'}};

// Registered tools, indexed by ID.
static ToolDescription *registered_tools[MAX_NUM_TOOLS] = {nullptr};

// Find a tool's ID given its name. Returns -1 if a tool
static int ToolId(const char *name) {
  //auto descs = descriptions.load(std::memory_order_acquire);
  //for (auto desc : ToolDescriptionIterator(descs)) {
  for (auto i = 0; i < MAX_NUM_TOOLS; ++i) {
    if (StringsMatch(name, tool_names[i])) {
      return i;
    }
  }

  // Allocate a new ID for this tool, even if it isn't registered yet.
  auto id = next_tool_id.fetch_add(1);
  GRANARY_ASSERT(MAX_NUM_TOOLS > id);
  CopyString(&(tool_names[id][0]), MAX_TOOL_NAME_LEN, name);
  return id;
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
InstrumentationTool::InstrumentationTool(void)
    : next(nullptr),
      context(context),
      curr_scope(-1) {
  for (auto &scope : scopes) {
    scope = nullptr;
  }
}
#pragma clang diagnostic pop

// Closes any open inline assembly scopes.
InstrumentationTool::~InstrumentationTool(void) {
  curr_scope = 0;
  for (auto scope : scopes) {
    if (scope) {
      EndInlineAssembly();
    }
    ++curr_scope;
  }
}

// Used to instrument code entrypoints.
void InstrumentationTool::InstrumentEntryPoint(BlockFactory *,
                                               CompensationBasicBlock *,
                                               EntryPointKind, int) {}

// Used to instrument control-flow instructions and decide how basic blocks
// should be materialized.
//
// This method is repeatedly executed until no more materialization
// requests are made.
void InstrumentationTool::InstrumentControlFlow(BlockFactory *,
                                                LocalControlFlowGraph *) {}

// Used to implement more complex forms of instrumentation where tools need to
// see the entire local control-flow graph.
//
// This method is executed once per tool per instrumentation session.
void InstrumentationTool::InstrumentBlocks(const LocalControlFlowGraph *) {}

// Used to implement the typical JIT-based model of single basic-block at a
// time instrumentation.
//
// This method is executed for each decoded BB in the local CFG,
// but is never re-executed for the same (tool, BB) pair in the current
// instrumentation session.
void InstrumentationTool::InstrumentBlock(DecodedBasicBlock *) {}

// Returns a pointer to the module containing an application `pc`.
const os::Module *InstrumentationTool::ModuleContainingPC(AppPC pc) {
  return os::FindModuleContainingPC(pc);
}

// Begin inserting some inline assembly. This takes in an optional scope
// specifier, which allows tools to use the same variables in two or more
// different contexts/scopes of instrumentation and not have them clash. This
// specifies the beginning of some scope. Any virtual registers defined in
// this scope will be live until the next `EndInlineAssembly` within the same
// block, by the same tool, with the same `scope_id`.
//
// Note: `scope_id`s must be non-negative integers.
void InstrumentationTool::BeginInlineAssembly(
    std::initializer_list<Operand *> inputs, int scope_id) {
  ContinueInlineAssembly(scope_id);
  EndInlineAssembly();
  curr_scope = scope_id;
  scopes[scope_id] = new InlineAssemblyScope(inputs);
}

// Switch to a different scope of inline assembly.
void InstrumentationTool::ContinueInlineAssembly(int scope_id) {
  GRANARY_ASSERT(0 <= scope_id && scope_id < MAX_NUM_INLINE_ASM_SCOPES);
  curr_scope = scope_id;
}

// End the current inline assembly scope.
void InstrumentationTool::EndInlineAssembly(void) {
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
Instruction *InstrumentationTool::InlineBefore(
    Instruction *instr, std::initializer_list<const char *> lines) {
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
Instruction *InstrumentationTool::InlineAfter(
    Instruction *instr, std::initializer_list<const char *> lines) {
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
void InstrumentationTool::RegisterMetaData(const MetaDataDescription *desc) {
  context->RegisterMetaData(const_cast<MetaDataDescription *>(desc));
}

// Initialize an empty tool manager.
InstrumentationManager::InstrumentationManager(ContextInterface *context_)
    : max_align(0),
      max_size(0),
      is_finalized(false),
      num_registed(0),
      allocator(),
      context(context_) {
  memset(&(is_registered[0]), 0, sizeof is_registered);
  memset(&(descriptions[0]), 0, sizeof descriptions);
}

InstrumentationManager::~InstrumentationManager(void) {
  allocator.Destroy();
}

// Register a tool given its name.
void InstrumentationManager::Register(const char *name) {
  GRANARY_ASSERT(!is_finalized);
  if (auto desc = registered_tools[ToolId(name)]) {
    is_registered[desc->id] = true;
    Register(desc);
    descriptions[num_registed++] = desc;
    max_size = GRANARY_MAX(max_size, desc->size);
    max_align = GRANARY_MAX(max_align, desc->align);
  }
}

// Register a tool with this manager using the tool's description.
void InstrumentationManager::Register(const ToolDescription *desc) {
  if (!is_registered[desc->id]) {
    is_registered[desc->id] = true;
    for (auto required_id = 0; required_id < MAX_NUM_TOOLS; ++required_id) {
      if (depends_on[desc->id][required_id]) {
        if (auto required_desc = registered_tools[required_id]) {
          Register(required_desc);
        }
      }
    }
    descriptions[num_registed++] = desc;
  }
}

// Allocate all the tools managed by this `ToolManager` instance, and chain
// then into a linked list.
InstrumentationTool *InstrumentationManager::AllocateTools(void) {
  if (GRANARY_UNLIKELY(!is_finalized)) {
    is_finalized = true;
    InitAllocator();
  }
  InstrumentationTool *tools(nullptr);
  if (max_size) {
    InstrumentationTool **next_tool(&tools);
    for (auto desc : descriptions) {
      if (!desc) {
        break;
      } else {
        auto mem = allocator->Allocate();
        auto tool = reinterpret_cast<InstrumentationTool *>(mem);
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
void InstrumentationManager::FreeTools(InstrumentationTool *tool) {
  for (auto next_tool = tool; tool; tool = next_tool) {
    next_tool = tool->next;
    tool->~InstrumentationTool();
    allocator->Free(tool);
  }
}

// Initialize the allocator for meta-data managed by this manager.
void InstrumentationManager::InitAllocator(void) {
  if (max_size) {
    auto size = GRANARY_ALIGN_TO(max_size, max_align);
    auto offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList), size);
    auto remaining_size = arch::PAGE_SIZE_BYTES - offset;
    auto max_num_allocs = remaining_size / size;
    allocator.Construct(max_num_allocs, offset, size, size);
  }
}


// Registers a tool description with Granary. This assigns the tool an ID if
// it hasn't already got an ID, and then adds the tool into the global list of
// all registered tools.
void RegisterInstrumentationTool(
    ToolDescription *desc, const char *name,
    std::initializer_list<const char *> required_tools) {
  auto &id(desc->id);
  if (-1 == id) {
    id = ToolId(name);
    desc->id = id;
    desc->name = name;
    registered_tools[id] = desc;
  }

  // Add in the dependencies. This might end up allocating ids for tool
  // descriptions that have yet to be loaded. This is because the initialization
  // order of the static constructors is a priori undefined.
  for (auto tool_name : required_tools) {
    if (auto required_id = ToolId(tool_name)) {
      GRANARY_ASSERT(!depends_on[required_id][id]);
      depends_on[id][required_id] = true;
    }
  }
}

}  // namespace granary
