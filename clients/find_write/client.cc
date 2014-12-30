/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_mask(address_mask, std::numeric_limits<uintptr_t>::max(),
    "Mask that is used to filter addresses. If all bits are set then all "
    "addresses are accepted.\n"
    "\n"
    "If `(addr & addr_mask) != 0` then the write is recorded "
    "into an in-memory log. Log entries contain four "
    "components:\n"
    "  1) Target address of the write.\n"
    "  2) Value being written to memory.\n"
    "  3) Application address of the instruction doing the\n"
    "     write.\n"
    "  4) Cache address of the instruction doing the write.",

    "find_write");

GRANARY_DEFINE_mask(value_mask, std::numeric_limits<uintptr_t>::max(),
    "Mask that is used to filter values. If all bits are set then all values "
    "are accepted.\n"
    "\n"
    "If `(value & value_mask) != 0` then the write is recorded "
    "into an in-memory log.",

    "find_write");

GRANARY_DEFINE_positive_uint(min_write_size, 1,
    "The minimum size of a write (in bytes) to memory that should be checked "
    "and logged.",

    "find_write");

namespace {

// Report an 8-bit memory write.
static void ReportWrite8(const char *mod_name, uint64_t offset,
                         void *addr, uint8_t value) {
  os::Log("W 1 %p %x B %s %lx\n", addr, value, mod_name, offset);
}

// Report a 16-bit memory write.
static void ReportWrite16(const char *mod_name, uint64_t offset,
                          void *addr, uint16_t value) {
  os::Log("W 2 %p %x B %s %lx\n", addr, value, mod_name, offset);
}

// Report a 32-bit memory write.
static void ReportWrite32(const char *mod_name, uint64_t offset,
                          void *addr, uint32_t value) {
  os::Log("W 4 %p %x B %s %lx\n", addr, value, mod_name, offset);
}

// Report a 64-bit memory write.
static void ReportWrite64(const char *mod_name, uint64_t offset,
                          void *addr, uint64_t value) {
  os::Log("W 8 %p %lx B %s %lx\n", addr, value, mod_name, offset);
}

// Choose what function to use to log the memory write.
static AppPC GetWriteReporter(Operand &op) {
  switch (op.BitWidth()) {
    case 8:
      return UnsafeCast<AppPC>(ReportWrite8);
    case 16:
      return UnsafeCast<AppPC>(ReportWrite16);
    case 32:
      return UnsafeCast<AppPC>(ReportWrite32);
    case 64:
      return UnsafeCast<AppPC>(ReportWrite64);
    default:
      GRANARY_ASSERT(false);
      return nullptr;
  }
}
}  // namespace

// Example tool that instruments memory writes of the form:
//    MOV [addr_reg], value_reg
//    MOV [addr_reg], value_imm
// This tool logs all writes where `0 != (addr_reg & FLAG_address_mask)` and
// `0 != (value_reg/_imm & FLAG_value_mask)`.
class MemoryWriteInstrumenter : public InstrumentationTool {
 public:
  virtual ~MemoryWriteInstrumenter(void) = default;

  // Writing an immediate constant to memory. Avoid a check on the value mask.
  void InstrumentMemoryWrite(DecodedBlock *block, os::ModuleOffset loc,
                             NativeInstruction *instr, VirtualRegister dst_addr,
                             MemoryOperand &mloc, ImmediateOperand &value) {
    if (FLAG_value_mask && !(FLAG_value_mask & value.UInt())) return;
    RegisterOperand address(dst_addr);
    ImmediateOperand address_mask(FLAG_address_mask, arch::ADDRESS_WIDTH_BYTES);

    lir::InlineAssembly asm_(address, address_mask, value);
    if (FLAG_address_mask &&
        std::numeric_limits<uintptr_t>::max() != FLAG_address_mask) {
      asm_.InlineBefore(instr,
          "MOV r64 %4, i64 %1;"
          "TEST r64 %4, r64 %0;"
          "JZ l %3;"_x86_64);
    }
    instr->InsertBefore(lir::InlineFunctionCall(block, GetWriteReporter(mloc),
                                                loc.module->Name(), loc.offset,
                                                address, value));

    asm_.InlineBefore(instr, "@LABEL %3:"_x86_64);
  }

  // Writing the value of a register to memory.
  void InstrumentMemoryWrite(DecodedBlock *block, os::ModuleOffset loc,
                             NativeInstruction *instr, VirtualRegister dst_addr,
                             MemoryOperand &mloc, RegisterOperand &value) {
    RegisterOperand address(dst_addr);
    ImmediateOperand address_mask(FLAG_address_mask, arch::ADDRESS_WIDTH_BYTES);
    ImmediateOperand value_mask(FLAG_value_mask, arch::ADDRESS_WIDTH_BYTES);

    lir::InlineAssembly asm_(address, address_mask, value, value_mask);

    if (FLAG_address_mask &&
        std::numeric_limits<uintptr_t>::max() != FLAG_address_mask) {
      asm_.InlineBefore(instr,
          "MOV r64 %5, i64 %1;"
          "TEST r64 %5, r64 %0;"
          "JZ l %4;"_x86_64);
    }
    if (FLAG_value_mask &&
        std::numeric_limits<uintptr_t>::max() != FLAG_value_mask) {
      asm_.InlineBefore(instr,
          "MOV r64 %5, i64 %3;"
          "TEST r64 %5, r64 %2;"
          "JZ l %4;"_x86_64);
    }
    instr->InsertBefore(lir::InlineFunctionCall(block, GetWriteReporter(mloc),
                                                loc.module->Name(), loc.offset,
                                                address, value));
    asm_.InlineBefore(instr, "@LABEL %4:");
  }

  // Instrument every memory write instruction.
  virtual void InstrumentBlock(DecodedBlock *block) {
    auto module = os::ModuleContainingPC(block->StartAppPC());
    for (auto instr : block->AppInstructions()) {
      if (!StringsMatch("MOV", instr->OpCodeName())) continue;

      MemoryOperand dst;
      VirtualRegister dst_addr;
      ImmediateOperand src_imm;
      RegisterOperand src_reg;
      auto pc = instr->DecodedPC();

      if (instr->MatchOperands(WriteTo(dst), ReadFrom(src_imm))) {
        if (dst.ByteWidth() >= FLAG_min_write_size &&
            dst.MatchRegister(dst_addr) && dst_addr.IsGeneralPurpose()) {
          InstrumentMemoryWrite(block, module->OffsetOfPC(pc), instr,
                                dst_addr, dst, src_imm);
        }
      } else if (instr->MatchOperands(WriteTo(dst), ReadFrom(src_reg))) {
        if (dst.ByteWidth() >= FLAG_min_write_size &&
            dst.MatchRegister(dst_addr) && dst_addr.IsGeneralPurpose() &&
            src_reg.Register().IsGeneralPurpose()) {
          InstrumentMemoryWrite(block, module->OffsetOfPC(pc), instr,
                                dst_addr, dst, src_reg);
        }
      }
    }
  }
};

// Initialize the `find_write` tool.
GRANARY_ON_CLIENT_INIT() {
  if (HAS_FLAG_address_mask && !FLAG_address_mask) return;
  if (HAS_FLAG_value_mask && !FLAG_value_mask) return;
  AddInstrumentationTool<MemoryWriteInstrumenter>("find_write");
}
