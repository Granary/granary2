# Copyright 2014 Peter Goodman, all rights reserved.

obj-m := granary.o
granary-y := abi.o command.o entry.o interrupt.o module.o
granary-y += slot.o symbol.o syscall.o granary_bin.o

# Extra C flags.
cflags-y := -g3 -I$(GRANARY_SRC_DIR)
ccflags-y := -g3 -I$(GRANARY_SRC_DIR)

# Define some extra symbols that we need to blank out on XED's behalf.
ldflags-y := --defsym=abort=granary_unreachable
ldflags-y += --defsym=fprintf=granary_unreachable
ldflags-y += --defsym=stderr=0

# Export some Granary functions to be callable by instrumented kernel code.
ldflags-y += -T $(GRANARY_SRC_DIR)/linker.lds
