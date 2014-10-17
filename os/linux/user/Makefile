# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all lint

include ../../../Makefile.inc

# Get all subdirectories of granary that contain source files to compile.
GRANARY_CC_SRC_FILES := $(shell find $(GRANARY_WHERE_SRC_DIR) -type f -name '*.cc')
GRANARY_CC_SRC_FILES += $(wildcard $(GRANARY_SRC_DIR)/os/*.cc)
GRANARY_CC_SRC_FILES += $(wildcard $(GRANARY_SRC_DIR)/os/$(GRANARY_OS)/arch/$(GRANARY_ARCH)/*.cc)
GRANARY_CC_OBJ_FILES := $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(GRANARY_CC_SRC_FILES:.cc=.o))

GRANARY_ASM_SRC_FILES = $(shell find $(GRANARY_OS_ARCH_SRC_DIR) -type f -name '*.asm')
GRANARY_ASM_OBJ_FILES := $(subst $(GRANARY_OS_ARCH_SRC_DIR),$(GRANARY_OS_ARCH_BIN_DIR),$(GRANARY_ASM_SRC_FILES:.asm=.o))

GRANARY_INJECTOR = $(GRANARY_BIN_DIR)/grr
GRANARY_INJECTOR_FLAGS = -O3 -g0 -fno-asynchronous-unwind-tables -std=gnu99

GRANARY_LIBRARY_OBJ = $(GRANARY_BIN_DIR)/lib$(GRANARY_NAME).o
GRANARY_LIBRARY = $(GRANARY_BIN_DIR)/lib$(GRANARY_NAME).so

# All objects that need to be put together to form the library.
GRANARY_O_FILES = os.o granary.o arch.o driver.o
ifneq (test,$(GRANARY_TARGET))
	GRANARY_O_FILES += $(addsuffix _client.o,$(GRANARY_CLIENTS))
endif
GRANARY_OBJS = $(addprefix $(GRANARY_BIN_DIR)/,$(GRANARY_O_FILES))

$(GRANARY_OS_OBJ): $(GRANARY_CC_OBJ_FILES) $(GRANARY_ASM_OBJ_FILES)
	@echo "Building object $(GRANARY_OS_OBJ)."
	@$(GRANARY_LD) -g -r $^ -o $(GRANARY_OS_OBJ)

# Lint all Granary OS code, independent of the OS, and independent of user or
# kernel space.
lint:
	@echo "Linting OS-specific Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_SRC_DIR)/os -type f -name '*.h' -or -name '*.cc')

# Create a combined types file for user space. This includes many useful C
# standard (and some non-standard) header files, and gets all there macros and
# typedefs in one spot.
$(GRANARY_OS_TYPES):
	@echo "Generating OS-specific header files."
	@mkdir -p $(@D)
	@$(eval GRANARY_OS_TYPES_TMP := $(shell mktemp -d))
	
	# Get the built-in macro defs of the compiler.
	@$(GRANARY_CC) -std=gnu90 -dM -E - < /dev/null \
	    > $(GRANARY_OS_TYPES_TMP)/builtin.h

	# Get the marco defs when compiling the specific files.
	@$(GRANARY_CC) -std=gnu90 -dM -E -w $(GRANARY_WHERE_SRC_DIR)/types.h \
	    > $(GRANARY_OS_TYPES_TMP)/0.h

	# Diff the builtin macros, with the builtin + file macros.
	@diff -a --suppress-common-lines --left-column \
	    $(GRANARY_OS_TYPES_TMP)/0.h $(GRANARY_OS_TYPES_TMP)/builtin.h \
	    | sed 's/^[0-9].*//' | sed 's/^< //' > $(GRANARY_OS_TYPES_TMP)/macros.h

	# Pre-process the files to get the types.
	@$(GRANARY_CC) -std=gnu90 -E -w $(GRANARY_WHERE_SRC_DIR)/types.h \
	    > $(GRANARY_OS_TYPES_TMP)/1.h

	# Clean up the pre-processed file.
	@$(GRANARY_PYTHON) $(GRANARY_CPARSER_DIR)/post_process_header.py \
	    $(GRANARY_OS_TYPES_TMP)/1.h \
	    > $(GRANARY_OS_TYPES_TMP)/2.h

	# Re-order the header so that (ideally) all relations between types are
	# satisfied.
	@$(GRANARY_PYTHON) $(GRANARY_CPARSER_DIR)/reorder_header.py \
	    $(GRANARY_OS_TYPES_TMP)/2.h \
	    > $(GRANARY_OS_TYPES_TMP)/3.h

	# Combine the macros and the types together into a single file.
	@cat $(GRANARY_OS_TYPES_TMP)/3.h $(GRANARY_OS_TYPES_TMP)/macros.h \
	    > $(GRANARY_OS_TYPES)
	
	# Get rid of our temp file.
	@rm -rf $(GRANARY_OS_TYPES_TMP)
	
all: $(GRANARY_LINT_TARGET) $(GRANARY_OS_TYPES) $(GRANARY_OS_OBJ)

# Makes `grr`, the user-space injector if `libgranary.so`.
$(GRANARY_INJECTOR): $(GRANARY_WHERE_SRC_DIR)/inject.c
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
$(GRANARY_LIBRARY_OBJ): $(GRANARY_OBJS)
	@echo "Building object $(GRANARY_LIBRARY_OBJ)."
	@$(GRANARY_LD) -g \
	    -z initfirst -z interpose -z nodelete -z noexecstack -z now \
	    -T $(GRANARY_SRC_DIR)/linker.lds \
	    -r $(GRANARY_OBJS) \
	    -o $(GRANARY_LIBRARY_OBJ)

# Makes `libgranary.so`, the user-space shared library version of Granary.
$(GRANARY_LIBRARY): $(GRANARY_LIBRARY_OBJ)
	@echo "Compiling library $(GRANARY_LIBRARY)"
	@$(GRANARY_CC) $(GRANARY_DEBUG_FLAGS) $(GRANARY_COMMON_ARCH_FLAGS) \
	    $(GRANARY_LD_FLAGS) \
	    -nodefaultlibs -nostartfiles $(GRANARY_LD_FLAGS_EARLY) -shared \
	    $(GRANARY_LIBRARY_OBJ) \
	    $(GRANARY_LD_FLAGS_LATE) \
	    -o $(GRANARY_LIBRARY)

# Make the final granary executables.
exec: $(GRANARY_INJECTOR) $(GRANARY_LIBRARY)
	