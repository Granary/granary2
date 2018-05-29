# Copyright 2014 Peter Goodman, all rights reserved.

include $(GRANARY_SRC_DIR)/Makefile.inc

.PHONY: types

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

$(GRANARY_OS_TYPES): Makefile
	@echo "Generating OS-specific header files."
	@mkdir -p $(@D)
	
	# Pre-process the files to get the types.
	@$(GRANARY_CC) $(GRANARY_OS_TYPE_FLAGS)  -std=gnu11 -E -w $(GRANARY_WHERE_SRC_DIR)/types.h \
		> /tmp/1.h
		
	@$(GRANARY_CC) $(GRANARY_OS_TYPE_FLAGS)  -std=gnu11 -dM -E -w $(GRANARY_WHERE_SRC_DIR)/types.h \
		> /tmp/macros.h
	
	# Clean up the pre-processed file.
	@$(GRANARY_PYTHON) $(GRANARY_CPARSER_DIR)/post_process_header.py /tmp/1.h \
		> /tmp/2.h
	
	# Re-order the header so that (ideally) all relations between types are
	# satisfied.
	@$(GRANARY_PYTHON) $(GRANARY_CPARSER_DIR)/reorder_header.py \
		/tmp/2.h \
		> /tmp/3.h
	
	@cat $(GRANARY_SRC_DIR)/os/types/header.inc \
		> $(GRANARY_OS_TYPES)
	
	# Combine the macros and the types together into a single file.
	@cat /tmp/3.h /tmp/macros.h \
		>> $(GRANARY_OS_TYPES)
	
	@cat $(GRANARY_SRC_DIR)/os/types/footer.inc \
		>> $(GRANARY_OS_TYPES)

# Create a combined types file for user space. This includes many useful C
# standard (and some non-standard) header files, and gets all there macros and
# typedefs in one spot.
types: $(GRANARY_OS_TYPES)
