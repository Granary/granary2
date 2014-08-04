# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all lint

include ../../Makefile.inc

# Find assembly files specific to the architecture being instrumented (e.g. x86-64).
ARCH_ASM_SRC_FILES = $(shell find $(GRANARY_ARCH_SRC_DIR) -type f -name '*.asm')
ARCH_ASM_OBJ_FILES = $(subst $(GRANARY_ARCH_SRC_DIR),$(GRANARY_ARCH_BIN_DIR),$(ARCH_ASM_SRC_FILES:.asm=.o))

# Find C++ files specific to the architecture being instrumented (e.g. x86-64).
ARCH_CC_SRC_FILES = $(shell find $(GRANARY_ARCH_SRC_DIR) -type f -name '*.cc')
ARCH_CC_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(ARCH_CC_SRC_FILES:.cc=.bc))

GRANARY_ARCH_MERGED_BC_BC = $(GRANARY_ARCH_BIN_DIR)/arch_ll.bc
GRANARY_ARCH_MERGED_BC_OBJ = $(GRANARY_ARCH_BIN_DIR)/arch_ll.o

# Link together all compiled .bc files into a single .bc file.
$(GRANARY_ARCH_MERGED_BC_OBJ) :: $(ARCH_CC_OBJ_FILES)
	@echo "Linking intermediate bitcode files into $(GRANARY_ARCH_MERGED_BC_OBJ)."
	@$(GRANARY_LLVM_LINK) $(ARCH_CC_OBJ_FILES) \
		-o $(GRANARY_ARCH_MERGED_BC_BC)
	@$(GRANARY_CXX) -Qunused-arguments $(GRANARY_CXX_FLAGS) \
		-c $(GRANARY_ARCH_MERGED_BC_BC) \
		-o $(GRANARY_ARCH_MERGED_BC_OBJ)

# Link together the merged .o files, the .o files related to arch-specific
# assembly files.
$(GRANARY_ARCH_OBJ) :: $(GRANARY_ARCH_MERGED_BC_OBJ) $(ARCH_ASM_OBJ_FILES)
	@echo "Linking together intermediate object files into $(GRANARY_ARCH_OBJ)."
	@$(GRANARY_LD) -g -r \
		$(GRANARY_ARCH_MERGED_BC_OBJ) \
		$(ARCH_ASM_OBJ_FILES) \
		-o $(GRANARY_ARCH_OBJ)

# Lint all Granary code.
lint:
	@echo "Linting arch-specific Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_ARCH_SRC_DIR) -type f -name '*.h' -or -name '*.cc')

all: $(GRANARY_LINT_TARGET) $(GRANARY_ARCH_OBJ)
