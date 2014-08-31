# Copyright 2014 Peter Goodman, all rights reserved.

obj-m := granary.o
granary-y := command.o entry.o module.o
granary-y += slot.o symbol.o syscall.o granary_bin.o

# Extra C flags.
cflags-y := -g3 -I$(GRANARY_SRC_DIR)
ccflags-y := -g3 -I$(GRANARY_SRC_DIR)

# Export some Granary functions to be callable by instrumented kernel code.
ldflags-y += -T $(GRANARY_SRC_DIR)/linker.lds
