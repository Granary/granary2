# Copyright 2014 Peter Goodman, all rights reserved.

include $(GRANARY_SRC_DIR)/Makefile.inc

GRANARY_OS_TYPE_FLAGS :=

ifeq (kernel,$(GRANARY_WHERE))
	GRANARY_OS_TYPE_FLAGS += -isystem $(GRANARY_KERNEL_DIR)/include
	GRANARY_OS_TYPE_FLAGS += -isystem $(GRANARY_KERNEL_DIR)/include/uapi
	GRANARY_OS_TYPE_FLAGS += -isystem $(GRANARY_KERNEL_DIR)/include/generated
	GRANARY_OS_TYPE_FLAGS += -isystem $(GRANARY_KERNEL_DIR)/include/generated/uapi
	GRANARY_OS_TYPE_FLAGS += -isystem $(GRANARY_KERNEL_DIR)/arch/x86/include
	GRANARY_OS_TYPE_FLAGS += -isystem $(GRANARY_KERNEL_DIR)/arch/x86/include/generated 
	GRANARY_OS_TYPE_FLAGS += -isystem $(GRANARY_KERNEL_DIR)/arch/x86/include/uapi
endif

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
	@$(GRANARY_CC) $(GRANARY_OS_TYPE_FLAGS) -std=gnu90 -dM -E -w $(GRANARY_WHERE_SRC_DIR)/types.h \
	    > $(GRANARY_OS_TYPES_TMP)/0.h
	
	# Diff the builtin macros, with the builtin + file macros.
	@diff -a --suppress-common-lines --left-column \
	    $(GRANARY_OS_TYPES_TMP)/0.h $(GRANARY_OS_TYPES_TMP)/builtin.h \
	    | sed 's/^[0-9].*//' | sed 's/^< //' > $(GRANARY_OS_TYPES_TMP)/macros.h
	
	# Pre-process the files to get the types.
	@$(GRANARY_CC) $(GRANARY_OS_TYPE_FLAGS)  -std=gnu90 -E -w $(GRANARY_WHERE_SRC_DIR)/types.h \
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
