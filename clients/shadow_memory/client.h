/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_SHADOW_MEMORY_CLIENT_H_
#define CLIENTS_SHADOW_MEMORY_CLIENT_H_

#include <granary.h>

// Forward declaration.
class DirectMappedShadowMemory;

class ShadowedMemoryOperand {
 public:
  // Block that contains `instr`.
  granary::DecodedBasicBlock * const block;

  // Instruction that contains the memory operand `native_mem_op`.
  granary::NativeInstruction * const instr;

  // Memory operand that is accessing native memory.
  const granary::MemoryOperand &native_mem_op;

  // Memory operand that can be used to access the shadow memory.
  const granary::RegisterOperand &shadow_addr_op;

  // Register operand containing the native address accessed by
  // `address_reg_op`.
  const granary::RegisterOperand &native_addr_op;

  // Which memory operand (of the instruction) is being shadowed? This is
  // going to be `0` or `1`.
  const size_t operand_number;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ShadowedMemoryOperand);
};

// Represents a description of a shadow memory structure.
class ShadowStructureDescription {
 public:
  ShadowStructureDescription *next;
  void (*instrumenter)(const ShadowedMemoryOperand &);
  size_t offset;
  const size_t size;
  const size_t align;

  // Have we registered this shadow data structure?
  bool is_registered;

  // Inline assembly for getting the address
  char offset_asm_instruction[32];
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
  false,
  {'\0'}
};

// Tells the shadow memory tool about a structure to be stored in shadow
// memory.
void AddShadowStructure(ShadowStructureDescription *desc,
                        void (*instrumenter)(const ShadowedMemoryOperand &));

// Returns the address of the shadow memory descriptor.
template <typename T>
inline static ShadowStructureDescription *ShadowDescription(void) {
  return &(GetShadowStructureDescription<T>::kDescription);
}

template <typename T>
inline static void AddShadowStructure(
    void (*instrumenter)(const ShadowedMemoryOperand &)) {
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
