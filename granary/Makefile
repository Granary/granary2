

include ../Makefile.inc

GRANARY_CC_SRC_DIRS = 
GRANARY_CC_SRC_DIRS += $(GRANARY_SRC_DIR)/granary
GRANARY_CC_SRC_DIRS += $(GRANARY_SRC_DIR)/granary/debug
GRANARY_CC_SRC_DIRS += $(GRANARY_SRC_DIR)/granary/base

GRANARY_CC_SRC_FILES = $(shell find $(GRANARY_CC_SRC_DIRS) -maxdepth 1 -type f -name '*.cc')
GRANARY_CC_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(GRANARY_CC_SRC_FILES:.cc=.ll))

# Find driver (i.e. encoder/decoder-specific files).
DRIVER_CC_SRC_FILES = $(shell find $(GRANARY_DRIVER_DIR) -type f -name '*.cc')
DRIVER_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(DRIVER_CC_SRC_FILES:.cc=.ll))


# Find files specific to the environment being instrumented (user/kernel).
WHERE_CC_SRC_FILES = $(shell find $(GRANARY_WHERE_DIR) -type f -name '*.cc')
WHERE_CC_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(WHERE_CC_SRC_FILES:.cc=.ll))


# Find files specific to the architecture being instrumented (e.g. x86-64).
ARCH_ASM_SRC_FILES = $(shell find $(GRANARY_ARCH_DIR) -type f -name '*.asm')
ARCH_ASM_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(ARCH_ASM_SRC_FILES:.asm=.S))


# All object files to be compiled.
GRANARY_OBJ_FILES =
GRANARY_OBJ_FILES += $(GRANARY_CC_OBJ_FILES) $(DRIVER_OBJ_FILES)
GRANARY_OBJ_FILES += $(WHERE_CC_OBJ_FILES) $(ARCH_ASM_OBJ_FILES)


# Run the C pre-processor on the *.asm files, then run those through the
# ASM post-processor to do extra token pasting and macro expansion.
$(GRANARY_BIN_DIR)/granary/arch/$(GRANARY_ARCH)/%.S: $(GRANARY_ARCH_DIR)/%.asm
	@echo "Processing assembly file $@"
	@mkdir -p $(@D)
	@$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -E -o $@ -x c -std=c99 $< 
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/post_process_asm.py $@


# Compile C++ files to LLVM bitcode.
$(GRANARY_BIN_DIR)/granary/%.ll: $(GRANARY_SRC_DIR)/granary/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -emit-llvm -c $< -o $@


lint:
	@echo "Linting Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_SRC_DIR)/granary -type f -name '*.h' -or -name '*.cc')


all: lint $(GRANARY_OBJ_FILES) 
