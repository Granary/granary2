# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../../Makefile.inc  # os/Makefile.inc

# Get all subdirectories of granary that contain source files to compile.
GRANARY_CC_SRC_FILES := $(shell find $(GRANARY_WHERE_SRC_DIR) -type f -name '*.cc')
GRANARY_CC_OBJ_FILES := $(subst $(GRANARY_WHERE_SRC_DIR),$(GRANARY_WHERE_BIN_DIR),$(GRANARY_CC_SRC_FILES:.cc=.o))

GRANARY_ASM_SRC_FILES = $(shell find $(GRANARY_OS_ARCH_SRC_DIR) -type f -name '*.asm')
GRANARY_ASM_OBJ_FILES := $(subst $(GRANARY_OS_ARCH_SRC_DIR),$(GRANARY_OS_ARCH_BIN_DIR),$(GRANARY_ASM_SRC_FILES:.asm=.o))

GRANARY_INJECTOR = $(GRANARY_BIN_DIR)/grr
GRANARY_INJECTOR_FLAGS = -O3 -g0 -fno-asynchronous-unwind-tables -std=gnu99

GRANARY_LIBRARY = $(GRANARY_BIN_DIR)/lib$(GRANARY_NAME).so

$(GRANARY_OS_OBJ): $(GRANARY_CC_OBJ_FILES) $(GRANARY_ASM_OBJ_FILES)
	@echo "Building object $(GRANARY_OS_OBJ)."
	$(GRANARY_LD) -r $^ -o $(GRANARY_OS_OBJ)

all: $(GRANARY_OS_OBJ)

# Makes `grr`, the user-space injector if `libgranary.so`.
$(GRANARY_INJECTOR):
	@echo "Compiling injector $(GRANARY_INJECTOR)"
	@$(GRANARY_CC) $(GRANARY_INJECTOR_FLAGS) \
        -c $(GRANARY_WHERE_SRC_DIR)/inject.c \
		-o $(GRANARY_WHERE_BIN_DIR)/inject.o
	@$(GRANARY_CC) $(GRANARY_INJECTOR_FLAGS) \
        $(GRANARY_WHERE_BIN_DIR)/inject.o \
        -o $(GRANARY_INJECTOR)

# Makes `libgranary.so`, the user-space shared library version of Granary.
$(GRANARY_LIBRARY):
	@echo "Compiling library $(GRANARY_LIBRARY)"
	@$(GRANARY_CC) $(GRANARY_COMMON_ARCH_FLAGS) \
		$(GRANARY_LD_FLAGS) $(GRANARY_LD_FLAGS_EARLY) -shared \
		$(GRANARY_BIN_DIR)/*.o \
		$(GRANARY_LD_FLAGS_LATE) "-Wl,-Bsymbolic" \
		-o $(GRANARY_LIBRARY)

# Make the final granary executables.
exec: $(GRANARY_INJECTOR) $(GRANARY_LIBRARY)
	