
.PHONY: all

include ../../Makefile.inc

DR_SRC_DIR = $(GRANARY_SRC_DIR)/dependencies/dynamorio
DR_BIN_DIR = $(GRANARY_BIN_DIR)/dependencies/dynamorio

DR_C_SRC_FILES = $(shell find $(DR_SRC_DIR) -type f -name '*.c')
DR_ASM_SRC_FILES = $(shell find $(DR_SRC_DIR) -type f -name '*.asm')

DR_OBJ_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(DR_C_SRC_FILES:.c=.ll))
DR_S_FILES = $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(DR_ASM_SRC_FILES:.asm=.S))

# Create the auto-generated file that makes it easier to create DynamoRIO-based
# instructions.
$(GRANARY_GEN_SRC_DIR)/dynamorio/builder.cc:
	@mkdir -p $(@D)
	@echo "Generating instruction building interface $@"
	@clang -I$(GRANARY_SRC_DIR) -E -dM \
		$(GRANARY_SRC_DIR)/dependencies/dynamorio/x86/instr_create.h | \
		grep INSTR_CREATE > /tmp/dr_instr_create_headers.h
	@$(GRANARY_PYTHON) $(DR_SRC_DIR)/scripts/generate_instruction_builder.py \
		/tmp/dr_instr_create_headers.h $(GRANARY_GEN_SRC_DIR)/dynamorio

# Compile the auto-generated low-level IR file for creating DynamoRIO
# instructions.
$(GRANARY_GEN_BIN_DIR)/dynamorio/builder.ll: $(GRANARY_GEN_SRC_DIR)/dynamorio/builder.cc
	@mkdir -p $(@D)
	$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -emit-llvm -c $< -o $@

# Compile the C DynamoRIO dependencies.
$(DR_BIN_DIR)/%.ll: $(DR_SRC_DIR)/%.c
	@echo "Building C object $@"
	@mkdir -p $(@D)
	$(GRANARY_CC) $(GRANARY_CC_FLAGS) -emit-llvm -c $< -o $@

# Run the C pre-processor on the *.asm files, then run those through the
# ASM post-processor to do extra token pasting and macro expansion.
$(DR_BIN_DIR)/%.S: $(DR_SRC_DIR)/%.asm
	@echo "Processing assembly file $@"
	@mkdir -p $(@D)
	@$(GRANARY_CC) $(GRANARY_ASM_FLAGS) -E -o $@ -x c -std=c99 $< 
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/post_process_asm.py $@

# Compile the DynamoRIO dependencies into .S (x86) and .ll files.
all: $(DR_OBJ_FILES) $(DR_S_FILES) $(GRANARY_GEN_BIN_DIR)/dynamorio/builder.ll
