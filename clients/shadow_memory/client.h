/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_SHADOW_MEMORY_CLIENT_H_
#define CLIENTS_SHADOW_MEMORY_CLIENT_H_

#include <granary.h>

// Forward declaration.
class DirectMappedShadowMemory;

class ShadowedOperand {
 public:
  granary::DecodedBasicBlock * const block;

  // Instruction that contains the memory operand `mem_op`.
  granary::NativeInstruction * const instr;

  // Memory operand that is accessing native memory.
  const granary::MemoryOperand &native_mem_op;

  // Memory operand that can be used to access the shadow memory.
  const granary::RegisterOperand &shadow_addr_op;

  // Register operand containing the native address accessed by
  // `address_reg_op`.
  const granary::RegisterOperand &native_addr_op;

 protected:
  friend class DirectMappedShadowMemory;

  inline ShadowedOperand(granary::DecodedBasicBlock *block_,
                         granary::NativeInstruction *instr_,
                         const granary::MemoryOperand &native_mem_op_,
                         const granary::RegisterOperand &shadow_addr_op_,
                         const granary::RegisterOperand &native_addr_op_)
      : block(block_),
        instr(instr_),
        native_mem_op(native_mem_op_),
        shadow_addr_op(shadow_addr_op_),
        native_addr_op(native_addr_op_) {}

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ShadowedOperand);
};

// Represents a description of a shadow memory structure.
class ShadowStructureDescription {
 public:
  ShadowStructureDescription *next;
  void (*instrumenter)(const ShadowedOperand &);
  size_t offset;
  const size_t size;
  const size_t align;
  bool is_registered;
};

// Used to initialize and get a description for some structure to be stored
// in shadow memory.
template <typename T>
class GetShadowStructureDescription {
 public:
  static_assert(std::is_trivially_default_constructible<T>(),
                "Type `T` must have a trivial default constructor, as shadow "
                "memory is default-initialized with zero bytes.");

  static ShadowStructureDescription kDescription GRANARY_EARLY_GLOBAL;

 private:
  GetShadowStructureDescription(void) = delete;
};

template <typename T>
ShadowStructureDescription GetShadowStructureDescription<T>::kDescription = {
  nullptr,
  nullptr,
  0,
  sizeof(T),
  alignof(T),
  false
};

// Tells the shadow memory tool about a structure to be stored in shadow
// memory.
void AddShadowStructure(ShadowStructureDescription *desc,
                        void (*instrumenter)(const ShadowedOperand &));

// Returns the address of the shadow memory descriptor.
template <typename T>
inline static ShadowStructureDescription *ShadowDescription(void) {
  return &(GetShadowStructureDescription<T>::kDescription);
}

template <typename T>
inline static void AddShadowStructure(
    void (*instrumenter)(const ShadowedOperand &)) {
  AddShadowStructure(ShadowDescription<T>(), instrumenter);
}

// Returns the address of some shadow object.
uintptr_t ShadowOf(const ShadowStructureDescription *desc, uintptr_t addr);

template <typename ShadowT, typename AddrT>
inline static ShadowT *ShadowOf(AddrT *ptr) {
  return reinterpret_cast<ShadowT *>(ShadowOf(
      ShadowDescription<ShadowT>(), reinterpret_cast<uintptr_t>(ptr)));
}

#endif  // CLIENTS_SHADOW_MEMORY_CLIENT_H_
