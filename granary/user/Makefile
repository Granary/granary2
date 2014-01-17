
include ../../Makefile.inc

GRANARY_ASM_SRC_FILES = $(shell find $(GRANARY_BIN_DIR) -type f -name '*.S')
GRANARY_ASM_OBJ_FILES = $(GRANARY_ASM_SRC_FILES:.S=.o)

GRANARY_LL_FILES = $(shell find $(GRANARY_BIN_DIR) -type f -name '*.ll')

# Figure out the prefix and suffix of the output file.
GRANARY_NAME_PREFIX =
GRANARY_NAME_SUFFIX = .out
ifeq (0,$(GRANARY_STANDALONE))
	GRANARY_NAME_PREFIX = lib
	GRANARY_NAME_SUFFIX = .so
	GRANARY_LD_FLAGS_EARLY += -shared
endif


# Most of the output file name. Used to link together .ll files, generate a .o
# file, and make either a .out or .so file.
GRANARY_INTERMEDIATE_FILE = $(GRANARY_BIN_DIR)/$(GRANARY_NAME_PREFIX)$(GRANARY_NAME)

# Remove the final linked together file, just in case it is present.
GRANARY_LL_FILES := $(GRANARY_LL_FILES:$(GRANARY_INTERMEDIATE_FILE).ll=)


# Compile an assembly source file into an object file.
$(GRANARY_BIN_DIR)/%.o: $(GRANARY_BIN_DIR)/%.S
	@echo "Building ASM object $@"
	$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -c $< -o $@


# Link together all compiled .ll files into a single .ll file.
$(GRANARY_INTERMEDIATE_FILE).ll: $(GRANARY_LL_FILES)
	@echo "Linking intermediate bitcode files."
	@llvm-link $(GRANARY_LL_FILES) -o $(GRANARY_BIN_DIR)/$(GRANARY_NAME_PREFIX)$(GRANARY_NAME).ll


# Compile the linked together .ll file into an object file. This should enable
# a simple form of link-time optimization.
$(GRANARY_INTERMEDIATE_FILE).o: $(GRANARY_INTERMEDIATE_FILE).ll
	@echo "Building object $@"
	@$(GRANARY_CC) $(GRANARY_LD_FLAGS) -c $(GRANARY_INTERMEDIATE_FILE).ll -o $(GRANARY_INTERMEDIATE_FILE).o


# Link together the final .o file with each of the compiled assembly files
# to form the final .out or .so file.
all: $(GRANARY_ASM_OBJ_FILES) $(GRANARY_INTERMEDIATE_FILE).o
	@echo "Linking $(GRANARY_BIN_DIR)/$(GRANARY_NAME_PREFIX)$(GRANARY_NAME)$(GRANARY_NAME_SUFFIX)"
	@$(GRANARY_CC) \
		$(GRANARY_LD_FLAGS) \
		$(GRANARY_LD_FLAGS_EARLY) \
		$(GRANARY_ASM_OBJ_FILES) $(GRANARY_INTERMEDIATE_FILE).o \
		$(GRANARY_LD_FLAGS_LATE) \
		-o $(GRANARY_INTERMEDIATE_FILE)$(GRANARY_NAME_SUFFIX)
