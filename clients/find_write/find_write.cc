/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

GRANARY_DEFINE_mask(address_mask, 0,
    "Mask that is used to filter addresses. If zero then all addresses are "
    "accepted.\n"
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

GRANARY_DEFINE_mask(value_mask, 0,
    "Mask that is used to filter values. If zero then all values are "
    "accepted.\n"
    "\n"
    "If `(value & value_mask) != 0` then the write is recorded "
    "into an in-memory log.",

    "find_write");

namespace {
static void ReportWrite8(AppPC pc, void *address, uint8_t value) {
  os::Log(os::LogDebug, "1,%p,%p,%x\n", pc, address, value);
}
static void ReportWrite16(AppPC pc, void *address, uint16_t value) {
  os::Log(os::LogDebug, "2,%p,%p,%x\n", pc, address, value);
}
static void ReportWrite32(AppPC pc, void *address, uint32_t value) {
  os::Log(os::LogDebug, "4,%p,%p,%x\n", pc, address, value);
}
static void ReportWrite64(AppPC pc, void *address, uint64_t value) {
  os::Log(os::LogDebug, "8,%p,%p,%lx\n", pc, address, value);
}
static AppPC GetWriteReporter(Operand &op) {
  switch (op.BitWidth()) {
    case 8:
      return UnsafeCast<AppPC>(ReportWrite8);
    case 16:
      return UnsafeCast<AppPC>(ReportWrite16);
    case 32:
      return UnsafeCast<AppPC>(ReportWrite32);
    case 64:
    default:
      return UnsafeCast<AppPC>(ReportWrite64);
  }
}
}  // namespace

// Tool that implements several kernel-space special cases for instrumenting
// common binaries.
class MemoryWriteInstrumenter : public InstrumentationTool {
 public:
  virtual ~MemoryWriteInstrumenter(void) = default;

  // Writing an immediate constant to memory.
  void InstrumentMemoryWrite(DecodedBasicBlock *block, AppPC pc,
                             NativeInstruction *instr, VirtualRegister dst_addr,
                             ImmediateOperand &value) {
    if (FLAG_value_mask && !(FLAG_value_mask & value.UInt())) return;
    RegisterOperand address(dst_addr);
    ImmediateOperand address_mask(FLAG_address_mask, arch::ADDRESS_WIDTH_BYTES);

    lir::InlineAssembly asm_({&address, &address_mask, &value});
    if (FLAG_address_mask) {
      asm_.InlineBefore(instr,
          "MOV r64 %4, i64 %1;"
          "TEST r64 %4, r64 %0;"
          "JZ l %3;"_x86_64);
    }
    instr->InsertBefore(lir::CallWithArgs(
        block, GetWriteReporter(value), pc, address, value));
    asm_.InlineBefore(instr, "LABEL %3:"_x86_64);
  }

  // Writing the value of a register to memory.
  void InstrumentMemoryWrite(DecodedBasicBlock *block, AppPC pc,
                             NativeInstruction *instr, VirtualRegister dst_addr,
                             RegisterOperand &value) {
    RegisterOperand address(dst_addr);
    ImmediateOperand address_mask(FLAG_address_mask, arch::ADDRESS_WIDTH_BYTES);
    ImmediateOperand value_mask(FLAG_value_mask, arch::ADDRESS_WIDTH_BYTES);

    lir::InlineAssembly asm_({&address, &address_mask, &value, &value_mask});
    if (FLAG_address_mask) {
      asm_.InlineBefore(instr,
          "MOV r64 %5, i64 %1;"
          "TEST r64 %5, r64 %0;"
          "JZ l %4;"_x86_64);
    }
    if (FLAG_value_mask) {
      asm_.InlineBefore(instr,
          "MOV r64 %5, i64 %3;"
          "TEST r64 %5, r64 %2;"
          "JZ l %4;"_x86_64);
    }
    instr->InsertBefore(lir::CallWithArgs(
        block, GetWriteReporter(value), pc, address, value));
    asm_.InlineBefore(instr, "LABEL %4:");
  }

  virtual void InstrumentBlock(DecodedBasicBlock *block) {
    AppPC pc(nullptr);
    for (auto instr : block->Instructions()) {
      if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
        if(auto instr_pc = ninstr->DecodedPC()) {
          pc = instr_pc;
        }

        if (!StringsMatch("MOV", ninstr->OpCodeName())) continue;

        MemoryOperand dst;
        VirtualRegister dst_addr;
        ImmediateOperand src_imm;
        RegisterOperand src_reg;

        if (ninstr->MatchOperands(WriteTo(dst), ReadFrom(src_imm))) {
          if (dst.MatchRegister(dst_addr)) {
            InstrumentMemoryWrite(block, pc, ninstr, dst_addr, src_imm);
          }
        } else if (ninstr->MatchOperands(WriteTo(dst), ReadFrom(src_reg))) {
          if (dst.MatchRegister(dst_addr)) {
            InstrumentMemoryWrite(block, pc, ninstr, dst_addr, src_reg);
          }
        }
      }
    }
  }
};

// Initialize the `kernel` tool.
GRANARY_CLIENT_INIT({
  RegisterInstrumentationTool<MemoryWriteInstrumenter>("find_write");
})
