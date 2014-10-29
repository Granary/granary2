# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := user
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_BIN_DIR := $(GRANARY_BIN_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o

CLIENT_SRC_FILES = $(wildcard $(CLIENT_SRC_DIR)/*.cc)
CLIENT_OBJ_FILES := $(subst $(CLIENT_SRC_DIR),$(CLIENT_BIN_DIR),$(CLIENT_SRC_FILES:.cc=.o))

CLIENT_GEN_FILE := $(CLIENT_GEN_DIR)/memory.h

$(CLIENT_GEN_FILE):
	@mkdir -p $(@D)
	@-rm $(@) $(GRANARY_DEV_NULL)
	@$(GRANARY_SH) find_symbol_offset.sh malloc /lib/x86_64-linux-gnu/libc.so.6 >> $(@)
	@$(GRANARY_SH) find_symbol_offset.sh calloc /lib/x86_64-linux-gnu/libc.so.6 >> $(@)
	@$(GRANARY_SH) find_symbol_offset.sh realloc /lib/x86_64-linux-gnu/libc.so.6 >> $(@)
	@$(GRANARY_SH) find_symbol_offset.sh free /lib/x86_64-linux-gnu/libc.so.6 >> $(@)

$(CLIENT_OBJ): $(CLIENT_OBJ_FILES)
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_LD) -g -r $(CLIENT_OBJ_FILES) -o $(CLIENT_OBJ)

all: $(CLIENT_OBJ)
