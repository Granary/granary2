/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/wrap_func/client.h"
#include "clients/watchpoints/type_id.h"
#include "clients/watchpoints/watchpoints.h"

#include "generated/clients/poly_code/offsets.h"

GRANARY_USING_NAMESPACE granary;

// Track the set of types accessed by each basic block.
class TypeMetaData : public MutableMetaData<TypeMetaData> {
 public:
  TypeMetaData(void)
      : type_ids(),
        type_ids_lock(),
        next(nullptr) {}

  TypeMetaData(const TypeMetaData &that)
      : next(nullptr) {
    ReadLockedRegion locker(&(that.type_ids_lock));
    type_ids = that.type_ids;
  }

  // Set of `type_id + 1` for this block. We use `type_id + 1` because the
  // default initializer of `uint16_t` is `0`.
  TinySet<uint16_t, 4> type_ids;
  mutable ReaderWriterLock type_ids_lock;

  // Next block meta-data.
  BlockMetaData *next;
};

namespace {

#define GET_ALLOCATOR(name) \
  auto name = WRAPPED_FUNCTION; \
  auto ret_address = NATIVE_RETURN_ADDRESS

// Return a tainted allocated address.
#define RETURN_TAINTED_ADDRESS \
    if (addr) { \
      auto type_id = TypeIdFor(ret_address, size); \
      addr = TaintAddress(addr, type_id); \
    } \
    return addr \

#ifdef GRANARY_WHERE_user

// Make a wrapper for an allocator.
#define ALLOC_WRAPPER(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void *), (size_t size)) { \
      GET_ALLOCATOR(name); \
      auto addr = name(size); \
      RETURN_TAINTED_ADDRESS; \
    }

ALLOC_WRAPPER(libc, malloc)
ALLOC_WRAPPER(libc, valloc)
ALLOC_WRAPPER(libc, pvalloc)
ALLOC_WRAPPER(libstdcxx, _Znwm)
ALLOC_WRAPPER(libstdcxx, _Znam)
ALLOC_WRAPPER(libcxx, _Znwm)
ALLOC_WRAPPER(libcxx, _Znam)

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, aligned_alloc, (void *), (size_t align,
                                                     size_t size)) {
  GET_ALLOCATOR(aligned_alloc);
  auto addr = aligned_alloc(align, size);
  RETURN_TAINTED_ADDRESS;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, memalign, (void *), (size_t align, size_t size)) {
  GET_ALLOCATOR(memalign);
  auto addr = memalign(align, size);
  RETURN_TAINTED_ADDRESS;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, posix_memalign, (int), (void **addr_ptr,
                                                   size_t align, size_t size)) {
  GET_ALLOCATOR(posix_memalign);
  auto ret = posix_memalign(addr_ptr, align, size);
  if (!ret) {
    auto type_id = TypeIdFor(ret_address, size);
    *addr_ptr = TaintAddress(*addr_ptr, type_id);
  }
  return ret;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, calloc, (void *), (size_t count, size_t size)) {
  GET_ALLOCATOR(calloc);
  auto addr = calloc(count, size);
  RETURN_TAINTED_ADDRESS;
}

// Make a wrapper for a re-allocator.
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

#endif  // GRANARY_WHERE_user

// Linked list of block meta-datas.
static BlockMetaData *gAllBlocks = nullptr;
static SpinLock gAllBlocksLock;

// Meta-data iterator, where the meta-data is chained together via the
// `BlockTypeInfo` type.
typedef MetaDataLinkedListIterator<TypeMetaData> BlockTypeInfoIterator;

// Taint a basic block with some type id.
static void TaintBlock(TypeMetaData *meta, void *address) {
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
      GetMetaData<TypeMetaData>(op->block),
      op->watched_reg_op));
}

}  // namespace

// Simple tool for static and dynamic basic block counting.
class PolyCode : public InstrumentationTool {
 public:
  virtual ~PolyCode(void) = default;

  virtual void Init(InitReason) {
#ifdef GRANARY_WHERE_user
    // Wrap libc.
    AddFunctionWrapper(&WRAP_FUNC_libc_malloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_valloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_pvalloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_aligned_alloc);
    AddFunctionWrapper(&WRAP_FUNC_libc_memalign);
    AddFunctionWrapper(&WRAP_FUNC_libc_posix_memalign);
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
#endif  // GRANARY_WHERE_user

    AddWatchpointInstrumenter(TaintBlockMeta);
    AddMetaData<TypeMetaData>();
  }

  static void LogTypeInfo(uint64_t type_id, AppPC ret_address,
                          size_t size_order) {
    auto offset = os::ModuleOffsetOfPC(ret_address);
    os::Log("T %u %lu B %s %lx\n", type_id, size_order,
            offset.module->Name(), offset.offset);
  }

  static void LogMetaInfo(BlockMetaData *meta) {
    auto app_meta = MetaDataCast<AppMetaData *>(meta);
    auto type_meta = MetaDataCast<TypeMetaData *>(meta);
    auto offset = os::ModuleOffsetOfPC(app_meta->start_pc);
    os::Log("B %s %lx", offset.module->Name(), offset.offset);
    auto sep = " Ts ";
    for (auto type_id : type_meta->type_ids) {
      os::Log("%s%lu", sep, static_cast<uint64_t>(type_id));
      sep = ",";
    }
    os::Log("\n");
  }

  virtual void Exit(ExitReason) {
    ForEachType(LogTypeInfo);
    SpinLockedRegion locker(&gAllBlocksLock);
    for (auto meta : BlockTypeInfoIterator(gAllBlocks)) {
      LogMetaInfo(meta);
    }
    gAllBlocks = nullptr;
  }

  // Build a global chain of all basic block meta-data.
  virtual void InstrumentBlock(DecodedBasicBlock *block) {
    if (IsA<CompensationBasicBlock *>(block)) return;

    auto meta = block->MetaData();
    auto type_meta = MetaDataCast<TypeMetaData *>(meta);
    SpinLockedRegion locker(&gAllBlocksLock);
    type_meta->next = gAllBlocks;
    gAllBlocks = meta;
  }
};

// Initialize the `poly_code` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<PolyCode>("poly_code", {"wrap_func", "watchpoints"});
}
