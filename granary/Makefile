# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all lint

include $(GRANARY_SRC_DIR)/Makefile.inc

GRANARY_MERGED_BC_BC = $(GRANARY_BIN_DIR)/granary/merged_ll.bc
GRANARY_MERGED_BC_OBJ = $(GRANARY_BIN_DIR)/granary/merged_ll.o

# Get all subdirectories of granary that contain source files to compile.
GRANARY_CC_SRC_FILES := $(shell find $(GRANARY_SRC_DIR)/granary -type f -name '*.cc')
GRANARY_CC_OBJ_FILES := $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(GRANARY_CC_SRC_FILES:.cc=.bc))

# All object files to be compiled.
GRANARY_OBJ_FILES = $(GRANARY_CC_OBJ_FILES)

# Link together all compiled .bc files into a single .bc file.
$(GRANARY_MERGED_BC_OBJ) :: $(GRANARY_OBJ_FILES)
	@echo "Linking intermediate bitcode files into $@."
	@$(GRANARY_LLVM_LINK) $^ \
		-o $(GRANARY_MERGED_BC_BC)
	@$(GRANARY_CXX) -Qunused-arguments $(GRANARY_CXX_FLAGS) \
		-c $(GRANARY_MERGED_BC_BC) \
		-o $@

# Link together the merged .o files, the .o files related to arch-specific
# assembly files, and any archives into a single large object file.
$(GRANARY_MERGED_OBJ) :: $(GRANARY_MERGED_BC_OBJ)
	@echo "Linking together intermediate object files into $(GRANARY_MERGED_OBJ)."
	@$(GRANARY_LD) -g -r \
		$(GRANARY_MERGED_BC_OBJ) \
		-o $(GRANARY_MERGED_OBJ)

# Lint all Granary code.
lint:
	@echo "Linting Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_SRC_DIR)/granary -type f -name '*.h' -or -name '*.cc')

all: $(GRANARY_LINT_TARGET) $(GRANARY_MERGED_OBJ)
