# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := util
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_BIN_DIR := $(GRANARY_BIN_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o
CLIENT_DEP_OBJS := $(CLIENT_BIN_DIR)/closure.o
$(CLIENT_OBJ): $(CLIENT_DEP_OBJS)
	@echo "Building combined object $@"
	@mkdir -p $(@D)
	@$(GRANARY_LD) -g -r $^ -o $@

all: $(CLIENT_OBJ)
