# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := user
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_BIN_DIR := $(GRANARY_BIN_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o

CLIENT_SRC_FILES = $(wildcard $(CLIENT_SRC_DIR)/*.cc)
CLIENT_OBJ_FILES := $(subst $(CLIENT_SRC_DIR),$(CLIENT_BIN_DIR),$(CLIENT_SRC_FILES:.cc=.o))

CLIENT_GEN_DIR := $(GRANARY_SRC_DIR)/generated/clients/$(CLIENT_NAME)
CLIENT_GEN_FILE := $(CLIENT_GEN_DIR)/offsets.h

$(CLIENT_GEN_FILE):
	@mkdir -p $(@D)
	@-rm $(@) $(GRANARY_DEV_NULL)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENT_DIR)/user/find_symbol_offset.py \
		do_dlopen ld >> $(@)

$(CLIENT_BIN_DIR)/user.o: $(CLIENT_SRC_DIR)/user.cc $(CLIENT_GEN_FILE)
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) $(GRANARY_CXX_FLAGS) -c $< -o $@

$(CLIENT_OBJ): $(CLIENT_OBJ_FILES)
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_LD) -g -r $(CLIENT_OBJ_FILES) -o $(CLIENT_OBJ)

all: $(CLIENT_OBJ)
