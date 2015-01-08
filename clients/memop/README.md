memop
=====

This is a helper tool that other tools (such as `shadow_memory` and `watchpoints`)
can depend upon in order to gain access to all memory accesses. The `memop` tool
provides enables other tools to register a callback that will be called for each
memory operand of each native instruction.

The callback takes a single argument, a `const InstrumentedMemoryOperand &`. This
gives other tools direct access to the following:

  1. `block`: The decoded block containing the memory access operand.
  2. `instr`: The application instruction performing the memory access.
  3. `native_mem_op`: A reference to a `MemoryOperand` that points to the native
    memory operand being referenced. This is sometimes useful when tools want
    to replace memory operands directly.
  4. `native_addr_op`: A `RegisterOperand` that contains the memory address being
    accessed.
  5. `operand_number`: The operand number (`0` or `1`) of the memory operand
    within the instruction. This is often useful when virtual registers can be
    re-used across instructions, but must be specific to individual operands due
    to potential interleavings of inline assembly.
