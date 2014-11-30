# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all lint

include $(GRANARY_SRC_DIR)/Makefile.inc

# Get all subdirectories of granary that contain source files to compile.
GRANARY_CC_SRC_FILES := $(shell find $(GRANARY_WHERE_SRC_DIR) -type f -name '*.cc')
GRANARY_CC_SRC_FILES += $(wildcard $(GRANARY_SRC_DIR)/os/*.cc)
GRANARY_CC_SRC_FILES += $(wildcard $(GRANARY_SRC_DIR)/os/$(GRANARY_OS)/arch/$(GRANARY_ARCH)/*.cc)
GRANARY_CC_OBJ_FILES := $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(GRANARY_CC_SRC_FILES:.cc=.o))

GRANARY_ASM_SRC_FILES = $(shell find $(GRANARY_OS_ARCH_SRC_DIR) -type f -name '*.asm')
GRANARY_ASM_OBJ_FILES := $(subst $(GRANARY_OS_ARCH_SRC_DIR),$(GRANARY_OS_ARCH_BIN_DIR),$(GRANARY_ASM_SRC_FILES:.asm=.o))

GRANARY_INJECTOR = $(GRANARY_BIN_DIR)/grr
GRANARY_INJECTOR_FLAGS = -O3 -g0 -fno-asynchronous-unwind-tables -std=gnu99

GRANARY_LIBRARY = $(GRANARY_BIN_DIR)/lib$(GRANARY_NAME).so

GRANARY_MOD_BIN_DIR = $(GRANARY_BIN_DIR)/module
GRANARY_MODULE = $(GRANARY_BIN_DIR)/$(GRANARY_NAME).ko
GRANARY_SHIPPED_BIN = $(GRANARY_MOD_BIN_DIR)/granary_bin.o_shipped
GRANARY_SHIPPED_CMD = $(GRANARY_MOD_BIN_DIR)/.granary_bin.o.cmd

# All objects that need to be put together to form the library.
GRANARY_O_FILES = os.o granary.o arch.o driver.o
ifneq ($(strip $(GRANARY_CLIENTS)),)
	GRANARY_O_FILES += clients.o
endif

GRANARY_OBJS = $(addprefix $(GRANARY_BIN_DIR)/,$(GRANARY_O_FILES))

$(GRANARY_OS_OBJ): $(GRANARY_CC_OBJ_FILES) $(GRANARY_ASM_OBJ_FILES)
	@echo "Building object $@."
	@$(GRANARY_LD) -g -r $^ -o $@

# Lint all Granary OS code, independent of the OS, and independent of user or
# kernel space.
lint:
	@echo "Linting OS-specific Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_SRC_DIR)/os -type f -name '*.h' -or -name '*.cc')

all: $(GRANARY_LINT_TARGET) $(GRANARY_OS_OBJ) $(GRANARY_OS_TYPES)

# Create an `_shipped` file out of the existing granary objects in the main
# binary dir.
$(GRANARY_SHIPPED_BIN) $(GRANARY_SHIPPED_CMD) : $(GRANARY_OBJS)
	@echo "Building shipped object $@."
	@mkdir -p $(GRANARY_MOD_BIN_DIR)
	@$(GRANARY_LD) -z noexecstack -g -r $^ -o $@
	# Make a `.cmd` file for the shipped object file.
	@echo "cmd_$@ := " > $(GRANARY_SHIPPED_CMD)

# Create the `granary.ko` file. The steps are as follows:
#	1) Merge all main granary `.o` files together into a "shipped" `.o` file,
#	   thus creating `bin/module/granary_bin.o_shipped`. We also create an
#	   associated `.cmd` file for the shipped binary.
#	2) Copy the `os/linux/kernel/module` subdirectory into the `bin/<triple>`
#	   directory.
#	3) Invoke kbuild on `bin/<triple>/module/Makefile`.
$(GRANARY_MODULE): $(GRANARY_SHIPPED_BIN)
	@echo "Building kernel object $@."
	@cp -arp $(GRANARY_WHERE_SRC_DIR)/module $(GRANARY_BIN_DIR)
	$(MAKE) -C $(GRANARY_KERNEL_DIR) \
		M=$(GRANARY_MOD_BIN_DIR) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) \
		modules

# Make the final granary executables.
exec: $(GRANARY_MODULE)
	
	