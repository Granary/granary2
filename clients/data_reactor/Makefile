# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := data_reactor
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o
CLIENT_GEN_DIR := $(GRANARY_SRC_DIR)/generated/clients/$(CLIENT_NAME)
CLIENT_GEN_FILE := $(CLIENT_GEN_DIR)/offsets.h

$(CLIENT_GEN_FILE):
	@mkdir -p $(@D)
	@-rm $(@) $(GRANARY_DEV_NULL)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENT_DIR)/user/find_symbol_offset.py \
		malloc libc >> $(@)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENT_DIR)/user/find_symbol_offset.py \
		calloc libc >> $(@)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENT_DIR)/user/find_symbol_offset.py \
		realloc libc >> $(@)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENT_DIR)/user/find_symbol_offset.py \
		free libc >> $(@)
	
$(CLIENT_OBJ): $(CLIENT_SRC_DIR)/$(CLIENT_NAME).cc $(CLIENT_GEN_FILE)
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -c $< -o $@

all: $(CLIENT_OBJ)
