# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all injector

include ../../Makefile.inc

GRANARY_ASM_SRC_FILES = $(shell find $(GRANARY_BIN_DIR) -type f -name '*.S')
GRANARY_ASM_OBJ_FILES = $(GRANARY_ASM_SRC_FILES:.S=.o)

GRANARY_LL_FILES = $(shell find $(GRANARY_BIN_DIR) -type f -name '*.ll')

# Most of the output file name. Used to link together .ll files, generate a .o
# file, and make either a .ko.
GRANARY_INTERMEDIATE_FILE = $(GRANARY_BIN_DIR)/$(GRANARY_NAME)
GRANARY_SHIPPED_FILE = $(GRANARY_INTERMEDIATE_FILE)_merged.o_shipped

# Remove the final linked together file, just in case it is present.
GRANARY_LL_FILES := $(GRANARY_LL_FILES:$(GRANARY_INTERMEDIATE_FILE).ll=)
GRANARY_OBJS =  $(GRANARY_SHIPPED_FILE) $(GRANARY_ASM_OBJ_FILES)
GRANARY_KBUILD_OBJS = $(subst $(GRANARY_BIN_DIR)/,,$(GRANARY_OBJS))

GRANARY_LD_FLAGS_LATE += "-Wl,-Bsymbolic"

# Makefile contents for Granary's kernel module Makefile.
GRANARY_KBUILD =
GRANARY_KBUILD += "obj-m += $(GRANARY_NAME).o\n"
GRANARY_KBUILD += "$(GRANARY_NAME)-y := $(GRANARY_KBUILD_OBJS) granary/kernel/entry.o\n"
GRANARY_KBUILD += "ccflags-y += $(GRANARY_LD_FLAGS_LATE) -I$(GRANARY_SRC_DIR)\n"
GRANARY_KBUILD += "asflags-y += $(GRANARY_LD_FLAGS_LATE)\n"
GRANARY_KBUILD += "ldflags-y += -Bsymbolic\n"

GRANARY_CMD_FILE ="cmd_$(GRANARY_BIN_DIR)/granary_merged.o_shipped := "

# Link together all compiled .ll files into a single .ll file.
$(GRANARY_INTERMEDIATE_FILE).ll: $(GRANARY_LL_FILES)
	@echo "Linking intermediate bitcode files."
	@llvm-link $(GRANARY_LL_FILES) -o $(GRANARY_INTERMEDIATE_FILE).ll

# Compile the linked together .ll file into an object file. This should enable
# a simple form of link-time optimization.
$(GRANARY_SHIPPED_FILE): $(GRANARY_INTERMEDIATE_FILE).ll
	@echo "Building object $@"
	@$(GRANARY_CC) $(GRANARY_LD_FLAGS) \
		-c $(GRANARY_INTERMEDIATE_FILE).ll \
		-o $(GRANARY_INTERMEDIATE_FILE)_merged.o_shipped
		
	# Create the "init function" that runs Granary's constructors.
	#@echo ".section .bss.granary_constructors\n.align 8\n\n.globl granary_constructors_begin\ngranary_constructors_begin:\n" \
	#	> $(GRANARY_BIN_DIR)/granary_construct.S
	#@readelf --syms --wide $(GRANARY_INTERMEDIATE_FILE)_merged.o_shipped \
	#	| grep -oh '_GLOBAL__I_.*' \
	#	| sed 's/^/.quad /' \
	#	>> $(GRANARY_BIN_DIR)/granary_construct.S
	#@echo ".globl granary_constructors_end\ngranary_constructors_end:\n\n" \
	#	>> $(GRANARY_BIN_DIR)/granary_construct.S

all: $(GRANARY_OBJS)
	# Copies the kernel space module entrypoint and Makefile into the bin
	# folder for final compilation.
	@echo $(GRANARY_KBUILD) > $(GRANARY_BIN_DIR)/Makefile
	@echo $(GRANARY_CMD_FILE) > \
		$(GRANARY_BIN_DIR)/.$(GRANARY_NAME)_merged.o_shipped.cmd
	@cp $(GRANARY_SRC_DIR)/granary/kernel/entry.c \
		$(GRANARY_BIN_DIR)/granary/kernel
	
	# Tells the kernel to compile Granary as a kernel module.
	make -C $(GRANARY_KERNEL_DIR) M=$(GRANARY_BIN_DIR) modules
