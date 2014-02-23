# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

GRANARY_INJECTOR = $(GRANARY_BIN_DIR)/grr
GRANARY_INJECTOR_TARGET =
GRANARY_INJECTOR_FLAGS = -O3 -g0 -fno-asynchronous-unwind-tables -std=gnu99

GRANARY_LIBRARY = $(GRANARY_BIN_DIR)/lib$(GRANARY_NAME).so
GRANARY_LIBRARY_TARGET =

GRANARY_EXE = $(GRANARY_BIN_DIR)/$(GRANARY_NAME).out
GRANARY_EXE_TARGET =

# Extra flags to bring in the dynamic linker (libdl.so on Linux)
GRANARY_LD_FLAGS_LATE += -ldl

ifneq (test,$(GRANARY_TARGET))
ifeq (0,$(GRANARY_STANDALONE))
	GRANARY_INJECTOR_TARGET = $(GRANARY_INJECTOR)
	GRANARY_LIBRARY_TARGET = $(GRANARY_LIBRARY)
else
	GRANARY_EXE_TARGET = $(GRANARY_EXE)
endif
endif

# Compile the injector for launching an executable with libgranary.so.
$(GRANARY_INJECTOR):
	@echo "Compiling injector $(GRANARY_INJECTOR)"
	@$(GRANARY_CC) $(GRANARY_INJECTOR_FLAGS) \
        -c $(GRANARY_SRC_DIR)/granary/user/inject.c \
		-o $(GRANARY_BIN_DIR)/granary/user/inject.o
	@$(GRANARY_CC) $(GRANARY_INJECTOR_FLAGS) \
        $(GRANARY_BIN_DIR)/granary/user/inject.o \
        -o $(GRANARY_INJECTOR)

# Compile Granary as a shared library.
$(GRANARY_LIBRARY): $(GRANARY_MERGED_OBJ)
	@echo "Compiling library $(GRANARY_LIBRARY)"
	@$(GRANARY_CC) $(GRANARY_COMMON_ARCH_FLAGS) \
		$(GRANARY_LD_FLAGS) $(GRANARY_LD_FLAGS_EARLY) -shared \
		$(GRANARY_MERGED_OBJ) \
		$(GRANARY_LD_FLAGS_LATE) "-Wl,-Bsymbolic" \
		-o $(GRANARY_LIBRARY)

# Compile Granary as a stand-alone executable.
$(GRANARY_EXE): $(GRANARY_MERGED_OBJ)
	@echo "Linking $(GRANARY_EXE)"
	@$(GRANARY_CC) $(GRANARY_COMMON_ARCH_FLAGS) \
		$(GRANARY_LD_FLAGS) $(GRANARY_LD_FLAGS_EARLY) \
		$(GRANARY_MERGED_OBJ) \
		$(GRANARY_LD_FLAGS_LATE) "-Wl,--export-dynamic" \
		-o $(GRANARY_EXE)

all: $(GRANARY_INJECTOR_TARGET) $(GRANARY_LIBRARY_TARGET) $(GRANARY_EXE_TARGET)
