# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all injector

include ../../Makefile.inc

GRANARY_ASM_SRC_FILES = $(shell find $(GRANARY_BIN_DIR) -type f -name '*.S')
GRANARY_ASM_OBJ_FILES = $(GRANARY_ASM_SRC_FILES:.S=.o)

GRANARY_LL_FILES = $(shell find $(GRANARY_BIN_DIR) -type f -name '*.ll')
GRANARY_INJECTOR_LL = $(GRANARY_BIN_DIR)/granary/user/inject.ll

# Figure out the prefix and suffix of the output file.
GRANARY_NAME_PREFIX =
GRANARY_NAME_SUFFIX = .out
GRANARY_INJECTOR =
GRANARY_LD_FLAGS_LATE += -ldl
ifeq (0,$(GRANARY_STANDALONE))
	GRANARY_NAME_PREFIX = lib
	GRANARY_NAME_SUFFIX = .so
	GRANARY_LD_FLAGS_EARLY += -shared
	GRANARY_LD_FLAGS_LATE += "-Wl,-Bsymbolic"
	GRANARY_INJECTOR = injector
else
	GRANARY_LD_FLAGS_LATE += "-Wl,--export-dynamic"
	GRANARY_LD_FLAGS_LATE += "-Wl,--defsym=__cxa_pure_virtual=granary_break_on_unreachable_code"
endif

GRANARY_INJECTOR_FLAGS = -O3 -g0 -fno-asynchronous-unwind-tables -std=gnu99

# Most of the output file name. Used to link together .ll files, generate a .o
# file, and make either a .out or .so file.
GRANARY_INTERMEDIATE_FILE = $(GRANARY_BIN_DIR)/$(GRANARY_NAME_PREFIX)$(GRANARY_NAME)

# Remove the final linked together file, just in case it is present.
GRANARY_LL_FILES := $(GRANARY_LL_FILES:$(GRANARY_INTERMEDIATE_FILE).ll=)

# Compile an assembly source file into an object file.
$(GRANARY_BIN_DIR)/%.o:: $(GRANARY_BIN_DIR)/%.S
	@echo "Building ASM object $@"
	$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -c $< -o $@

# Link together all compiled .ll files into a single .ll file.
$(GRANARY_INTERMEDIATE_FILE).ll: $(GRANARY_LL_FILES)
	@echo "Linking intermediate bitcode files."
	@llvm-link $(GRANARY_LL_FILES) -o $(GRANARY_INTERMEDIATE_FILE).ll

# Compile the linked together .ll file into an object file. This should enable
# a simple form of link-time optimization.
$(GRANARY_INTERMEDIATE_FILE).o:: $(GRANARY_INTERMEDIATE_FILE).ll
	@echo "Building object $@"
	@$(GRANARY_CC) $(GRANARY_LD_FLAGS) \
		-c $(GRANARY_INTERMEDIATE_FILE).ll \
		-o $(GRANARY_INTERMEDIATE_FILE).o

# Compile the injector for launching an executable with libgranary.so.
injector:
	@echo "Compiling injector $(GRANARY_BIN_DIR)/grr"
	@$(GRANARY_CC) $(GRANARY_INJECTOR_FLAGS) \
        -c $(GRANARY_SRC_DIR)/granary/user/inject.c \
		-o $(GRANARY_BIN_DIR)/granary/user/inject.o
	@$(GRANARY_CC) $(GRANARY_INJECTOR_FLAGS) \
        $(GRANARY_BIN_DIR)/granary/user/inject.o \
        -o $(GRANARY_BIN_DIR)/grr

# Link together the final .o file with each of the compiled assembly files
# to form the final .out or .so file.
all: $(GRANARY_ASM_OBJ_FILES) $(GRANARY_INTERMEDIATE_FILE).o $(GRANARY_INJECTOR)
	@echo "Linking $(GRANARY_INTERMEDIATE_FILE)$(GRANARY_NAME_SUFFIX)"
	@$(GRANARY_CC) $(GRANARY_COMMON_ARCH_FLAGS) \
		$(GRANARY_LD_FLAGS) \
		$(GRANARY_LD_FLAGS_EARLY) \
		$(GRANARY_ASM_OBJ_FILES) $(GRANARY_INTERMEDIATE_FILE).o \
		$(GRANARY_LD_FLAGS_LATE) \
		-o $(GRANARY_INTERMEDIATE_FILE)$(GRANARY_NAME_SUFFIX)
