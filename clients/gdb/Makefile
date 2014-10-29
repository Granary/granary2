# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := gdb
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o
CLIENT_GEN_DIR := $(GRANARY_SRC_DIR)/generated/clients/$(CLIENT_NAME)
CLIENT_GEN_FILE := $(CLIENT_GEN_DIR)/offsets.h

$(CLIENT_GEN_FILE):
	@mkdir -p $(@D)
	@-rm $(@) $(GRANARY_DEV_NULL)
	@$(GRANARY_SH) ../user/find_symbol_offset.sh _dl_debug_state /lib64/ld-linux-x86-64.so.2 >> $(@)
	@$(GRANARY_SH) ../user/find_symbol_offset.sh nptl_create_event /lib/x86_64-linux-gnu/libpthread.so.0 >> $(@)
	@$(GRANARY_SH) ../user/find_symbol_offset.sh nptl_death_event /lib/x86_64-linux-gnu/libpthread.so.0 >> $(@)
	
$(CLIENT_OBJ): $(CLIENT_SRC_DIR)/$(CLIENT_NAME).cc $(CLIENT_GEN_FILE)
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -c $< -o $@

all: $(CLIENT_OBJ)
