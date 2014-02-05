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
		-o $(GRANARY_SHIPPED_FILE)
	
	# Make an `.cmd` file for the shipped object file.
	@echo "cmd_$(GRANARY_SHIPPED_FILE) := " > \
		$(GRANARY_BIN_DIR)/.$(GRANARY_NAME)_merged.o_shipped.cmd

all: $(GRANARY_OBJS)
	@cp $(GRANARY_SRC_DIR)/granary/kernel/entry.c \
		$(GRANARY_BIN_DIR)/granary/kernel
