# Copyright 2014 Peter Goodman, all rights reserved.

include Makefile.inc

.PHONY: all all_objects clean clean_generated install example

# Compile all files. This passes in `GRANARY_SRC_DIR` through to all sub-
# invocations of `make`.
all_objects:
	@echo "Entering $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER)"
	$(MAKE) -C $(GRANARY_SRC_DIR)/dependencies/$(GRANARY_DRIVER) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all
	@echo "Entering $(GRANARY_SRC_DIR)/granary"
	$(MAKE) -C $(GRANARY_SRC_DIR)/granary \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all
	@echo "Entering $(GRANARY_WHERE_DIR)"
	$(MAKE) -C $(GRANARY_WHERE_DIR) \
		$(MFLAGS) GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) all

all: all_objects
	# Make a final object that tools can link against for getting arch-specific
	# implementations of built-in compiler functions that are also sometimes
	# synthesized by optimizing compilers (e.g. memset).
	@echo "Building object $@"
	@$(GRANARY_CXX) -c $(GRANARY_BIN_DIR)/granary/breakpoint.ll \
    	-o $(GRANARY_BIN_DIR)/granary/breakpoint.o
	@$(GRANARY_LD) -r \
    	$(GRANARY_BIN_DIR)/granary/arch/$(GRANARY_ARCH)/asm/string.o \
    	$(GRANARY_BIN_DIR)/granary/breakpoint.o \
    	-o $(GRANARY_BIN_DIR)/tool.o
	
	@echo "Done."

# Clean up all executable / binary files.
clean:
	@echo "Removing all previously compiled files."
	@find $(GRANARY_BIN_DIR) -type f -name \*.so -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.ll -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.o -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.o.cmd -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.S -execdir rm {} \;
	@find $(GRANARY_BIN_DIR) -type f -name \*.out -execdir rm {} \;

# Clean up all auto-generated files.
clean_generated:
	@echo "Removing all previously auto-generated files."
	@find $(GRANARY_GEN_SRC_DIR) -type f -execdir rm {} \;

# Make a header file that external tools can use to define tools.
headers:
	@mkdir -p $(GRANARY_EXPORT_HEADERS_DIR)
	@$(GRANARY_PYTHON) $(GRANARY_SRC_DIR)/scripts/generate_export_headers.py \
		$(GRANARY_WHERE) $(GRANARY_SRC_DIR) $(GRANARY_EXPORT_HEADERS_DIR)

# Install libgranary.so onto the OS.
install: all headers
	cp $(GRANARY_BIN_DIR)/libgranary.so $(GRANARY_EXPORT_LIB_DIR)

# Compile one or more specific tools. For example:
# `make tools GRANARY_TOOLS=bbcount`.
tools:
	$(MAKE) -C $(GRANARY_SRC_DIR) -f Tool.mk \
		$(MFLAGS) \
		GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) \
		GRANARY_TOOL_DIR=$(GRANARY_TOOL_DIR) all

# Clean one or more specific tools. For example:
# `make clean_tools GRANARY_TOOLS=bbcount`.
clean_tools:
	$(MAKE) -C $(GRANARY_SRC_DIR) -f Tool.mk \
		$(MFLAGS) \
		GRANARY_SRC_DIR=$(GRANARY_SRC_DIR) \
		GRANARY_TOOL_DIR=$(GRANARY_TOOL_DIR) clean
