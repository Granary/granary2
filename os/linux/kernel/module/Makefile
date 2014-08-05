# Copyright 2014 Peter Goodman, all rights reserved.

obj-m := granary.o
granary-y := abi.o constructor.o entry.o interrupt.o module.o symbol.o
granary-y += syscall.o granary_bin.o

# Extra C flags.
cflags-y := -I$(GRANARY_SRC_DIR)
ccflags-y := -I$(GRANARY_SRC_DIR)

# Define some extra symbols that we need to blank out on XED's behalf.
ldflags-y := --defsym=abort=granary_break_on_fault
ldflags-y += --defsym=fprintf=granary_break_on_fault
ldflags-y += --defsym=stderr=0
