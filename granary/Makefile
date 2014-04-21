# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all lint

include ../Makefile.inc

GRANARY_LINT_TARGET =
GRANARY_TOOL_OBJ_TARGET =

# Should Granary's main code be linted?
ifeq (1,$(GRANARY_LINT_CODE))
	GRANARY_LINT_TARGET = lint
endif
ifeq (user,$(GRANARY_WHERE))
	GRANARY_TOOL_OBJ_TARGET = $(GRANARY_TOOL_OBJ)
endif

GRANARY_MERGED_BC_BC = $(GRANARY_BIN_DIR)/granary_merged_ll.bc
GRANARY_MERGED_BC_OBJ = $(GRANARY_BIN_DIR)/granary_merged_ll.o

# Go find already compiled files that were not compiled by this Makefile.
GRANARY_EXCLUDE_FIND = -not -path "$(GRANARY_BIN_DIR)/granary/*"
GRANARY_OTHER_AR_FILES := $(shell find $(GRANARY_BIN_DIR) -type f -name '*.a' $(GRANARY_EXCLUDE_FIND))
GRANARY_OTHER_OBJ_FILES := $(shell find $(GRANARY_BIN_DIR) -type f -name '*.bc' $(GRANARY_EXCLUDE_FIND))
GRANARY_OTHER_OBJ_FILES := $(filter-out $(GRANARY_MERGED_BC_BC),$(GRANARY_OTHER_OBJ_FILES))

# TODO(pag): Will need to filter-out non Linux OS's in the future.
GRANARY_EXCLUDE_FIND = 
GRANARY_EXCLUDE_FIND += -not -path "$(GRANARY_SRC_DIR)/granary/$(GRANARY_NOT_WHERE)/*"
GRANARY_EXCLUDE_FIND += -not -path "$(GRANARY_SRC_DIR)/granary/arch/*"
GRANARY_EXCLUDE_FIND += -not -path "$(GRANARY_SRC_DIR)/granary/arch/*"

# Get all subdirectories of granary that contain source files to compile.
GRANARY_CC_SRC_FILES := $(shell find $(GRANARY_SRC_DIR)/granary -type f -name '*.cc' $(GRANARY_EXCLUDE_FIND))
GRANARY_CC_OBJ_FILES := $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(GRANARY_CC_SRC_FILES:.cc=.bc))

# Find assembly files specific to the architecture being instrumented (e.g. x86-64).
ARCH_ASM_SRC_FILES = $(shell find $(GRANARY_ARCH_SRC_DIR) -type f -name '*.asm')
ARCH_ASM_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(ARCH_ASM_SRC_FILES:.asm=.o))

# Find C++ files specific to the architecture being instrumented (e.g. x86-64).
ARCH_CC_SRC_FILES = $(shell find $(GRANARY_ARCH_SRC_DIR) -type f -name '*.cc')
ARCH_CC_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(ARCH_CC_SRC_FILES:.cc=.bc))

# All object files to be compiled.
GRANARY_OBJ_FILES = $(GRANARY_CC_OBJ_FILES) $(ARCH_CC_OBJ_FILES)

GRANARY_TOOL_OBJ_DEPS =
GRANARY_TOOL_OBJ_DEPS += $(GRANARY_ARCH_BIN_DIR)/asm/string.o
GRANARY_TOOL_OBJ_DEPS += $(GRANARY_BIN_DIR)/granary/breakpoint.o

# Run the C pre-processor on the *.asm files, then run those through the
# ASM post-processor to do extra token pasting and macro expansion.
$(GRANARY_BIN_DIR)/granary/%.S: $(GRANARY_SRC_DIR)/granary/%.asm
	@echo "Processing assembly file $@"
	@mkdir -p $(@D)
	@$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -E -o $@ -x c -std=c99 $< 
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/post_process_asm.py $@

# Compile C++ files to LLVM bitcode.
$(GRANARY_BIN_DIR)/granary/%.bc: $(GRANARY_SRC_DIR)/granary/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -emit-llvm -flto -c $< -o $@

# Compile an assembly source file into an object file.
$(GRANARY_BIN_DIR)/granary/%.o :: $(GRANARY_BIN_DIR)/granary/%.S
	@echo "Building ASM object $@"
	$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -c $< -o $@

# Compile a single LLVM bitcode file to an object file.
$(GRANARY_BIN_DIR)/granary/%.o :: $(GRANARY_BIN_DIR)/granary/%.bc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) -Qunused-arguments $(GRANARY_CXX_FLAGS) -c $< -o $@

# Link together all compiled .bc files into a single .bc file.
$(GRANARY_MERGED_BC_OBJ) :: $(GRANARY_OBJ_FILES) $(GRANARY_OTHER_OBJ_FILES)
	@echo "Linking intermediate bitcode files into $(GRANARY_MERGED_BC_OBJ)."
	@/usr/bin/llvm-link-3.4 $(GRANARY_OBJ_FILES) $(GRANARY_OTHER_OBJ_FILES) \
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
		$(GRANARY_OTHER_AR_FILES) \
		-o $(GRANARY_MERGED_OBJ)

# Compile the file that (user space) tools link against 
$(GRANARY_TOOL_OBJ) :: $(GRANARY_TOOL_OBJ_DEPS)
	@echo "Linking together tool dependencies into $(GRANARY_TOOL_OBJ)"
	@$(GRANARY_LD) -r $(GRANARY_TOOL_OBJ_DEPS) -o $(GRANARY_TOOL_OBJ)

# Lint all Granary code.
lint:
	@echo "Linting Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_SRC_DIR)/granary -type f -name '*.h' -or -name '*.cc')

all: $(GRANARY_LINT_TARGET) $(GRANARY_MERGED_OBJ) $(GRANARY_TOOL_OBJ_TARGET)
