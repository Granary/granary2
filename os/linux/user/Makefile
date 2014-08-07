# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all lint

include ../../../Makefile.inc

# Get all subdirectories of granary that contain source files to compile.
GRANARY_CC_SRC_FILES := $(shell find $(GRANARY_WHERE_SRC_DIR) -type f -name '*.cc')
GRANARY_CC_SRC_FILES += $(GRANARY_SRC_DIR)/os/*.cc
GRANARY_CC_OBJ_FILES := $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(GRANARY_CC_SRC_FILES:.cc=.o))

GRANARY_ASM_SRC_FILES = $(shell find $(GRANARY_OS_ARCH_SRC_DIR) -type f -name '*.asm')
GRANARY_ASM_OBJ_FILES := $(subst $(GRANARY_OS_ARCH_SRC_DIR),$(GRANARY_OS_ARCH_BIN_DIR),$(GRANARY_ASM_SRC_FILES:.asm=.o))

GRANARY_INJECTOR = $(GRANARY_BIN_DIR)/grr
GRANARY_INJECTOR_FLAGS = -O3 -g0 -fno-asynchronous-unwind-tables -std=gnu99

GRANARY_LIBRARY_OBJ = $(GRANARY_BIN_DIR)/lib$(GRANARY_NAME).o
GRANARY_LIBRARY = $(GRANARY_BIN_DIR)/lib$(GRANARY_NAME).so

$(GRANARY_OS_OBJ): $(GRANARY_CC_OBJ_FILES) $(GRANARY_ASM_OBJ_FILES)
	@echo "Building object $(GRANARY_OS_OBJ)."
	$(GRANARY_LD) -r $^ -o $(GRANARY_OS_OBJ)

# Lint all Granary OS code, independent of the OS, and independent of user or
# kernel space.
lint:
	@echo "Linting OS-specific Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_SRC_DIR)/os -type f -name '*.h' -or -name '*.cc')

all: $(GRANARY_LINT_TARGET) $(GRANARY_OS_OBJ)

# Makes `grr`, the user-space injector if `libgranary.so`.
$(GRANARY_INJECTOR):
	@echo "Compiling injector $(GRANARY_INJECTOR)"
	@$(GRANARY_CC) $(GRANARY_INJECTOR_FLAGS) \
        -c $(GRANARY_WHERE_SRC_DIR)/inject.c \
		-o $(GRANARY_WHERE_BIN_DIR)/inject.o
	@$(GRANARY_CC) $(GRANARY_INJECTOR_FLAGS) \
        $(GRANARY_WHERE_BIN_DIR)/inject.o \
        -o $(GRANARY_INJECTOR)

# Makes a `libgranary.o` file that is the combination of all the other `.o`
# files. We have this extra step so that we can add in the linker script at a
# stage where it will have the desired effect, while also not causing any later
# load-time issues due to R_386_RELATIVE (0x08) PLT relocation types.
$(GRANARY_LIBRARY_OBJ):
	@echo "Building object $(GRANARY_LIBRARY_OBJ)."
	$(GRANARY_LD) -T $(GRANARY_SRC_DIR)/inst_exports.lds -g \
		-r $(filter-out $(GRANARY_LIBRARY_OBJ),$(wildcard $(GRANARY_BIN_DIR)/*.o)) \
		-o $(GRANARY_LIBRARY_OBJ)

# Makes `libgranary.so`, the user-space shared library version of Granary.
$(GRANARY_LIBRARY): $(GRANARY_LIBRARY_OBJ)
	@echo "Compiling library $(GRANARY_LIBRARY)"
	@$(GRANARY_CC) $(GRANARY_COMMON_ARCH_FLAGS) \
		$(GRANARY_LD_FLAGS) \
		-nodefaultlibs $(GRANARY_LD_FLAGS_EARLY) -shared \
		$(GRANARY_LIBRARY_OBJ) \
		$(GRANARY_LD_FLAGS_LATE) "-Wl,-Bsymbolic" \
		-o $(GRANARY_LIBRARY)

# Make the final granary executables.
exec: $(GRANARY_INJECTOR) $(GRANARY_LIBRARY)
	