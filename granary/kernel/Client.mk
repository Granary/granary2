# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all clean

include ../../Makefile.inc

# Include the `Makefile.inc`s of every client.
GRANARY_CLIENT_INCS := $(addsuffix /Makefile.inc,$(GRANARY_CLIENTS))
GRANARY_CLIENT_INCS := $(addprefix $(GRANARY_CLIENT_DIR)/,$(GRANARY_CLIENT_INCS))

include $(GRANARY_CLIENT_INCS)

GRANARY_CLIENT_MKS := $(addsuffix .mk,$(GRANARY_CLIENTS))
GRANARY_CLIENT_MKS := $(addprefix $(GRANARY_BIN_DIR)/clients/,$(GRANARY_CLIENT_MKS))

GRANARY_ASM_S_FILES := $(shell find $(GRANARY_BIN_DIR) -type f -name '*.S')
GRANARY_ASM_O_FILES := $(GRANARY_ASM_S_FILES:.S=.o)
GRANARY_ASM_REL_O_FILES := $(subst $(GRANARY_BIN_DIR)/,,$(GRANARY_ASM_O_FILES))

GRANARY_CXX_FLAGS += -fvisibility=hidden
GRANARY_CC_FLAGS += -fvisibility=hidden
GRANARY_LD_FLAGS_LATE += "-Wl,-Bsymbolic"
GRANARY_LD_FLAGS_LATE += "-Wl,-Bdynamic"
GRANARY_LD_FLAGS_LATE += "-Wl,--export-dynamic"
#GRANARY_LD_FLAGS_LATE += -fvisibility=hidden

# Makefile contents for Granary's kernel module Makefile.
GRANARY_MAKEFILE_HEADER =
GRANARY_MAKEFILE_FOOTER =
GRANARY_MAKEFILE_HEADER += "obj-m += $(GRANARY_NAME).o\n"
GRANARY_MAKEFILE_HEADER += "$(GRANARY_NAME)-y := granary/kernel/entry.o\n"
GRANARY_MAKEFILE_HEADER += "$(GRANARY_NAME)-y += $(GRANARY_ASM_REL_O_FILES)\n"
GRANARY_MAKEFILE_HEADER += "$(GRANARY_NAME)-y += $(GRANARY_NAME)_merged.o_shipped"

GRANARY_MAKEFILE_FOOTER += "ccflags-y += $(GRANARY_LD_FLAGS_LATE) -I$(GRANARY_SRC_DIR)\n"
GRANARY_MAKEFILE_FOOTER += "asflags-y += $(GRANARY_LD_FLAGS_LATE)\n"
GRANARY_MAKEFILE_FOOTER += "ldflags-y += --export-dynamic\n"

# Generate a rule to build the LLVM bitcode files for a specific client.
define GENRULE
$(1)-all : $(addprefix $(GRANARY_CLIENT_DIR)/$(1)/,$($(1)-objs))
	@mkdir -p $(GRANARY_BIN_DIR)/clients
	@echo "Building $(GRANARY_BIN_DIR)/clients/$(1)_merged.o_shipped"
	
	# Link the bitcode files together.
	@llvm-link $(addprefix $(GRANARY_CLIENT_DIR)/$(1)/,$($(1)-objs)) \
		-o $(GRANARY_CLIENT_DIR)/$(1)/$(1).ll
	
	# Convert the linked bitcode files into a single object file. Goal here is
	# to take advantage of optimization on the now fully-linked file.
	@$(GRANARY_CC) -Qunused-arguments $(GRANARY_CC_FLAGS) \
		-c $(GRANARY_CLIENT_DIR)/$(1)/$(1).ll \
		-o $(GRANARY_BIN_DIR)/clients/$(1)_merged.o_shipped
	
	# Create an `.cmd` file for the merged binary.
	@echo "cmd_$(GRANARY_BIN_DIR)/$(1)_merged.o_shipped := " \
		> $(GRANARY_BIN_DIR)/clients/.$(1)_merged.o_shipped.cmd
	
	# Create an includable Makefile for this client.
	@echo "$(GRANARY_NAME)-y += clients/$(1)_merged.o_shipped\n" \
		> $(GRANARY_BIN_DIR)/clients/$(1).mk
endef

# Generate rules to build the bitcode files for each client.
$(foreach client,$(GRANARY_CLIENTS),$(eval $(call GENRULE,$(client))))

# Compile C++ files to LLVM bitcode.
$(GRANARY_CLIENT_DIR)/%.ll: $(GRANARY_CLIENT_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -emit-llvm -flto -c $< -o $@
	
# Clean a specific client.
%-clean:
	@echo "Cleaning client $* in $(GRANARY_CLIENT_DIR)/$*"
	@find $(GRANARY_CLIENT_DIR)/$* -type f -name \*.ll -execdir rm {} \;
	@find $(GRANARY_CLIENT_DIR)/$* -type f -name \*.o -execdir rm {} \;

# Build all clients specified in `GRANARY_CLIENTS`.
all: $(addsuffix -all,$(GRANARY_CLIENTS))
	# Make a unified client makefile.
	@echo $(GRANARY_MAKEFILE_HEADER) > $(GRANARY_BIN_DIR)/Makefile
	@cat $(GRANARY_CLIENT_MKS) >> $(GRANARY_BIN_DIR)/Makefile
	@echo $(GRANARY_MAKEFILE_FOOTER) >> $(GRANARY_BIN_DIR)/Makefile
	
	# Instruct the kernel to compile the specified clients into Granary by
	# invoking the kernel module makefile on the unified makefile.
	@echo "Building $(GRANARY_BIN_DIR)/granary.ko with $(GRANARY_CLIENTS)."
	@make -C $(GRANARY_KERNEL_DIR) M=$(GRANARY_BIN_DIR) modules
	@echo "Done."
	
# Clean all clients specified in `GRANARY_CLIENTS`.	
clean: $(addsuffix -clean,$(GRANARY_CLIENTS))
	