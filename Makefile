# Copyright 2014 Peter Goodman, all rights reserved.

include Makefile.inc

.PHONY: all headers clean clean_generated test
.PHONY: tools clean_tools
.PHONY: where_common where_user where_kernel
.PHONY: target_debug target_release target_test

# Compile all files. This passes in `GRANARY_SRC_DIR` through to all sub-
# invocations of `make`.
where_common:
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER)"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies/xxhash"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies/xxhash \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all
	@echo "Entering $(GRANARY_SRC_DIR)/granary"
	$(MAKE) -C $(GRANARY_SRC_DIR)/granary \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all
	@echo "Entering $(GRANARY_WHERE_SRC_DIR)"
	$(MAKE) -C $(GRANARY_WHERE_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

# Make a final object that tools can link against for getting arch-specific
# implementations of built-in compiler functions that are also sometimes
# synthesized by optimizing compilers (e.g. memset).
where_user: where_common
	@echo "Building object $(GRANARY_BIN_DIR)/granary/breakpoint.o"
	@$(GRANARY_CXX) -c $(GRANARY_BIN_DIR)/granary/breakpoint.ll \
    	-o $(GRANARY_BIN_DIR)/granary/breakpoint.o
    	
	@echo "Loading user space $(GRANARY_BIN_DIR)/tool.o"
	@$(GRANARY_LD) -r \
    	$(GRANARY_BIN_DIR)/granary/arch/$(GRANARY_ARCH)/asm/string.o \
    	$(GRANARY_BIN_DIR)/granary/breakpoint.o \
    	-o $(GRANARY_BIN_DIR)/tool.o
	
# We handle the equivalent of `user_tool` in `granary/kernel/Took.mk`.
where_kernel: where_common

# Target-specific.
target_debug: where_$(GRANARY_WHERE)
target_release: where_$(GRANARY_WHERE)
target_test: target_debug
	@mkdir -p $(GRANARY_TEST_BIN_DIR)
	@mkdir -p $(GRANARY_GTEST_BIN_DIR)
	@echo "Entering $(GRANARY_TEST_SRC_DIR)"
	$(MAKE) -C $(GRANARY_TEST_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

all: target_$(GRANARY_TARGET)
	@echo "Done."

# Clean up all executable / binary files. This just throws away the bin dir
# that is specific to the target/where variables.
clean:
	@echo "Removing all previously compiled files."
	@-rm -rf $(GRANARY_BIN_DIR) $(GRANARY_DEV_NULL)

# Clean up all auto-generated files.
clean_generated:
	@echo "Removing all previously auto-generated files."
	@find $(GRANARY_GEN_SRC_DIR) -type f -execdir rm {} \;

# Make a header file that external tools can use to define tools.
headers:
	@mkdir -p $(GRANARY_EXPORT_HEADERS_DIR)
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/generate_export_headers.py \
		$(GRANARY_WHERE) $(GRANARY_SRC_DIR) $(GRANARY_EXPORT_HEADERS_DIR)

# Compile one or more specific tools. For example:
# `make tools GRANARY_TOOLS=bbcount`.
tools:
	$(MAKE) -C $(GRANARY_SRC_DIR)/granary/$(GRANARY_WHERE) -f Tool.mk \
		$(MFLAGS) \
		GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) \
		GRANARY_TOOL_DIR=$(GRANARY_TOOL_DIR) all

# Clean one or more specific tools. For example:
# `make clean_tools GRANARY_TOOLS=bbcount`.
clean_tools:
	$(MAKE) -C $(GRANARY_SRC_DIR)/granary/$(GRANARY_WHERE) -f Tool.mk \
		$(MFLAGS) \
		GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) \
		GRANARY_TOOL_DIR=$(GRANARY_TOOL_DIR) clean

# Run all test cases.
test: target_test
	@echo "Entering $(GRANARY_TEST_SRC_DIR)"
	$(MAKE) -C $(GRANARY_TEST_SRC_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) test
