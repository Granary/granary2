
include ../../Makefile.inc

DR_C_SRC_FILES = $(shell find $(GRANARY_SRC_DIR)/dependencies/dynamorio -type f -name '*.c')
DR_ASM_SRC_FILES = $(shell find $(GRANARY_SRC_DIR)/dependencies/dynamorio -type f -name '*.asm')

DR_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(DR_C_SRC_FILES:.c=.ll))
DR_S_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(DR_ASM_SRC_FILES:.asm=.S))


# Compile the C DynamoRIO dependencies.
$(GRANARY_BIN_DIR)/dependencies/dynamorio/%.ll: $(GRANARY_SRC_DIR)/dependencies/dynamorio/%.c
	@echo "Building C object $@"
	@mkdir -p $(@D)
	$(GRANARY_CC) $(GRANARY_CC_FLAGS) -emit-llvm -c $< -o $@


# Run the C pre-processor on the *.asm files, then run those through the
# ASM post-processor to do extra token pasting and macro expansion.
$(GRANARY_BIN_DIR)/dependencies/dynamorio/%.S: $(GRANARY_SRC_DIR)/dependencies/dynamorio/%.asm
	@echo "Processing assembly file $@"
	@mkdir -p $(@D)
	@$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -E -o $@ -x c -std=c99 $< 
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/post_process_asm.py $@


all: $(DR_OBJ_FILES) $(DR_S_FILES)
