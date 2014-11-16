/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/wrap_func/client.h"
#include "clients/watchpoints/type_id.h"
#include "clients/watchpoints/watchpoints.h"

#include "generated/clients/poly_code/offsets.h"

GRANARY_USING_NAMESPACE granary;

// Track the set of types accessed by each basic block.
class BlockTypeInfo : public MutableMetaData<BlockTypeInfo> {
 public:
  BlockTypeInfo(void)
      : type_ids(),
        type_ids_lock(),
        next(nullptr) {}

  BlockTypeInfo(const BlockTypeInfo &that)
      : next(nullptr) {
    ReadLockedRegion locker(&(that.type_ids_lock));
    type_ids = that.type_ids;
  }

  // Set of `type_id + 1` for this block. We use `type_id + 1` because the
  // default initializer of `uint16_t` is `0`.
  TinySet<uint16_t, 16> type_ids;
  mutable ReaderWriterLock type_ids_lock;

  // Next block meta-data.
  BlockMetaData *next;
};

namespace {

// Make a wrapper for an allocator
#define ALLOC_WRAPPER(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void *), (size_t num_bytes)) { \
      auto name = WRAPPED_FUNCTION; \
      auto ret_address = NATIVE_RETURN_ADDRESS; \
      if (!num_bytes) return nullptr; \
      return TaintAddress(name(num_bytes), TypeIdFor(ret_address, num_bytes)); \
    }

ALLOC_WRAPPER(libc, malloc)
ALLOC_WRAPPER(libstdcxx, _Znwm)
ALLOC_WRAPPER(libstdcxx, _Znam)
ALLOC_WRAPPER(libcxx, _Znwm)
ALLOC_WRAPPER(libcxx, _Znam)

// Associate types with memory.
WRAP_NATIVE_FUNCTION(libc, calloc, (void *), (size_t count, size_t eltsize)) {
  auto calloc = WRAPPED_FUNCTION;
  auto ret_address = NATIVE_RETURN_ADDRESS;
  if (!(eltsize * count)) return nullptr;
  return TaintAddress(calloc(count, eltsize), TypeIdFor(ret_address, eltsize));
}

// Deallocate some memory.
#define FREE_WRAPPER(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void), (void *ptr)) { \
      auto name = WRAPPED_FUNCTION; \
      name(UntaintAddress(ptr)); \
    }

FREE_WRAPPER(libc, free)
FREE_WRAPPER(libstdcxx, _ZdlPv)
FREE_WRAPPER(libstdcxx, _ZdaPv)
FREE_WRAPPER(libcxx, _ZdlPv)
FREE_WRAPPER(libcxx, _ZdaPv)

// Associate types with memory.
WRAP_NATIVE_FUNCTION(libc, realloc, (void *), (void *ptr, size_t new_size)) {
  auto realloc = WRAPPED_FUNCTION;
  if (!IsTaintedAddress(ptr)) {
    return realloc(ptr, new_size);
  } else {
    auto type_id = ExtractTaint(ptr);
    ptr = realloc(UntaintAddress(ptr), new_size);
    return TaintAddress(ptr, type_id);
  }
}

// Linked list of block meta-datas.
static BlockMetaData *gAllBlocks = nullptr;
static SpinLock gAllBlocksLock;

// Meta-data iterator, where the meta-data is chained together via the
// `BlockTypeInfo` type.
typedef MetaDataLinkedListIterator<BlockTypeInfo> BlockTypeInfoIterator;

static void TaintBlock(BlockTypeInfo *meta, void *address) {
  auto type_id = static_cast<uint16_t>(ExtractTaint(address) + 1);
  {
    ReadLockedRegion locker(&(meta->type_ids_lock));
    if (meta->type_ids.Contains(type_id)) return;
  }
  {
    WriteLockedRegion locker(&(meta->type_ids_lock));
    meta->type_ids.Add(type_id);
  }
}

// Taints block meta-data when some watchpoint is triggered.
static void TaintBlockMeta(void *, WatchedOperand *op) {
  op->instr->InsertBefore(lir::InlineFunctionCall(op->block,
      TaintBlock,
      GetMetaData<BlockTypeInfo>(op->block),
      op->watched_reg_op));
}

}  // namespace

// Simple tool for static and dynamic basic block counting.
class PolyCode : public InstrumentationTool {
 public:
  virtual ~PolyCode(void) = default;

  virtual void Init(InitReason) {
    // Wrap libc.
    AddFunctionWrapper(&WRAP_FUNC_libc_malloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_calloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_realloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_free);

    // Wrap GNU's C++ standard library.
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__Znwm);
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__Znam);
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__ZdlPv);
    AddFunctionWrapper(&WRAP_FUNC_libstdcxx__ZdaPv);

    // Wrap clang's C++ standard library.
    AddFunctionWrapper(&WRAP_FUNC_libcxx__Znwm);
    AddFunctionWrapper(&WRAP_FUNC_libcxx__Znam);
    AddFunctionWrapper(&WRAP_FUNC_libcxx__ZdlPv);
    AddFunctionWrapper(&WRAP_FUNC_libcxx__ZdaPv);

    AddWatchpointInstrumenter(TaintBlockMeta);
    AddMetaData<BlockTypeInfo>();
  }

  virtual void Exit(ExitReason) {
    SpinLockedRegion locker(&gAllBlocksLock);
    gAllBlocks = nullptr;
  }

  // Build a global chain of all basic block meta-data.
  virtual void InstrumentBlock(DecodedBasicBlock *block) {
    auto meta = block->MetaData();
    auto type_meta = MetaDataCast<BlockTypeInfo *>(meta);
    SpinLockedRegion locker(&gAllBlocksLock);
    type_meta->next = meta;
    gAllBlocks = meta;
  }
};

// Initialize the `poly_code` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<PolyCode>("poly_code", {"wrap_func", "watchpoints"});
}
