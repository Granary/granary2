# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all generated

include $(GRANARY_SRC_DIR)/Makefile.inc

CLIENT_SRC_DIR := $(GRANARY_CLIENTS_SRC_DIR)/$(CLIENT_NAME)
CLIENT_BIN_DIR := $(GRANARY_CLIENTS_BIN_DIR)/$(CLIENT_NAME)

CLIENT_SRC_FILES = $(wildcard $(CLIENT_SRC_DIR)/*.cc)
CLIENT_OBJ_FILES := $(subst $(CLIENT_SRC_DIR),$(CLIENT_BIN_DIR),$(CLIENT_SRC_FILES:.cc=.bc))

SYSCALL_WRAPPERS := $(CLIENT_GEN_DIR)/syscall.h

$(SYSCALL_WRAPPERS): $(GRANARY_OS_TYPES)
	@echo "Generating system call wrappers file $@"
	@mkdir -p $(@D)
	@$(GRANARY_PYTHON) $(CLIENT_SRC_DIR)/generate_syscall_wrappers.py \
		$(GRANARY_OS_TYPES) > $@

$(CLIENT_OBJ): $(CLIENT_OBJ_FILES)
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_LLVM_LINK) $^ -o $@

all: $(CLIENT_OBJ)

generated: $(SYSCALL_WRAPPERS)
