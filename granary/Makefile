# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all lint

include ../Makefile.inc

GRANARY_MERGED_BC_BC = $(GRANARY_BIN_DIR)/granary/merged_ll.bc
GRANARY_MERGED_BC_OBJ = $(GRANARY_BIN_DIR)/granary/merged_ll.o

GRANARY_EXCLUDE_FIND = 
GRANARY_EXCLUDE_FIND += -not -path "$(GRANARY_SRC_DIR)/granary/arch/*"

# Get all subdirectories of granary that contain source files to compile.
GRANARY_CC_SRC_FILES := $(shell find $(GRANARY_SRC_DIR)/granary -type f -name '*.cc' $(GRANARY_EXCLUDE_FIND))
GRANARY_CC_SRC_FILES += $(shell find $(GRANARY_SRC_DIR)/granary/arch -maxdepth 1 -type f -name '*.cc')
GRANARY_CC_OBJ_FILES := $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(GRANARY_CC_SRC_FILES:.cc=.bc))

# Find assembly files specific to the architecture being instrumented (e.g. x86-64).
ARCH_ASM_SRC_FILES = $(shell find $(GRANARY_ARCH_SRC_DIR) -type f -name '*.asm')
ARCH_ASM_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(ARCH_ASM_SRC_FILES:.asm=.o))

# Find C++ files specific to the architecture being instrumented (e.g. x86-64).
ARCH_CC_SRC_FILES = $(shell find $(GRANARY_ARCH_SRC_DIR) -type f -name '*.cc')
ARCH_CC_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(ARCH_CC_SRC_FILES:.cc=.bc))

# All object files to be compiled.
GRANARY_OBJ_FILES = $(GRANARY_CC_OBJ_FILES) $(ARCH_CC_OBJ_FILES)

# Link together all compiled .bc files into a single .bc file.
$(GRANARY_MERGED_BC_OBJ) :: $(GRANARY_OBJ_FILES)
	@echo "Linking intermediate bitcode files into $(GRANARY_MERGED_BC_OBJ)."
	@$(GRANARY_LLVM_LINK) $(GRANARY_OBJ_FILES) \
		-o $(GRANARY_MERGED_BC_BC)
	@$(GRANARY_CXX) -Qunused-arguments $(GRANARY_CXX_FLAGS) \
		-c $(GRANARY_MERGED_BC_BC) \
		-o $(GRANARY_MERGED_BC_OBJ)

# Link together the merged .o files, the .o files related to arch-specific
# assembly files, and any archives into a single large object file.
$(GRANARY_MERGED_OBJ) :: $(GRANARY_MERGED_BC_OBJ) $(ARCH_ASM_OBJ_FILES)
	@echo "Linking together intermediate object files into $(GRANARY_MERGED_OBJ)."
	@$(GRANARY_LD) -g -r \
		$(GRANARY_MERGED_BC_OBJ) \
		$(ARCH_ASM_OBJ_FILES) \
		-o $(GRANARY_MERGED_OBJ)

# Lint all Granary code.
lint:
	@echo "Linting Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_SRC_DIR)/granary -type f -name '*.h' -or -name '*.cc')

all: $(GRANARY_LINT_TARGET) $(GRANARY_MERGED_OBJ)
