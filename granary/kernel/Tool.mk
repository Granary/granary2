# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all clean

include ../../Makefile.inc

# Include the `Makefile.inc`s of every tool.
GRANARY_TOOL_INCS := $(addsuffix /Makefile.inc,$(GRANARY_TOOLS))
GRANARY_TOOL_INCS := $(addprefix $(GRANARY_TOOL_DIR)/,$(GRANARY_TOOL_INCS))

include $(GRANARY_TOOL_INCS)

GRANARY_TOOL_MKS := $(addsuffix .mk,$(GRANARY_TOOLS))
GRANARY_TOOL_MKS := $(addprefix $(GRANARY_BIN_DIR)/tools/,$(GRANARY_TOOL_MKS))

GRANARY_CXX_FLAGS += -fvisibility=hidden
GRANARY_CC_FLAGS += -fvisibility=hidden

GRANARY_LD_FLAGS_LATE += "-Wl,-Bsymbolic"
GRANARY_LD_FLAGS_LATE += "-Wl,-Bdynamic"
GRANARY_LD_FLAGS_LATE += "-Wl,--export-dynamic"
GRANARY_LD_FLAGS_LATE += "-Wl,-flat_namespace"

# Generate a rule to build the LLVM bitcode files for a specific tool.
define GENRULE
$(1)-all : $(addprefix $(GRANARY_TOOL_DIR)/$(1)/,$($(1)-objs))
	@mkdir -p $(GRANARY_BIN_DIR)/tools
	@echo "Building $(GRANARY_BIN_DIR)/tools/$(1)_merged.o_shipped"
	
	# Link the bitcode files together.
	@llvm-link $(addprefix $(GRANARY_TOOL_DIR)/$(1)/,$($(1)-objs)) \
		-o $(GRANARY_TOOL_DIR)/$(1)/$(1).ll
	
	# Convert the linked bitcode files into a single object file. Goal here is
	# to take advantage of optimization on the now fully-linked file.
	@$(GRANARY_CC) -Qunused-arguments $(GRANARY_CC_FLAGS) \
		-c $(GRANARY_TOOL_DIR)/$(1)/$(1).ll \
		$(GRANARY_LD_FLAGS_LATE) \
		-o $(GRANARY_TOOL_DIR)/$(1)/$(1).o
	
	# Link together Granary's `tool.o` and the grouped `llvm-link` compiled file
	# into a shipped .o file.
	@$(GRANARY_LD) -r \
		$(GRANARY_TOOL_DIR)/$(1)/$(1).o $(GRANARY_BIN_DIR)/tool.o \
		-o $(GRANARY_BIN_DIR)/tools/$(1)_merged.o_shipped
	
	# Create an `.cmd` file for the merged binary.
	@echo "cmd_$(GRANARY_BIN_DIR)/$(1)_merged.o_shipped := " \
		> $(GRANARY_BIN_DIR)/tools/.$(1)_merged.o_shipped.cmd
	
	# Create an includable Makefile for this tool.
	@echo "obj-m += $(1).o\n" \
	      "$(1)-y := $(1)_merged.o_shipped tool_entry.o\n" \
		> $(GRANARY_BIN_DIR)/tools/$(1).mk
endef

# Generate rules to build the bitcode files for each tool.
$(foreach tool,$(GRANARY_TOOLS),$(eval $(call GENRULE,$(tool))))

# Compile C++ files to LLVM bitcode.
$(GRANARY_TOOL_DIR)/%.ll: $(GRANARY_TOOL_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -emit-llvm -c $< -o $@
	
# Clean a specific tool.
%-clean:
	@echo "Cleaning tool $* in $(GRANARY_TOOL_DIR)/$*"
	@find $(GRANARY_TOOL_DIR)/$* -type f -name \*.ll -execdir rm {} \;
	@find $(GRANARY_TOOL_DIR)/$* -type f -name \*.o -execdir rm {} \;

# Build all tools specified in `GRANARY_TOOLS`.
all: $(addsuffix -all,$(GRANARY_TOOLS))
	# Copy in the tool entrypoint code.
	@cp $(GRANARY_SRC_DIR)/granary/kernel/tool_entry.c $(GRANARY_BIN_DIR)/tools
	
	# Make a unified tool makefile.
	@echo "include $(GRANARY_TOOL_MKS)\n" \
		  "ccflags-y += $(GRANARY_LD_FLAGS_LATE) -I$(GRANARY_SRC_DIR)\n" \
		  "asflags-y += $(GRANARY_LD_FLAGS_LATE)\n" \
		  "ldflags-y += --export-dynamic -Bdynamic\n" \
		> $(GRANARY_BIN_DIR)/tools/Makefile
	
	# Instruct the kernel to compile the tools and to reference the symbols
	# of `granary.ko`.
	make -C $(GRANARY_KERNEL_DIR) \
		M=$(GRANARY_BIN_DIR)/tools \
		KBUILD_EXTRA_SYMBOLS=$(GRANARY_BIN_DIR)/Module.symvers \
		modules
	
# Clean all tools specified in `GRANARY_TOOLS`.	
clean: $(addsuffix -clean,$(GRANARY_TOOLS))
	